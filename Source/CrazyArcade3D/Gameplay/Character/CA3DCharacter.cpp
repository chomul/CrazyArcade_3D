#include "Gameplay/Character/CA3DCharacter.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Character/OcclusionFadeComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Bomb/PredictedBombVisual.h"
#include "Gameplay/CA3DFeedback.h"
#include "Core/PoolSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Framework/CA3DPlayerState.h" // 복제되는 관전 카메라 각의 출처 — .cpp 에서만
#include "Core/CameraYawSnap.h"        // 90도 스냅 공식의 단일 출처 (Gameplay→Core 는 허용)
#include "GameFramework/PlayerController.h"  // 로컬 플레이어 판정 (GetViewRotation 분기)
#include "Camera/PlayerCameraManager.h"      // PendingViewTarget — 블렌드 중 새 대상 판정
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"   // 외형 적용(Task 37) — 메시·애님 클래스 교체
#include "Animation/AnimMontage.h"              // 설치 몽타주(Task 38)
#include "Net/UnrealNetwork.h"                  // DOREPLIFETIME (BombPlaceCounter)
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	// 룰셋 해석 — 예측 비주얼 클래스의 출처. GameState 복제 포인터 → CDO 폴백
	// (Bomb.cpp 의 ResolveBombRules 와 동일 관례. 시각 전용이라 CMC 튜닝처럼 대기하지 않는다).
	const UCA3DRuleSet* ResolveVisualRules(const UWorld* World)
	{
		if (World)
		{
			if (const ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>())
			{
				if (GameState->Rules)
				{
					return GameState->Rules;
				}
			}
		}
		return GetDefault<UCA3DRuleSet>();
	}
}

ACA3DCharacter::ACA3DCharacter()
{
	PrimaryActorTick.bCanEverTick = true; // 서버 낙사 검사 (Tick)

	// 캡슐 크기는 엔진 기본(반지름 34, 반높이 88) 유지 — ⚠️ 임시, 셀 크기 확정 시 함께 튜닝.
	// 1칸 폭(임시 100) 통로 통과 가능: 지름 68 < 100.

	// 90도 스냅 카메라(Task 11)와의 역할 분담: 카메라 yaw 는 컨트롤러 ControlRotation 이
	// 소유하므로 캐릭터가 카메라를 따라 돌면 안 된다 — 이동 방향을 보게 한다.
	bUseControllerRotationYaw = false;                        // CMC 기본에서 변경 (기본 true)
	GetCharacterMovement()->bOrientRotationToMovement = true; // CMC 기본에서 변경 (기본 false)

	// ⚠️ 임시 초기값 — "CellSize 100 × 룰셋 기본 계수" 와 같은 값. BeginPlay 의
	// TryApplyMovementTuning 이 실제 CellSize × 룰셋 계수로 덮어쓴다. 초기값을 파생 결과와
	// 맞춰두면 클라에 룰셋이 한 틱 늦게 도착해도 서버·클라 CMC 값 불일치 구간이 없다.
	GetCharacterMovement()->MaxWalkSpeed = 400.f;   // MoveSpeedCellsPerSec 4 × 100
	GetCharacterMovement()->JumpZVelocity = 523.8f; // sqrt(2 × 980 × 1.4 × 100) — 정점 1.4칸
	GetCharacterMovement()->MaxStepHeight = 30.f;   // StepHeightCellFactor 0.3 × 100

	// 즉각 반응 이동 (엔진 기본 2048 은 0.2초에 걸쳐 붙고 떨어져 "미끄러지는" 느낌을 준다).
	// 실제 값은 RefreshMoveSpeed 가 "속도 ÷ 룰셋 시간" 으로 매번 다시 계산한다.
	GetCharacterMovement()->MaxAcceleration = 8000.f;              // 400 ÷ MoveAccelTime 0.05
	GetCharacterMovement()->bUseSeparateBrakingFriction = true;    // 제동은 감속도만 (마찰 0)
	GetCharacterMovement()->BrakingFriction = 0.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 8000.f;   // 400 ÷ MoveBrakeTime 0.05

	// 공중도 지상과 같은 조작감 — 엔진 기본 AirControl 0.05 는 점프 중 방향 전환이 사실상 안 된다.
	GetCharacterMovement()->AirControl = 1.f;
	GetCharacterMovement()->FallingLateralFriction = 0.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 8000.f;

	// 카메라 붐: 컨트롤러의 고정 pitch + 스냅 yaw(ControlRotation)를 그대로 사용.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->TargetArmLength = 1200.f; // ⚠️ 임시 — CameraDistanceCells 12 × 100. 튜닝이 덮어씀

	// 붐 컬리전 끔 (2026-07-30 사용자 결정): 켜두면 붐이 지형에 닿는 순간 팔 길이를 줄여
	// 카메라가 캐릭터에 확 붙는다 — 고정 시점 아케이드에서는 시야 배율이 튀는 쪽이
	// 지형에 살짝 걸치는 것보다 훨씬 나쁘다 (칸 세기가 무너진다).
	// 가림 블록 처리는 3주차 디더 페이드로 (설계서) — 거리를 줄이는 방식으로는 해결하지 않는다.
	CameraBoom->bDoCollisionTest = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom);
	FollowCamera->bUsePawnControlRotation = false; // 붐이 회전을 다 처리한다

	// 위 주석이 예고한 디더 페이드의 실행부 (2026-08-06). 순수 시각 — 게임 상태를 안 만진다.
	// 데디 서버는 BeginPlay 에서 스스로 틱을 끈다 (컴포넌트는 남지만 아무것도 안 한다).
	OcclusionFade = CreateDefaultSubobject<UOcclusionFadeComponent>(TEXT("OcclusionFade"));

	// 스탯·생존 상태 (Task 12) — 봇과 플레이어가 완전히 같은 코드 경로를 탄다.
	Status = CreateDefaultSubobject<UStatusComponent>(TEXT("Status"));

	// 위험 데칼이 캐릭터 몸에 빨갛게 입혀지는 것 방지 (데칼은 발판 표시용 — 판정과 무관).
	if (USkeletalMeshComponent* CharMesh = GetMesh())
	{
		CharMesh->SetReceivesDecals(false);
	}

	// 폭탄 클래스 기본값 — BP_Bomb 서브클래스가 메시·이펙트만 덮어쓴다 (BP 로직 금지).
	BombClass = ABomb::StaticClass();
}

UStatusComponent* ACA3DCharacter::GetStatus() const
{
	return Status;
}

void ACA3DCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACA3DCharacter, BombPlaceCounter);
}

void ACA3DCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer())
	{
		// 데디 서버는 시각이 필요 없다 (불변식 5) — 카메라 컴포넌트 파괴 (VoxelWorld 렌더러와 동일 관례).
		if (FollowCamera)
		{
			FollowCamera->DestroyComponent();
			FollowCamera = nullptr;
		}
		if (CameraBoom)
		{
			CameraBoom->DestroyComponent();
			CameraBoom = nullptr;
		}
	}

	TryApplyMovementTuning();
}

