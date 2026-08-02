#include "Gameplay/Character/CA3DCharacter.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Bomb/PredictedBombVisual.h"
#include "Core/PoolSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Components/CapsuleComponent.h"
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

	// 45도 스냅 카메라(Task 11)와의 역할 분담: 카메라 yaw 는 컨트롤러 ControlRotation 이
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

	if (!HasAuthority()) return; // 불변식 5 — 낙사 판정은 상태 변경으로 이어지므로 서버 전용

	// 낙사 → ServerKill(Fall). Dead 면 스킵 (중복 호출 방지). 갇힌 채 추락하는 상황은
	// 막지 않는다 (미결정 정책) — Trapped 여도 그대로 낙사 처리된다.
	if (Status && Status->LifeState != ELifeState::Dead && GetActorLocation().Z < KillZ)
	{
		UE_LOG(LogCA3D, Log, TEXT("ACA3DCharacter %s: KillZ(%.0f) 아래로 낙하 — 낙사 처리"), *GetName(), KillZ);
		Status->ServerKill(EDeathCause::Fall);
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

	// 액터 전체를 숨긴다 — 스켈레탈 메시(GetMesh)만 끄면 **BP 서브클래스가 추가한 메시
	// 컴포넌트가 그대로 남는다** (2026-07-30 PIE 에서 시체가 계속 보이던 원인).
	// 여기서 "무엇이 보이는 컴포넌트인지"를 코드가 알 필요가 없게 액터 단위로 끈다.
	// 카메라는 영향받지 않는다: bHidden 은 프리미티브 렌더링만 막고, 카메라 시점 계산
	// (CameraComponent::GetCameraView)과 ViewTarget 자격은 숨김 여부와 무관하다.
	SetActorHiddenInGame(true);
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

	// 개수 예측치: ActiveBombCount 는 서버 전용(원격 클라에선 항상 0)이라 아직 확정 안 된
	// 예측 비주얼 수를 더해 연타 초과를 로컬에서 거른다. 확정 후 어긋나면 서버가 거부한다.
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
		}
	}
	return true;
}

void ACA3DCharacter::ReleasePredictedVisualAt(const FIntVector& Cell)
{
	UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;

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
	}
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
}

void ACA3DCharacter::ClientRejectBomb_Implementation(FIntVector Cell)
{
	// 서버 거부 — 같은 셀의 예측 비주얼만 반납하면 끝 (불변식 3: 예측에 타이머·상태가 없어
	// 되돌릴 것이 이펙트뿐이다).
	UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter %s: 서버가 폭탄 설치 거부 — 셀 (%d, %d, %d) 예측 비주얼 반납"),
		*GetName(), Cell.X, Cell.Y, Cell.Z);
	ReleasePredictedVisualAt(Cell);
}
