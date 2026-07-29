#include "Gameplay/Character/CA3DCharacter.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

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

	// 카메라 붐: 컨트롤러의 고정 pitch + 스냅 yaw(ControlRotation)를 그대로 사용.
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->TargetArmLength = 1200.f; // ⚠️ 임시 — CameraDistanceCells 12 × 100. 튜닝이 덮어씀
	// bDoCollisionTest 기본 true 유지 — 벽 파고들기 방지 (디더 페이드 가림 처리는 3주차).

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom);
	FollowCamera->bUsePawnControlRotation = false; // 붐이 회전을 다 처리한다

	// 스탯·생존 상태 (Task 12) — 봇과 플레이어가 완전히 같은 코드 경로를 탄다.
	Status = CreateDefaultSubobject<UStatusComponent>(TEXT("Status"));

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

	if (Status && Status->LifeState == ELifeState::Trapped)
	{
		// 갇힘: 미세 이동만 (GDD 2.3). 튜닝 미도착 시 CDO 폴백 (TryApplyMovementTuning 관례).
		const UCA3DRuleSet* Rules = CachedRules ? CachedRules.Get() : GetDefault<UCA3DRuleSet>();
		Movement->MaxWalkSpeed = Rules->TrappedMoveSpeed;
		return;
	}

	Movement->MaxWalkSpeed = BaseWalkSpeed * (Status ? Status->MoveSpeedMul : 1.f);
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
	// 월드 평면 방향을 그대로 CMC 에 전달 — 커스텀 이동 코드 없음.
	// 대각 입력(크기 √2)은 CMC 가 가속 단계에서 정규화한다.
	AddMovementInput(FVector(WorldAxis.X, WorldAxis.Y, 0.f));
}

void ACA3DCharacter::DoJump()
{
	Jump(); // CMC 기본 점프 — 정점 높이는 TryApplyMovementTuning 이 "계수 × 셀" 로 설정
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
	// TODO(Task 17): 같은 셀의 APredictedBombVisual 제거 — 지금은 예측 비주얼이 없어 로그만.
	UE_LOG(LogCA3D, Verbose, TEXT("ACA3DCharacter %s: 서버가 폭탄 설치 거부 — 셀 (%d, %d, %d)"),
		*GetName(), Cell.X, Cell.Y, Cell.Z);
}