void ACA3DCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 서버 응답 전에 캐릭터가 파괴되면 예측 비주얼이 고아로 남는다 — 남은 것 전부 반납.
	// 월드 정리 중에는 풀 조작을 건너뛴다 (ABomb::EndPlay 관례 — 죽어가는 액터 반납 금지).
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;
		for (const TObjectPtr<APredictedBombVisual>& Visual : PredictedBombVisuals)
		{
			if (Pool && IsValid(Visual))
			{
				Pool->Release(Visual);
			}
		}
	}
	PredictedBombVisuals.Empty();

	// 파괴·레벨 전환 중 지연 숨김 타이머가 남지 않게 정리 (UStatusComponent::EndPlay 의
	// TrappedTimer 와 같은 관례 — 액터 파괴 시 자동 정리되긴 하지만 경로를 명시한다).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeathHideTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ACA3DCharacter::TryApplyMovementTuning()
{
	if (bMovementTuningApplied)
	{
		return; // 재진입 가드 — 지연 재시도 타이머 대비
	}

	// ── 1. VoxelWorld 탐색·캐시 — CellSize 와 좌표 변환(GetFootCell)의 유일한 출처 ──
	if (!VoxelWorld)
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			VoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약 — 첫 번째만 사용
		}
	}

	// ── 2. 룰셋 해석 — VoxelWorld::InitGridFromSeed 와 동일 관례 ──
	const UCA3DRuleSet* Rules = nullptr;
	const AGameStateBase* GameStateBase = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (const ACA3DGameState* CA3DGameState = Cast<ACA3DGameState>(GameStateBase))
	{
		Rules = CA3DGameState->Rules;
	}
	if (!Rules)
	{
		if (HasAuthority() || (GameStateBase && !GameStateBase->IsA<ACA3DGameState>()))
		{
			// 서버에서 여기 도달은 GameState 없는 테스트 월드나 GameMode 미설정 맵뿐 —
			// 기다릴 대상이 없으니 기본 룰셋(CDO)으로 진행. GameState 타입이 다르면
			// Rules 는 영원히 안 오므로(서버도 같은 폴백) 클라도 기본값으로 맞춘다.
			Rules = GetDefault<UCA3DRuleSet>();
		}
	}

	if (!VoxelWorld || !Rules)
	{
		// 알려진 함정(리플리케이션 순서): 클라에서 GameState/Rules 복제가 폰 BeginPlay 보다
		// 늦게 도착할 수 있다. 서버와 다른 값으로 CMC 를 굳히면 예측이 어긋나므로
		// next-tick 재시도 (VoxelWorld.cpp 와 동일한 안전책).
		UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter: VoxelWorld/룰셋 미확보 — CMC 튜닝을 다음 틱으로 지연"));
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ACA3DCharacter::TryApplyMovementTuning));
		return;
	}

	// ── 3. 적용 — 전부 "셀 단위 계수 × CellSize" (셀 크기를 바꿔도 감각 유지, 하드코딩 금지) ──
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const float CellSize = VoxelWorld->CellSize;

	// 이동속도: 1초에 몇 칸 = 기본 속도. 실제 MaxWalkSpeed 반영은 RefreshMoveSpeed
	// 단일 경로 — Status 의 MoveSpeedMul·Trapped 상태와 곱해진다 (Task 12).
	CachedRules = Rules;
	BaseWalkSpeed = Rules->MoveSpeedCellsPerSec * CellSize;
	RefreshMoveSpeed();

	// 자동 오르기 한계: 1칸 미만이어야 "층간 이동은 점프만"(GDD 2.1)이 유지된다.
	Movement->MaxStepHeight = Rules->StepHeightCellFactor * CellSize;

	// 점프: 목표 정점 높이 = 계수 × CellSize. 등가속 포물선 v = sqrt(2·g·h) —
	// 계수가 (1, 2) 구간이면 1칸은 오르고 2칸은 못 오른다 (계수 근거는 룰셋 주석).
	const float GravityZ = Movement->GetGravityZ(); // GravityScale 포함
	ensureMsgf(GravityZ < 0.f, TEXT("ACA3DCharacter: 중력이 0 이상 (%f) — JumpZVelocity 계산 불가"), GravityZ);
	const float TargetApexHeight = Rules->JumpApexCellFactor * CellSize;
	Movement->JumpZVelocity = FMath::Sqrt(2.f * FMath::Abs(GravityZ) * TargetApexHeight);

	// 카메라 붐 길이 (시각 전용 — 데디 서버는 BeginPlay 에서 이미 파괴됨, 불변식 5).
	if (!IsRunningDedicatedServer() && CameraBoom)
	{
		CameraBoom->TargetArmLength = Rules->CameraDistanceCells * CellSize;
	}

	bMovementTuningApplied = true;
	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DCharacter %s: CMC 튜닝 적용 — CellSize %.0f / MaxWalkSpeed %.0f (%.1f칸/s) / JumpZVelocity %.1f (정점 %.2f칸) / MaxStepHeight %.0f"),
		*GetName(), CellSize, Movement->MaxWalkSpeed, Rules->MoveSpeedCellsPerSec,
		Movement->JumpZVelocity, Rules->JumpApexCellFactor, Movement->MaxStepHeight);
}

void ACA3DCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 선택 캐릭터 외형 폴링 (Task 37) — **아래 권한 가드보다 앞**이다: 외형은 클라에서도
	// 적용돼야 한다 (원격 폰 포함). 데디만 함수 내부 최상단에서 걸러진다 —
	// 리슨 서버·스탠드얼론은 서버이면서 화면도 있으므로 IsRunningDedicatedServer 만 거른다.
	ApplyCharacterAppearance();

	// 설치 몽타주 폴링 (Task 38) — 외형과 같은 사정(권한 가드 앞·데디만 내부에서 거름).
	UpdateBombPlaceMontage();

	if (!HasAuthority()) return; // 불변식 5 — 낙사 판정은 상태 변경으로 이어지므로 서버 전용

	// 낙사 → ServerKill. Dead 면 스킵 (중복 호출 방지). 갇힌 채 추락하는 상황은
	// 막지 않는다 (미결정 정책) — Trapped 여도 그대로 낙사 처리된다.
	if (Status && Status->LifeState != ELifeState::Dead && GetActorLocation().Z < KillZ)
	{
		// 사망 원인 분기 (Task 24): 서든데스 진행 중이면 SuddenDeath, 아니면 Fall.
		//
		// ⚠️ "누가 이 구멍을 냈는가" 를 추적하지 않고 **시각으로만** 판정한다. 추적하려면
		// 파괴된 셀마다 원인(폭탄 소유자 / 서든데스)을 기록해야 하는데, 그건 FVoxelGrid 가
		// "누가 왜 부쉈는지" 를 알아야 한다는 뜻이고 Voxel 의 독립성(폴더 의존 규칙 —
		// "지형은 게임 규칙을 몰라야 함")이 그 자리에서 무너진다. 통계·킬 피드용 분류에
		// 그만한 비용을 치를 이유가 없다.
		// 실질적으로도 어긋나지 않는다: 서든데스 전에는 바닥이 파괴되지 않아
		// (bFloorDestructible=false) 구멍 자체가 생기지 않는다 — 그때의 낙사는 맵 밖 추락뿐이다.
		const ACA3DGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACA3DGameState>() : nullptr;
		const bool bSuddenDeath = GameState && GameState->bSuddenDeathActive;

		UE_LOG(LogCA3D, Log, TEXT("ACA3DCharacter %s: KillZ(%.0f) 아래로 낙하 — 낙사 처리 (%s)"),
			*GetName(), KillZ, bSuddenDeath ? TEXT("서든데스") : TEXT("추락"));
		Status->ServerKill(bSuddenDeath ? EDeathCause::SuddenDeath : EDeathCause::Fall);
	}

	// 갇힌 상대 터뜨리기 (2026-08-10 확정) — 내가 갇혀 있을 때만 실제로 무언가를 한다.
	// 낙사 판정 **뒤**에 두는 이유: 같은 틱에 둘 다 성립하면 이미 KillZ 아래로 떨어진 쪽이
	// 사실이고, 그 경우 여기는 ServerKill 의 Dead 가드로 조용히 no-op 이 된다.
	ServerTryPopIfTouched();

	// 폭탄 킥 (2026-08-06 확정) — 방향키로 민다. 전용 키가 없으므로 "지금 어느 쪽으로 밀고
	// 있는가" 를 매 틱 본다.
	//
	// 입력의 출처가 CMC 의 **현재 가속도**인 이유: 원격 클라의 AddMovementInput 은 서버에서
	// 불리지 않지만(GetLastMovementInputVector 는 서버에서 항상 0), ServerMove 가 실어 보낸
	// 가속도를 CMC 가 서버에서 그대로 적용하므로 서버에도 같은 값이 있다. 봇(AIController)은
	// 서버에서 직접 Move 를 부르므로 같은 값이 자연스럽게 채워진다 — 사람과 봇이 한 경로.
	if (const UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		ServerTryKickBomb(Movement->GetCurrentAcceleration());
	}
}

// ─── 선택 캐릭터 외형 (Task 37) ──────────────────────────────────────────────

void ACA3DCharacter::ApplyCharacterAppearance()
{
	// ⚠️ HISM 함정(CLAUDE.md — "시각 전용인 줄 알았던 것이 유일한 컬리전")과 달리 이건
	// **진짜 시각 전용**이다: 캐릭터의 물리 형상은 캡슐이 담당하고, 스켈레탈 메시는 어떤
	// 판정에도 쓰이지 않는다 (피격은 셀 단위, 접촉은 GetContactReach 의 캡슐 치수) —
	// 데디에서 통째로 건너뛰어도 서버 판정이 달라질 수 없다.
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용

	// **OnRep 체인을 잡지 않고 틱 폴링 + 스냅샷 비교** (HUD/MatchWidget 관례).
	// 재료가 폰·PlayerState·GameState(Rules) 세 액터에 흩어져 있고 클라에서 복제 도착 순서
	// 보장이 없다 — 어느 하나의 OnRep 에 걸면 "나머지 둘이 아직" 인 조합을 전부 다시 얽어야
	// 한다. 폴링은 셋 다 도착했고 값이 바뀐 틱에만 일하고, 그 전에는 비교 몇 번이 전부다.
	const ACA3DPlayerState* CA3DPlayerState = GetPlayerState<ACA3DPlayerState>();
	const ACA3DGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACA3DGameState>() : nullptr;
	const UCA3DRuleSet* Rules = GameState ? GameState->Rules.Get() : nullptr;
	if (!CA3DPlayerState || !Rules)
	{
		return; // 복제 미도착 — 다음 틱에 다시 본다 (TryApplyMovementTuning 과 같은 사정)
	}

	const int32 Index = CA3DPlayerState->CharacterIndex;
	if (Index == AppliedCharacterIndex || Index == INDEX_NONE)
	{
		return; // 미확정이거나 이미 처리한 인덱스 — 재적용 금지 (스냅샷 비교)
	}

	// 범위 밖·Mesh 미지정·메시 컴포넌트 없음은 조용히 스킵하되 **스냅샷은 기록한다** —
	// 룰셋 에셋은 매치 중 바뀌지 않으므로 재시도해도 결과가 같고, 기록해야 로그가 1회로
	// 끝난다 (Verbose 1회 관례). 기본 외형(BP 서브클래스의 메시)이 그대로 남는다.
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!Rules->Characters.IsValidIndex(Index) || !Rules->Characters[Index].Mesh || !MeshComp)
	{
		UE_LOG(LogCA3D, Verbose,
			TEXT("ACA3DCharacter %s: 캐릭터 정의 미비 — 인덱스 %d (목록 %d개) 외형 적용 스킵"),
			*GetName(), Index, Rules->Characters.Num());
		AppliedCharacterIndex = Index;
		return;
	}

	const FCA3DCharacterDef& Def = Rules->Characters[Index];

	// 메시 먼저, 애님은 그 다음 — SetSkeletalMesh 가 AnimInstance 를 재초기화하므로
	// 순서를 바꾸면 새 애님이 이전 메시 기준으로 한 번 초기화됐다 버려진다.
	MeshComp->SetSkeletalMesh(Def.Mesh);
	if (Def.AnimClass)
	{
		MeshComp->SetAnimInstanceClass(Def.AnimClass);
	}

	// 상대 트랜스폼은 **대체**한다 (기존 값에 더하지 않는다). 기존 C++ 생성자는 메시 상대
	// 위치를 건드리지 않지만 BP 서브클래스가 기본 메시용 값을 갖고 있을 수 있다 — 더하기로
	// 하면 "이전에 어떤 메시가 있었나" 에 결과가 좌우되고 재선택 시 누적되어 멱등이 깨진다.
	// 정의 데이터가 트랜스폼을 통째로 소유해야 8종을 한 벌의 코드로 붙인다 (FCA3DCharacterDef 주석).
	// 기준은 캡슐 바닥(-반높이): 언리얼 스켈레탈 메시 관례(발 = 원점)에서 MeshOffset 기본값
	// ZeroVector 가 곧 "발이 바닥에 닿는" 표준 배치다. MeshOffset 은 원점이 표준과 다른
	// 외부 에셋의 보정값, MeshYawOffset 기본 -90 은 "메시 Y+ 정면 → 캡슐 X+ 정면" 관례 보정.
	const float HalfHeight = GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const FVector MeshRelativeLocation = FVector(0.f, 0.f, -HalfHeight) + Def.MeshOffset;
	const FRotator MeshRelativeRotation(0.f, Def.MeshYawOffset, 0.f);
	MeshComp->SetRelativeLocationAndRotation(MeshRelativeLocation, MeshRelativeRotation);
	MeshComp->SetRelativeScale3D(FVector(Def.MeshScale));

	// CMC 스무딩 캐시 갱신 (Task 39). CMC 의 SmoothClientPosition_UpdateVisuals 가 시뮬레이티드
	// 프록시에서 **매 프레임** 메시 상대 트랜스폼을 "스무딩 오프셋 + GetBaseTranslationOffset/
	// GetBaseRotationOffset" 으로 다시 쓰는데, 그 Base 캐시는 PostInitializeComponents 시점 값이다.
	// 여기서 갱신하지 않으면 위에서 적용한 트랜스폼을 스무딩이 낡은 오프셋으로 되돌린다 —
	// **원격 프록시에서만** 클라마다 메시 위치·방향이 어긋난다 (엔진 주석: "call this at runtime
	// if you intend to change the default mesh offset from the capsule").
	CacheInitialMeshOffset(MeshRelativeLocation, MeshRelativeRotation);

	AppliedCharacterIndex = Index;
	UE_LOG(LogCA3D, Log, TEXT("ACA3DCharacter %s: 캐릭터 외형 적용 — 인덱스 %d (%s)"),
		*GetName(), Index, *Def.DisplayName.ToString());
}

// ─── 폭탄 설치 Attack 몽타주 (Task 38) ──────────────────────────────────────

bool ACA3DCharacter::ShouldPlayFromCounter(int32 Counter, int32 Snapshot, bool bLocallyControlled, bool bHasAuthority)
{
	if (Counter == Snapshot)
	{
		return false; // 변화 없음 — 폴링의 평상시 경로
	}
	if (Snapshot == INDEX_NONE)
	{
		return false; // 첫 관측 — 과거 설치분(늦은 접속)에 헛스윙하지 않는다. 동기화만
	}
	if (bLocallyControlled && !bHasAuthority)
	{
		return false; // 원격 클라 본인 — 예측이 이미 재생했다 (두 번 휘두르면 안 된다)
	}
	return true; // 리슨 호스트 본인 · 봇 · 원격 시뮬레이티드 프록시
}

void ACA3DCharacter::UpdateBombPlaceMontage()
{
	// 몽타주는 순수 시각 — 설치 판정(ServerPlaceBomb 권위 검증)과 무관하다 (외형 적용과 같은 근거).
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용

	const int32 Counter = BombPlaceCounter;
	if (Counter == AppliedBombPlaceCounter)
	{
		return; // 평상시 — 비교 한 번이 전체 비용 (스냅샷 비교 관례)
	}

	const bool bPlay = ShouldPlayFromCounter(
		Counter, AppliedBombPlaceCounter, IsLocallyControlled(), HasAuthority());
	AppliedBombPlaceCounter = Counter; // 재생 여부와 무관하게 동기화 — 같은 변화에 두 번 반응 금지

	if (bPlay)
	{
		PlayAttackMontage();
	}
}

void ACA3DCharacter::PlayAttackMontage()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용

	// 몽타주의 출처는 외형(ApplyCharacterAppearance)과 같은 룰셋 캐릭터 정의다 — 캐릭터마다
	// 스켈레톤이 달라 "무슨 몽타주인가" 는 CharacterIndex 를 따라가야 한다.
	const ACA3DPlayerState* CA3DPlayerState = GetPlayerState<ACA3DPlayerState>();
	const int32 Index = CA3DPlayerState ? CA3DPlayerState->CharacterIndex : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return; // 선택 미확정(선택 페이즈 전·PlayerState 미도착) — 조용히 생략
	}

	const UCA3DRuleSet* Rules = ResolveVisualRules(GetWorld());
	if (!Rules->Characters.IsValidIndex(Index) || !Rules->Characters[Index].AttackMontage)
	{
		return; // 정의 미비·몽타주 미지정 — 재생만 생략 (설치 자체는 정상, 외형 스킵과 같은 관례)
	}

	PlayAnimMontage(Rules->Characters[Index].AttackMontage);
}

// ─── 관전 카메라 각 (2026-08-09) ─────────────────────────────────────────────

const APlayerController* ACA3DCharacter::GetLocalViewingController() const
{
	// 내가 로컬 플레이어의 폰이면 그 컨트롤러가 곧 카메라다 — ViewTarget 조회보다 **먼저** 본다.
	// 카메라 매니저가 없는 환경(자동화 테스트 월드)에서도 이 가지는 항상 성립하고,
	// 살아 있는 내 폰이라는 가장 흔한 경우를 반복 없이 끝낸다.
	if (const APlayerController* OwningPC = Cast<APlayerController>(GetController()))
	{
		if (OwningPC->IsLocalPlayerController())
		{
			return OwningPC;
		}
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalPlayerController())
		{
			continue; // 원격 플레이어의 컨트롤러(서버에 뜬 것)는 이 머신의 카메라가 아니다
		}

		// 블렌드 중에는 **이전 대상(ViewTarget)과 새 대상(PendingViewTarget) 둘 다** 카메라
		// 계산을 탄다. 한쪽만 인정하면 대상 전환 0.4초 동안 한 화면 안에서 각이 두 개가 되어
		// 카메라가 휘청인다 — 둘 다 같은 컨트롤러 각을 돌려줘야 위치만 매끄럽게 옮겨간다.
		if (PC->GetViewTarget() == this)
		{
			return PC;
		}
		if (PC->PlayerCameraManager && PC->PlayerCameraManager->PendingViewTarget.Target == this)
		{
			return PC;
		}
	}
	return nullptr;
}

float ACA3DCharacter::GetReplicatedCamYawDeg() const
{
	// PlayerState 가 아직 안 왔으면(스폰 직후) 0번 칸. 다음 프레임이면 도착한다.
	const ACA3DPlayerState* CA3DPlayerState = GetPlayerState<ACA3DPlayerState>();
	return CameraYawSnap::IndexToYawDeg(CA3DPlayerState ? CA3DPlayerState->CamYawIndex : 0);
}

FRotator ACA3DCharacter::GetViewRotation() const
{
	// **지금 이 폰을 보고 있는 로컬 컨트롤러**가 있으면 그 컨트롤러의 ControlRotation 이 카메라
	// 각이다. 살아서 조종 중인 내 폰과, 죽어서 관전 중인 남의 폰이 **같은 가지**로 처리된다 —
	// 그래야 관전 중에도 Q/E 회전이 그대로 먹는다 (2026-08-09 사용자 요청).
	//
	// 관전자별로 다른 각을 보게 되지만 그게 맞다: 카메라는 보는 사람의 것이다. 한 머신에
	// 로컬 컨트롤러는 하나뿐이라 "누구 각을 쓸까" 라는 모호함도 없다.
	if (const APlayerController* Viewer = GetLocalViewingController())
	{
		return Viewer->GetControlRotation();
	}

	// 아무도 로컬에서 보고 있지 않은 폰(봇 · 원격 플레이어) = **이 폰의 표현**.
	// 관전 대상을 이 폰으로 바꾸는 순간 관전자가 이 각을 **시작각으로 받아 간다**
	// (ACA3DPlayerController::SetSpectateTarget) — "다른 플레이어가 보는 시점 그대로" 는
	// 여기서 성립한다. 받은 뒤부터는 관전자가 Q/E 로 자유롭게 돌린다.
	return FRotator(ResolveVisualRules(GetWorld())->CameraPitchDeg, GetReplicatedCamYawDeg(), 0.f);
}

void ACA3DCharacter::ServerTryKickBomb(const FVector& WorldInputDirection)
{
	if (!HasAuthority()) return; // 불변식 5 — 폭탄을 움직이는 것은 상태 변경이다

	// 킥 아이템이 없으면 지금까지처럼 그냥 벽이다 (ABomb::BlockingBox 가 막는다) —
	// 승격 로직은 건드리지 않고 킥 경로만 따로 낸 것이 이 설계의 요점이다.
	// 갇힘(Trapped)·사망 중에는 못 찬다: 갇힌 채로 폭탄을 밀 수 있으면 물방울의 의미가 없다
	// (DoJump 가 Trapped 를 막는 것과 같은 근거 — 미세 이동은 허용, 판을 바꾸는 행동은 금지).
	if (!Status || !Status->bHasKick || Status->LifeState != ELifeState::Alive)
	{
		return;
	}
	if (!VoxelWorld)
	{
		return; // 좌표 변환(GetFootCell) 불가 — BeginPlay 이전이거나 VoxelWorld 없는 맵
	}

	// 방향을 **한 축으로 접는다.** 미끄러짐은 그리드 4방향 규칙(폭발 전파와 같은 축)이라
	// 대각 입력으로 두 폭탄을 동시에 밀거나 대각선으로 굴러가는 일이 있으면 안 된다.
	const FVector2D Flat(WorldInputDirection.X, WorldInputDirection.Y);
	if (Flat.IsNearlyZero())
	{
		return; // 서 있는 중 — 밀고 있지 않다
	}

	const FIntVector Direction = FMath::Abs(Flat.X) >= FMath::Abs(Flat.Y)
		? FIntVector(Flat.X > 0.f ? 1 : -1, 0, 0)
		: FIntVector(0, Flat.Y > 0.f ? 1 : -1, 0);

	UExplosionSubsystem* Explosion = GetWorld() ? GetWorld()->GetSubsystem<UExplosionSubsystem>() : nullptr;
	if (!Explosion)
	{
		return;
	}

	// 레지스트리 조회 — 폭탄의 실제 위치가 아니라 **셀**로 찾는다. 폭발 판정과 같은 키를 써야
	// "차진 칸" 과 "터지는 칸" 이 갈리지 않는다.
	ABomb* Bomb = Explosion->FindBombAt(GetFootCell() + Direction);
	if (!Bomb)
	{
		return;
	}

	// 접촉 판정 — "걸어 들어가면" 이므로 실제로 닿아야 한다. 옆 칸에 들어서자마자 차이면
	// 폭탄이 손도 대기 전에 한 칸(CellSize) 앞서 도망가는 것처럼 보인다.
	// 사거리는 실제 형상에서 파생한다 — 공식은 GetContactReach 한 곳뿐이다 (갇힌 상대
	// 터뜨리기도 같은 함수를 쓴다. 두 벌이 되면 "차지는 거리"와 "터지는 거리"가 갈라진다).
	const float Reach = GetContactReach(
		GetCapsuleComponent()->GetScaledCapsuleRadius(), Bomb->GetBlockingExtent());

	// 미는 축의 거리만 본다 — 수직 정렬은 위의 "발밑 셀 + 방향 == 폭탄 셀" 이 이미 보장한다.
	const FVector Delta = Bomb->GetActorLocation() - GetActorLocation();
	const float AxisDistance = FMath::Abs(Direction.X != 0 ? Delta.X : Delta.Y);
	if (AxisDistance > Reach)
	{
		return;
	}

	// 시작 조건(이미 미끄러지는 중·폭발 예약·앞이 막힘)은 폭탄이 단독으로 판단한다 —
	// 규칙을 두 곳에 적으면 언젠가 한쪽만 바뀐다 (ServerUseNeedle → ServerEscape 와 같은 관례).
	Bomb->ServerStartKick(Direction);
}

// ─── 접촉 판정의 단일 출처 ───────────────────────────────────────────────────

float ACA3DCharacter::GetContactReach(float SelfExtent, float TargetExtent) const
{
	// 튜닝 미도착 시 CDO 폴백 (RefreshMoveSpeed·TryApplyMovementTuning 관례).
	const UCA3DRuleSet* Rules = CachedRules ? CachedRules.Get() : GetDefault<UCA3DRuleSet>();

	// 여유는 **셀 단위 계수 × CellSize** — 셀 크기를 바꿔도 접촉 감각이 유지된다.
	// VoxelWorld 가 없으면(BeginPlay 이전·없는 맵) 여유 0 — 두 형상이 실제로 겹칠 때만 인정한다.
	const float CellSize = VoxelWorld ? VoxelWorld->CellSize : 0.f;
	return SelfExtent + TargetExtent + Rules->BombKickReachToleranceCells * CellSize;
}

// ─── 갇힌 상대 터뜨리기 (2026-08-10 사용자 확정) ──────────────────────────────

void ACA3DCharacter::ServerTryPopIfTouched()
{
	if (!HasAuthority()) return; // 불변식 5 — 사망은 상태 변경이다 (데디에서도 반드시 돈다)

	// ⚠️ **여기가 이 기능의 비용 전부다.** 갇힌 사람이 아니면 한 줄에서 되돌아간다 —
	//    아무도 갇혀 있지 않은 평상시에는 캐릭터당 enum 비교 하나가 전체 비용이다.
	//    캐시한 플래그가 아니라 LifeState 를 매번 읽는 것이 중요하다: 같은 프레임에 니들
	//    탈출이 먼저 돌았으면 여기서 곧바로 Alive 로 보이고, 그래서 "탈출한 뒤에 죽는" 일이 없다.
	if (!Status || Status->LifeState != ELifeState::Trapped)
	{
		return;
	}

	const UCA3DRuleSet* Rules = CachedRules ? CachedRules.Get() : GetDefault<UCA3DRuleSet>();
	if (!Rules->bPopTrappedOnContact)
	{
		return; // 룰셋에서 끈 상태 — 갇힘은 오직 익사 타이머로만 끝난다
	}

	UWorld* World = GetWorld();
	const UCapsuleComponent* SelfCapsule = GetCapsuleComponent();
	if (!World || !SelfCapsule)
	{
		return;
	}
	const float SelfRadius     = SelfCapsule->GetScaledCapsuleRadius();
	const float SelfHalfHeight = SelfCapsule->GetScaledCapsuleHalfHeight();
	const FVector SelfLocation = GetActorLocation();

	// 후보는 캐릭터뿐이다. ABomb::CanKickInto 가 "폭탄을 막는 플레이어" 를 찾는 것과 **같은
	// 순회 방식**을 쓴다 — 컬리전 오버랩 이벤트를 쓸 수 없기 때문이다: 캐릭터 캡슐은 엔진
	// 기본 `Pawn` 프로파일이고 그 프로파일은 Visibility 만 Ignore 로 덮으므로 **Pawn↔Pawn 이
	// Block** 이다(2026-08-10 코드 확인: Character.cpp 의 SetCollisionProfileName(Pawn_ProfileName)
	// + BaseEngine.ini 의 Pawn 프로파일). Block 인 쌍은 오버랩 이벤트를 아예 만들지 않는다.
	for (ACA3DCharacter* Other : TActorRange<ACA3DCharacter>(World))
	{
		if (!IsValid(Other) || Other == this)
		{
			continue; // 자기 자신은 무관 — 혼자 갇혀 있는 것만으로 죽지 않는다
		}

		// 터뜨리는 쪽은 **Alive 여야 한다**: 갇힌 사람끼리는 서로 못 터뜨리고(둘 다 Trapped),
		// 시체도 못 터뜨린다(Dead — GDD "유령 방해 없음" 과 같은 정신).
		const UStatusComponent* OtherStatus = Other->GetStatus();
		if (!OtherStatus || OtherStatus->LifeState != ELifeState::Alive)
		{
			continue;
		}

		const UCapsuleComponent* OtherCapsule = Other->GetCapsuleComponent();
		if (!OtherCapsule)
		{
			continue;
		}

		// 수직 먼저 — 아래층·위층 사람이 평면상 겹쳐 있다고 터뜨리면 안 된다.
		// 두 캡슐의 반높이 합을 넘어서면 애초에 몸이 닿을 수 없다.
		const FVector Delta = Other->GetActorLocation() - SelfLocation;
		if (FMath::Abs(Delta.Z) > GetContactReach(SelfHalfHeight, OtherCapsule->GetScaledCapsuleHalfHeight()))
		{
			continue;
		}

		// 수평은 **평면 거리**로 본다. 킥이 한 축만 보는 것은 폭탄이 그리드 축을 따라 미끄러져
		// 나머지 축 정렬이 셀 비교로 이미 보장되기 때문이고, 사람은 어느 방향에서든 닿을 수
		// 있으므로 그 특수화가 성립하지 않는다 — 사거리 공식 자체는 같다.
		if (FVector2D(Delta.X, Delta.Y).SizeSquared()
			> FMath::Square(GetContactReach(SelfRadius, OtherCapsule->GetScaledCapsuleRadius())))
		{
			continue;
		}

		UE_LOG(LogCA3D, Log, TEXT("ACA3DCharacter %s: 갇힌 채로 %s 에게 접촉 — 즉시 터짐"),
			*GetName(), *Other->GetName());

		// **단일 진입점**을 통과한다 — 순위·생존자 수 판정은 ACA3DGameMode 단독이라는
		// 기존 구조를 그대로 탄다 (ServerKill → NotifyPlayerDeath → 다음 틱 PendingDeaths 해소).
		Status->ServerKill(EDeathCause::Popped);
		return; // 이미 죽었다 — 두 명이 동시에 밀어도 사망은 한 번이다
	}
}

void ACA3DCharacter::RefreshMoveSpeed()
{
	// 서버·클라 단일 재계산 경로 — StatusComponent 의 Server*(서버)와 OnRep(클라)
	// 양쪽이 이 함수만 탄다. 공식이 갈라지면 CMC 예측이 어긋난다 (중복 공식 금지).
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	// 튜닝 미도착 시 CDO 폴백 (TryApplyMovementTuning 관례).
	const UCA3DRuleSet* Rules = CachedRules ? CachedRules.Get() : GetDefault<UCA3DRuleSet>();

	// 갇힘이면 미세 이동만 (GDD 2.3), 아니면 기본 속도 × 아이템 배율.
	const float GroundSpeed = (Status && Status->LifeState == ELifeState::Trapped)
		? Rules->TrappedMoveSpeed
		: BaseWalkSpeed * (Status ? Status->MoveSpeedMul : 1.f);

	// 공중에서는 수평 속도만 줄인다 — 체공 시간(점프 높이)은 그대로라 이동 거리가 정확히 계수배가 된다.
	// MOVE_Falling 도 GetMaxSpeed 로 MaxWalkSpeed 를 쓰므로 상한은 이 한 값으로 통제된다.
	const float Speed = Movement->IsFalling() ? GroundSpeed * Rules->JumpAirSpeedFactor : GroundSpeed;

	Movement->MaxWalkSpeed = Speed;

	// 가속·제동은 룰셋의 "시간"에서 파생 — 속도가 변해도(아이템·갇힘·공중) 반응 감각이 일정하다.
	// 제동 마찰을 0 으로 분리해 감속도만 작용시킨다: 정지 시간 = 속도 ÷ 감속도 로 예측 가능해져
	// 원하는 칸에 정확히 멈춰 설 수 있다 (미끄러지면 폭탄을 엉뚱한 칸에 놓게 된다).
	const float Accel = Speed / FMath::Max(Rules->MoveAccelTime, KINDA_SMALL_NUMBER);
	const float Brake = Speed / FMath::Max(Rules->MoveBrakeTime, KINDA_SMALL_NUMBER);

	Movement->MaxAcceleration = Accel;
	Movement->bUseSeparateBrakingFriction = true;
	Movement->BrakingFriction = 0.f;
	Movement->BrakingDecelerationWalking = Brake;

	// 공중에도 같은 가감속을 적용 — 점프 중 조작이 지상과 다르면 착지 칸을 맞추기 어렵다.
	// AirControl 1.0 = "지상과 동일한 가속을 공중에서도" (엔진 기본 0.05 는 사실상 조작 불가).
	// 낙하 측면 마찰은 지상 BrakingFriction 과 같은 이유로 0 — 제동은 감속도 하나로만.
	Movement->AirControl = 1.f;
	Movement->FallingLateralFriction = 0.f;
	Movement->BrakingDecelerationFalling = Brake;
}

void ACA3DCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	const bool bNowFalling  = Movement->MovementMode == MOVE_Falling;
	const bool bWasFalling  = PrevMovementMode == MOVE_Falling;
	if (!bNowFalling && !bWasFalling)
	{
		return; // 공중 진입·착지와 무관한 전환 (수영·비행 등) — 건드리지 않는다
	}

	// 상한·가감속을 현재 모드 기준으로 다시 계산 (공중이면 JumpAirSpeedFactor 적용).
	RefreshMoveSpeed();

	if (bNowFalling)
	{
		// 도약 순간의 수평 관성을 공중 상한으로 깎는다. CMC 는 이미 상한을 넘은 속도를
		// 스스로 낮추지 않아서(가속 시 '현재 속도'를 상한으로 삼는다) 안 깎으면
		// 전력 질주 점프만 지상 속도 그대로 날아가 거리 계수가 무의미해진다.
		const FVector Lateral(Movement->Velocity.X, Movement->Velocity.Y, 0.f);
		if (Lateral.SizeSquared() > FMath::Square(Movement->MaxWalkSpeed))
		{
			const FVector Clamped = Lateral.GetClampedToMaxSize(Movement->MaxWalkSpeed);
			Movement->Velocity.X = Clamped.X;
			Movement->Velocity.Y = Clamped.Y;
		}
	}
}

FIntVector ACA3DCharacter::GetFootCell() const
{
	// ⚠️ 공중 발밑 셀 정의 미결정 (설계서 3.2) — 1차 정의: 캡슐 하단 위치가 속한 셀.
	if (!ensureMsgf(VoxelWorld, TEXT("ACA3DCharacter::GetFootCell: VoxelWorld 미캐시 — BeginPlay 이전 호출?")))
	{
		return FIntVector::ZeroValue;
	}
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return VoxelWorld->WorldToCell(GetActorLocation() - FVector(0.f, 0.f, HalfHeight));
}

void ACA3DCharacter::Move(const FVector2D& WorldAxis)
{
	// 생존 가드 (Task 27) — 컨트롤러가 아니라 **캐릭터**에서 막는다. 봇(Task 20)의 AIController 도
	// 결국 이 함수를 호출하므로, 차단을 여기 두면 플레이어와 봇이 같은 규칙을 자동으로 공유한다.
	// (컨트롤러에서 막으면 봇 컨트롤러에 같은 가드를 또 넣어야 하고, 하나 빠뜨리면 시체가 걸어다닌다.)
	// Trapped 는 막지 않는다 — 갇힘 중 미세 이동은 규칙이다 (GDD 2.3). 속도 제한은
	// RefreshMoveSpeed 가 TrappedMoveSpeed 로 이미 처리한다.
	if (Status && Status->LifeState == ELifeState::Dead)
	{
		return;
	}

	// 월드 평면 방향을 그대로 CMC 에 전달 — 커스텀 이동 코드 없음.
	// 대각 입력(크기 √2)은 CMC 가 가속 단계에서 정규화한다.
	AddMovementInput(FVector(WorldAxis.X, WorldAxis.Y, 0.f));
}

void ACA3DCharacter::DoJump()
{
	// Move 와 같은 이유로 캐릭터에서 차단 (봇 공용 경로). 다만 **Move 와 조건이 다르다** —
	// 갇힘(Trapped)은 이동은 허용하되(미세 이동, GDD 2.3) **점프는 막는다**: 물방울에
	// 갇힌 채로 뛰어오르면 갇힘의 의미가 사라진다 (2026-08-02 확정).
	if (Status && Status->LifeState != ELifeState::Alive)
	{
		return;
	}

	Jump(); // CMC 기본 점프 — 정점 높이는 TryApplyMovementTuning 이 "계수 × 셀" 로 설정
}

void ACA3DCharacter::ApplyDeathState()
{
	// ⚠️ 부활을 넣게 되면 되돌릴 것 (Task 27 문서) — 흩어지지 않게 여기 모아둔다:
	//   · 이 함수가 바꾸는 것: 캡슐 컬리전 · MovementMode · 액터 숨김(bHidden)
	//   · 이 함수 밖: UStatusComponent::LifeState (ServerKill),
	//                 ACA3DPlayerState::bAlive/FinalRank · ACA3DGameState::AliveCount (GameMode — Task 18)
	// 카메라는 죽은 자리에 그대로 둔다 (자유 관전·추적은 후속) — 그래서 폰을 살려두는 것만으로
	// 관전 시점 재배선이 0줄이다.
	//
	// 서버(ServerKill)·클라(OnRep_Life) 가 같은 이 함수를 통과한다. 아래 두 값을 클라에서도
	// 직접 적용해야 하는 이유는 "복제로 알아서 맞겨질 것 같지만 안 맞는" 두 가지 때문이다:
	//   · 컴포넌트 컬리전 설정은 애초에 복제되지 않는다 (UPrimitiveComponent 프로퍼티).
	//   · ACharacter::ReplicatedMovementMode 는 COND_SimulatedOnly — 정작 **죽은 본인**
	//     (AutonomousProxy)에게는 오지 않는다. 클라가 스스로 안 멈추면 자기 화면에서만
	//     시체가 바닥을 뚫고 떨어지고 카메라가 따라 내려간다.
	// 권위는 여전히 서버의 LifeState 하나뿐이다 — 클라는 그 복제 결과를 그대로 반영할 뿐이라
	// 불변식 5 를 어기지 않는다 (CMC 값을 서버·클라가 같은 경로로 계산하는 RefreshMoveSpeed 와 같은 관례).

	// GDD "유령 방해 없음" — 시체가 산 사람의 길을 막으면 안 된다.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// 컬리전을 끄면 바닥을 뚫고 떨어진다 — 카메라가 폰의 SpringArm 이라 시체가 낙하하면
	// 관전 시점도 같이 떨어진다. 그래서 이동 자체를 멈춘다.
	// MOVE_None 진입은 CMC 가 속도·점프 상태·누적 힘을 스스로 비운다(OnMovementModeChanged).
	// 우리 OnMovementModeChanged 오버라이드와는 충돌하지 않는다: 지상에서 죽으면
	// (공중 진입도 착지도 아니라) 조기 반환하고, 공중에서 죽으면 RefreshMoveSpeed 로
	// 상한만 다시 잡는다 — 관성 클램프는 `bNowFalling` 분기라 실행되지 않는다.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_None);
	}

	if (IsRunningDedicatedServer()) return; // 불변식 5 — 이하 시각 전용

	// 숨김은 **지연**한다 (Task 38) — 즉시 숨기면 사망 애님이 한 프레임도 안 보인다.
	// 이 지연 구간 동안 캐릭터는 보이는 채로 서 있고(위에서 컬리전·이동은 이미 껐다 —
	// 판정상으로는 완전한 시체다) 사망 애님이 재생된다.
	//
	// 숨김 시점의 1순위는 캐릭터별 DeathMontage 의 **실측 재생 길이**다 (Task 39) —
	// 8종의 Die 애님 길이가 제각각이라 전역 DeathHideDelay 하나로는 안 맞는다.
	// 재생하는 것이 곧 타이머의 출처: PlayAnimMontage 반환값(재생 속도 반영)이 그대로 숨김
	// 시점이라 표시와 타이머가 어긋날 수 없다. 몽타주는 슬롯으로 상태 머신(bDead → Die)을
	// 덮는다 — 같은 Die 애님이면 겹쳐도 동일 화면이고, 몽타주가 끝나는 순간 숨기므로
	// 상태 머신 포즈로 되돌아가는 프레임은 보이지 않는다.
	const UCA3DRuleSet* Rules = ResolveVisualRules(GetWorld());
	const ACA3DPlayerState* CA3DPlayerState = GetPlayerState<ACA3DPlayerState>();
	const int32 Index = CA3DPlayerState ? CA3DPlayerState->CharacterIndex : INDEX_NONE;
	if (Rules->Characters.IsValidIndex(Index) && Rules->Characters[Index].DeathMontage)
	{
		const float Duration = PlayAnimMontage(Rules->Characters[Index].DeathMontage);
		if (Duration > 0.f)
		{
			GetWorldTimerManager().SetTimer(DeathHideTimerHandle,
				FTimerDelegate::CreateUObject(this, &ACA3DCharacter::HideAfterDeath), Duration, false);
			return;
		}
		// Duration 0 = 재생 실패 (메시·AnimInstance 미준비 — 헤드리스 테스트 포함) → 아래 폴백.
	}

	// 폴백 — 몽타주 미지정·인덱스 미확정·재생 실패는 DeathHideDelay. 0 이하면 즉시 숨김
	// (기존 동작). 룰셋 미도착이어도 기다리지 않는다 — 시각 전용이라 CDO 폴백으로 충분하다
	// (ResolveVisualRules 관례).
	const float HideDelay = Rules->DeathHideDelay;
	if (HideDelay <= 0.f)
	{
		HideAfterDeath();
		return;
	}
	GetWorldTimerManager().SetTimer(DeathHideTimerHandle,
		FTimerDelegate::CreateUObject(this, &ACA3DCharacter::HideAfterDeath), HideDelay, false);
}

void ACA3DCharacter::HideAfterDeath()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (호출처가 이미 걸렀지만 관례 가드)

	// 액터 전체를 숨긴다 — 스켈레탈 메시(GetMesh)만 끄면 **BP 서브클래스가 추가한 메시
	// 컴포넌트가 그대로 남는다** (2026-07-30 PIE 에서 시체가 계속 보이던 원인).
	// 여기서 "무엇이 보이는 컴포넌트인지"를 코드가 알 필요가 없게 액터 단위로 끈다.
	// 카메라는 영향받지 않는다: bHidden 은 프리미티브 렌더링만 막고, 카메라 시점 계산
	// (CameraComponent::GetCameraView)과 ViewTarget 자격은 숨김 여부와 무관하다.
	SetActorHiddenInGame(true);
}

// ─── 니들 사용 (Task 23) ────────────────────────────────────────────────────

void ACA3DCharacter::TryUseNeedle()
{
	// 리슨 호스트·데디의 봇·자동화 테스트는 전부 여기서 끝난다 (RPC 없이 곧장 규칙으로).
	if (HasAuthority())
	{
		if (Status)
		{
			Status->ServerEscape();
		}
		return;
	}

	// 원격 클라: 서버도 거부할 요청이면 RPC 를 생략한다 (폭탄 설치의 로컬 검증과 같은 절약).
	// bHasNeedle·LifeState 는 복제되므로 이 판단의 재료가 클라에도 있다. 어긋나더라도
	// 손해는 없다 — 최종 판정은 서버의 ServerEscape 단독이고, 예측으로 바꾸는 상태가 없다.
	if (!Status || Status->LifeState != ELifeState::Trapped || !Status->bHasNeedle)
	{
		UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter %s: 니들 사용 조건 불충족 — RPC 생략"), *GetName());
		return;
	}

	ServerUseNeedle();
}

void ACA3DCharacter::ServerUseNeedle_Implementation()
{
	if (!HasAuthority()) return; // 불변식 5 (Server RPC 라 항상 서버지만 관례 가드)

	// 검증을 여기서 다시 하지 않는다 — ServerEscape 가 Trapped + 니들 보유를 이미 확인하고
	// 아니면 조용히 무시한다. 조건을 두 곳에 적으면 언젠가 한쪽만 바뀐다.
	if (Status)
	{
		Status->ServerEscape();
	}
}

// ─── 폭탄 설치 (Task 16 — 데이터 흐름 3.1) ──────────────────────────────────

bool ACA3DCharacter::TryGetBombPlacementCell(FIntVector& OutCell) const
{
	if (!VoxelWorld)
	{
		return false; // BeginPlay 이전·VoxelWorld 없는 맵 — 설치 불가
	}

	const FVoxelGrid& Grid = VoxelWorld->GetGrid();
	FIntVector Cell = GetFootCell();

	// 경계 접촉 보정: 캡슐 바닥이 발판 윗면에 정확히 걸치거나 살짝 파고들면 발밑 셀이
	// 그 솔리드 셀로 계산될 수 있다 — 한 칸 위에서 시작한다. 두 칸 이상 파묻힘은 비정상 — 거부.
	if (Grid.IsSolid(Cell))
	{
		Cell.Z += 1;
		if (Grid.IsSolid(Cell))
		{
			return false;
		}
	}

	// ⚠️ 공중 설치 규칙 (잠정 확정 — 사용자 결정): -Z 스캔으로 첫 솔리드 블록 바로 위의
	// Empty 셀을 찾는다. 지상에 서 있으면 바로 아래가 솔리드라 첫 반복에서 발밑 셀 그대로.
	// 그리드 밖(가장자리 구멍 위 공중 등)까지 내려가면 발판이 없다 — 설치 거부.
	while (true)
	{
		const FIntVector Below = Cell - FIntVector(0, 0, 1);
		if (Below.Z < 0)
		{
			return false; // 그리드 아래 밖 — 놓을 발판 없음
		}
		if (Grid.IsSolid(Below))
		{
			break; // 첫 솔리드 발견 — 그 바로 위 셀이 설치 위치
		}
		Cell = Below;
	}

	OutCell = Cell;
	return true;
}

void ACA3DCharacter::TryPlaceBombPredicted()
{
	FIntVector Cell;
	if (!TryGetBombPlacementCell(Cell))
	{
		// 발판 없는 공중(그리드 밖 하향 스캔) — 로컬에서 조용히 거부 (서버 왕복 불필요).
		UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter %s: 설치 셀 없음 — 요청 생략"), *GetName());
		return;
	}

	// 리슨 호스트(권한 있음): 지연이 없어 ABomb 이 같은 프레임에 스폰된다 — 예측 비주얼을
	// 만들면 진짜 폭탄과 겹치므로 예측을 생략하고 바로 서버 경로를 탄다.
	// (데디 서버도 이 분기로 빠진다 — APredictedBombVisual 의 데디 가드는 이 스폰 경로가 담당.)
	if (HasAuthority())
	{
		ServerPlaceBomb(Cell);
		return;
	}

	// 원격 클라: 로컬 검증 실패는 서버도 거부할 요청 — RPC 없이 조용히 반환 (서버 왕복 절약).
	if (!TryAcquirePredictedVisual(Cell))
	{
		return;
	}

	// 예측 즉시 재생 (Task 38) — 설치음(TryAcquirePredictedVisual)과 같은 근거로 왕복을 안
	// 기다린다. 서버가 거부해도 이미 재생된 몽타주는 "헛스윙"일 뿐이다 — 되돌릴 상태가 없다
	// (불변식 3: 예측은 시각 전용). 확정 복제(BombPlaceCounter)는 UpdateBombPlaceMontage 가
	// "로컬 조종 + 비권한" 분기로 걸러 두 번 휘두르지 않는다.
	PlayAttackMontage();

	ServerPlaceBomb(Cell);
}

bool ACA3DCharacter::TryAcquirePredictedVisual(const FIntVector& Cell)
{
	// 로컬 검증 — 서버 권위 검증(ServerPlaceBomb 4종)의 클라 예측판. 통과해도 최종 판정은
	// 서버 몫이고, 어긋나면 ClientRejectBomb 이 비주얼만 지운다 (불변식 3 — 상태 불일치 불가능).
	// "같은 셀 기존 폭탄" 은 서버 전용 레지스트리라 조회 불가 — 같은 셀 예측 중복으로 대신한다.
	const bool bAlive     = Status && Status->LifeState == ELifeState::Alive;
	const bool bCellEmpty = VoxelWorld && VoxelWorld->GetBlock(Cell) == EBlockType::Empty;

	bool bNoDupPrediction = true;
	for (const TObjectPtr<APredictedBombVisual>& Visual : PredictedBombVisuals)
	{
		if (IsValid(Visual) && Visual->Cell == Cell)
		{
			bNoDupPrediction = false; // 같은 셀 연타 — 이미 예측이 떠 서버 응답 대기 중
			break;
		}
	}

	// 개수 예측치: ActiveBombCount(소유자에게만 복제 — Task 39)에 아직 확정 안 된 예측
	// 비주얼 수를 더한다. 상한까지 깔린 상태의 재설치 시도가 여기서 걸려 몽타주·설치음·RPC 가
	// 전부 생략된다 (비복제였을 때는 원격 클라에서 항상 0 이라 상한인데도 통과 → 헛스윙).
	// 복제 지연으로 [폭탄 확정(ABomb 복제) ↔ 카운트 복제] 도착 순서가 어긋나는 한 프레임 동안
	// 잠깐 과차단될 수 있다 — 서버도 거부할 요청이므로 안전측, 무해하다.
	const bool bHasSlot = Status
		&& Status->ActiveBombCount + PredictedBombVisuals.Num() < Status->MaxBombCount;

	if (!bAlive || !bCellEmpty || !bNoDupPrediction || !bHasSlot)
	{
		UE_LOG(LogCA3D, Verbose,
			TEXT("ACA3DCharacter %s: 로컬 검증 실패 — 셀 (%d, %d, %d) [Alive %d / Empty %d / 예측중복없음 %d / 슬롯 %d] — RPC 생략"),
			*GetName(), Cell.X, Cell.Y, Cell.Z, bAlive, bCellEmpty, bNoDupPrediction, bHasSlot);
		return false;
	}

	// 풀에서 획득 — 클래스는 룰셋(BP_PredictedBombVisual), 미지정이면 C++ 기본 폴백.
	// 풀 미확보(정리 중 월드 등)면 비주얼만 생략하고 요청은 진행한다 — 시각 전용, 판정과 무관.
	if (UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr)
	{
		const UCA3DRuleSet* Rules = ResolveVisualRules(GetWorld());
		const TSubclassOf<APredictedBombVisual> VisualClass = Rules->PredictedBombVisualClass
			? Rules->PredictedBombVisualClass
			: TSubclassOf<APredictedBombVisual>(APredictedBombVisual::StaticClass());

		APredictedBombVisual* Visual = Pool->Acquire<APredictedBombVisual>(
			VisualClass, FTransform(VoxelWorld->CellToWorld(Cell)));
		if (Visual)
		{
			Visual->Cell = Cell; // 매칭 키 — 풀 재사용 잔존값을 매번 덮어쓴다 (오염 방지)
			PredictedBombVisuals.Add(Visual);

			// 설치음은 **여기서** 낸다 — 예측이 존재하는 목적이 왕복 지연을 감추는 것인데
			// 소리만 서버 확정까지 기다리면 "눌렀는데 반응이 늦다" 가 그대로 남는다.
			// 목록에 실제로 들어간 경우에만 낸다: 그래야 "예측 회수 == 이미 소리 냄" 이라는
			// 1:1 대응이 성립해 ABomb::BeginPlay 의 중복 방지가 정확해진다.
			CA3DFeedback::Play(GetWorld(), ECA3DCue::BombPlace, VoxelWorld->CellToWorld(Cell));
		}
	}
	return true;
}

bool ACA3DCharacter::ReleasePredictedVisualAt(const FIntVector& Cell)
{
	UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;

	bool bReleasedAny = false;
	for (int32 Index = PredictedBombVisuals.Num() - 1; Index >= 0; --Index)
	{
		APredictedBombVisual* Visual = PredictedBombVisuals[Index];
		if (!IsValid(Visual))
		{
			PredictedBombVisuals.RemoveAt(Index); // 레벨 정리 등으로 파괴된 항목 청소
			continue;
		}
		if (Visual->Cell != Cell)
		{
			continue;
		}
		if (Pool)
		{
			Pool->Release(Visual);
		}
		PredictedBombVisuals.RemoveAt(Index);
		bReleasedAny = true;
	}
	return bReleasedAny;
}

void ACA3DCharacter::ServerPlaceBomb_Implementation(FIntVector Cell)
{
	if (!HasAuthority()) return; // 불변식 5 (Server RPC 라 항상 서버지만 관례 가드)

	UWorld* World = GetWorld();
	UExplosionSubsystem* Explosion = World ? World->GetSubsystem<UExplosionSubsystem>() : nullptr;

	// 권위 검증 4종 (명세): 개수·셀 Empty·기존 폭탄 없음·Alive. 하나라도 실패 → 거부 통보.
	const bool bAlive     = Status && Status->LifeState == ELifeState::Alive;
	const bool bHasSlot   = Status && Status->ActiveBombCount < Status->MaxBombCount;
	const bool bCellEmpty = VoxelWorld && VoxelWorld->GetBlock(Cell) == EBlockType::Empty;
	const bool bNoBomb    = Explosion && !Explosion->FindBombAt(Cell);

	if (!VoxelWorld || !Explosion || !bAlive || !bHasSlot || !bCellEmpty || !bNoBomb)
	{
		UE_LOG(LogCA3D, Log,
			TEXT("ACA3DCharacter %s: 폭탄 설치 거부 — 셀 (%d, %d, %d) [Alive %d / 슬롯 %d / Empty %d / 폭탄없음 %d]"),
			*GetName(), Cell.X, Cell.Y, Cell.Z, bAlive, bHasSlot, bCellEmpty, bNoBomb);
		ClientRejectBomb(Cell);
		return;
	}

	// Cell·Range 를 첫 복제 전에 확정해야 클라 BeginPlay 프리뷰가 올바른 값으로 돈다 —
	// SpawnActorDeferred → ServerArm → FinishSpawning 순서 (Bomb.h 주석).
	const FTransform SpawnTransform(VoxelWorld->CellToWorld(Cell));
	ABomb* Bomb = World->SpawnActorDeferred<ABomb>(
		BombClass ? *BombClass : ABomb::StaticClass(), SpawnTransform, this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Bomb)
	{
		ClientRejectBomb(Cell);
		return;
	}

	Bomb->ServerArm(this, Status->BombRange, Cell); // ActiveBombCount++ 는 ServerArm 안 (슬롯 점유/반환 대칭)
	Bomb->FinishSpawning(SpawnTransform);

	// 설치 확정 신호 (Task 38) — **스폰 성공이 확정된 여기서만** ++. 거부 경로에서는 절대
	// 안 올린다 (거부인데 올리면 관전자 화면에서 헛스윙이 보인다 — 표시와 실제의 어긋남).
	++BombPlaceCounter; // 랩어라운드 무해 — 변화 감지 용도 (헤더 주석)
}

void ACA3DCharacter::ClientRejectBomb_Implementation(FIntVector Cell)
{
	// 서버 거부 — 같은 셀의 예측 비주얼만 반납하면 끝 (불변식 3: 예측에 타이머·상태가 없어
	// 되돌릴 것이 이펙트뿐이다).
	UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter %s: 서버가 폭탄 설치 거부 — 셀 (%d, %d, %d) 예측 비주얼 반납"),
		*GetName(), Cell.X, Cell.Y, Cell.Z);
	ReleasePredictedVisualAt(Cell);
}
