#include "AI/BotController.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Bomb/ExplosionTypes.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelMovement.h" // 이동 규칙(설 수 있는 칸·한 걸음)의 단일 출처 — 검증기와 공유한다
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"    // AI→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h"  // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Framework/CA3DPlayerState.h"
#include "Core/CameraYawSnap.h" // 90도 스냅 공식의 단일 출처 — 봇도 같은 변환을 쓴다
#include "GameFramework/CharacterMovementComponent.h"
#include "Algo/Reverse.h"
#include "EngineUtils.h"

namespace
{
	// 봇의 물리 제약 — 격자 기하(VoxelMove 기본값)에 **머리 공간 하나만** 더 얹는다.
	//
	// 캡슐 높이(176)가 셀(100)보다 커서 1칸 높이 틈에는 몸이 안 들어간다. 검증기는 이걸
	// 요구하지 않는데, 맵 생성 시점에는 오버행이 없어 차이가 나지 않기 때문이다
	// (VoxelMovement.h FMoveCaps 주석). 1칸 틈은 폭발이 기둥 중간을 뚫은 런타임에만 생긴다.
	//
	// **이 함수 밖에서 이동 규칙을 다시 쓰지 말 것.** 평면 방향 배열·오르기 높이·낙하 규칙은
	// 전부 VoxelMove 에 있고, 봇은 그것을 호출만 한다. 예전에는 여기에 dz ∈ {0,+1,-1} 배열이
	// 따로 있었고, 그래서 검증기가 허용하는 **2칸 이상 낙하를 봇만 계획하지 못했다** —
	// 산 위에서 못 내려오거나 빙 돌아가는 증상의 원인이 그것이었다.
	VoxelMove::FMoveCaps BotMoveCaps()
	{
		VoxelMove::FMoveCaps Caps;
		Caps.bRequireHeadroom = true;
		return Caps;
	}

	// 좌표 사전순 — 액터 이터레이션 순서를 지우는 용도 (GetActiveBombCellsSorted 와 같은 규칙).
	bool BotCellLess(const FIntVector& A, const FIntVector& B)
	{
		if (A.X != B.X) return A.X < B.X;
		if (A.Y != B.Y) return A.Y < B.Y;
		return A.Z < B.Z;
	}
}

ABotController::ABotController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// **이 Task 의 핵심**: 봇에게도 ACA3DPlayerState 를 붙인다.
	// 그래야 Task 18 의 승패 판정(GameState->PlayerArray 순회 · 공동 등수 · 최후 1인 종료)이
	// 봇을 사람과 완전히 똑같이 취급한다 — 봇으로 인원을 채워 한 판을 완주시키면
	// 사망 복제·종료 판정이 "덤으로" 검증된다 (헤드리스 데디에서 사람 8명을 모을 수는 없다).
	// AAIController::PostInitializeComponents 가 이 플래그를 보고 InitPlayerState 를 부르므로
	// 반드시 생성자에서 세워야 한다.
	bWantsPlayerState = true;
}

// ─── 컨텍스트 해석 ───────────────────────────────────────────────────────────

ACA3DCharacter* ABotController::GetBotCharacter() const
{
	return Cast<ACA3DCharacter>(GetPawn());
}

AVoxelWorld* ABotController::ResolveVoxelWorld() const
{
	if (!IsValid(CachedVoxelWorld))
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약 — 첫 번째만 사용
		}
	}
	return CachedVoxelWorld;
}

const UCA3DRuleSet* ABotController::ResolveRules() const
{
	if (const UWorld* World = GetWorld())
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

// ─── 판정 (전부 Propagate 재사용 — 불변식 2) ────────────────────────────────

void ABotController::GatherDangerCells(TArray<FIntVector>& OutCells) const
{
	OutCells.Reset();

	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	UWorld* World = GetWorld();
	UExplosionSubsystem* Explosion = World ? World->GetSubsystem<UExplosionSubsystem>() : nullptr;
	if (!VoxelWorld || !Explosion)
	{
		return;
	}

	// 정렬된 폭탄 셀 목록이 곧 순회 순서다 — 서버 레지스트리의 등록 순서(로컬)를 쓰지 않는다.
	const TArray<FIntVector> BombCells = Explosion->GetActiveBombCellsSorted();
	if (BombCells.Num() == 0)
	{
		return;
	}

	const bool bFloorDestructible = ResolveRules()->bFloorDestructible;
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	for (const FIntVector& BombCell : BombCells)
	{
		// 범위는 **폭탄이 들고 있는 값**을 쓴다 — 설치자의 현재 BombRange 가 아니다.
		// 설치 후 포션을 먹으면 둘이 달라지고, 그때 봇의 위험 구역이 실폭발보다 좁아진다.
		const ABomb* Bomb = Explosion->FindBombAt(BombCell);
		if (!Bomb)
		{
			continue; // 정렬 목록과 레지스트리 사이에 파괴된 폭탄 — 무시
		}

		const FExplosionResult Result = UExplosionSubsystem::Propagate(
			Grid, BombCell, Bomb->GetRange(), bFloorDestructible, BombCells);

		for (const FIntVector& Water : Result.WaterCells)
		{
			OutCells.AddUnique(Water);
		}
	}
}

bool ABotController::IsCellDangerous(const FIntVector& Cell) const
{
	TArray<FIntVector> DangerCells;
	GatherDangerCells(DangerCells);
	return DangerCells.Contains(Cell);
}

void ABotController::GatherEnemyFootCells(TArray<FIntVector>& OutCells) const
{
	OutCells.Reset();

	UWorld* World = GetWorld();
	const APawn* Self = GetPawn();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ACA3DCharacter> It(World); It; ++It)
	{
		ACA3DCharacter* Other = *It;
		if (!IsValid(Other) || Other == Self)
		{
			continue;
		}
		const UStatusComponent* OtherStatus = Other->GetStatus();
		if (!OtherStatus || OtherStatus->LifeState == ELifeState::Dead)
		{
			continue; // 시체는 목표가 아니다 (GDD "유령 방해 없음" 과 같은 정신)
		}
		OutCells.AddUnique(Other->GetFootCell());
	}

	OutCells.Sort(BotCellLess); // 액터 순서 제거 — 동점(같은 거리) 상대 선택이 흔들리지 않게
}

bool ABotController::IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell) const
{
	// 판정 본체는 VoxelMove — FMapValidator::IsStandable 과 **같은 함수**다 (머리 공간만 추가).
	return VoxelMove::IsStandable(Grid, Cell, BotMoveCaps());
}

bool ABotController::ShouldPlaceBombAt(const FIntVector& Cell) const
{
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	const ACA3DCharacter* BotChar = GetBotCharacter(); // 이름이 Character 면 AController::Character 를 가린다
	const UStatusComponent* Status = BotChar ? BotChar->GetStatus() : nullptr;
	UWorld* World = GetWorld();
	UExplosionSubsystem* Explosion = World ? World->GetSubsystem<UExplosionSubsystem>() : nullptr;
	if (!VoxelWorld || !Status || !Explosion)
	{
		return false;
	}

	const FVoxelGrid& Grid = VoxelWorld->GetGrid();
	const UCA3DRuleSet* Rules = ResolveRules();

	// 놓을 자리 자체가 이미 폭탄이면 서버가 거부한다 — 물어볼 것도 없다.
	if (Explosion->FindBombAt(Cell) || Grid.Get(Cell) != EBlockType::Empty)
	{
		return false;
	}

	// ── 이득 판단 — 실제로 놓았을 때와 **같은 함수**로 계산한다 (불변식 2) ──
	const TArray<FIntVector> BombCells = Explosion->GetActiveBombCellsSorted();
	const FExplosionResult Result = UExplosionSubsystem::Propagate(
		Grid, Cell, Status->BombRange, Rules->bFloorDestructible, BombCells);

	bool bWorthIt = Result.BrokenCells.Num() > 0; // 길을 뚫는 것 자체가 이득 (GDD — 파괴로 공간을 연다)
	if (!bWorthIt)
	{
		TArray<FIntVector> EnemyCells;
		GatherEnemyFootCells(EnemyCells);
		for (const FIntVector& Enemy : EnemyCells)
		{
			if (Result.WaterCells.Contains(Enemy))
			{
				bWorthIt = true;
				break;
			}
		}
	}
	if (!bWorthIt)
	{
		return false;
	}

	// ── 탈출로 확인 — 이걸 빼면 봇이 자폭만 반복한다 ──
	// 놓을 이유는 거의 항상 있으므로(주변에 부술 블록이 하나만 있어도 참) 판단의 실질은 여기다.
	// 통행 판정: 기존 폭탄의 위험 구역은 피해서 지나간다(퓨즈가 언제 끝날지 모른다).
	// 목표: 기존 위험도 아니고 **방금 놓을 폭탄의 물줄기도 아닌** 칸.
	TArray<FIntVector> DangerCells;
	GatherDangerCells(DangerCells);

	auto IsPassable = [this, &Grid, &DangerCells](const FIntVector& Each)
	{
		return IsStandable(Grid, Each) && !DangerCells.Contains(Each);
	};
	auto IsSafeGoal = [&Result](const FIntVector& Each)
	{
		return !Result.WaterCells.Contains(Each);
	};

	TArray<FIntVector> EscapePath;
	// 시작 셀은 정의상 물줄기 안이라 목표가 될 수 없다 — 반드시 한 칸 이상 나가는 경로가 나온다.
	return RunBFS(Cell, IsPassable, IsSafeGoal, EscapePath);
}

// ─── 경로 탐색 ──────────────────────────────────────────────────────────────

bool ABotController::RunBFS(
	const FIntVector& Start,
	TFunctionRef<bool(const FIntVector&)> IsPassable,
	TFunctionRef<bool(const FIntVector&)> IsGoal,
	TArray<FIntVector>& OutPath,
	TArray<FIntVector>* OutVisited) const
{
	OutPath.Reset();
	if (OutVisited)
	{
		OutVisited->Reset();
	}

	// 이웃 확장을 VoxelMove 에 맡기므로 여기서도 그리드가 필요하다 (캐시 조회라 사실상 공짜).
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld)
	{
		return false;
	}
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	const int32 MaxNodes = FMath::Max(ResolveRules()->BotMaxPathNodes, 1);

	// 방문 순서를 그대로 담는 배열 + 부모 인덱스. TMap 은 **조회 전용**이다 —
	// 순회하지 않으므로 해시 순서가 결과에 개입할 여지가 없다 (불변식 4 준용).
	TArray<FIntVector> Nodes;
	TArray<int32> Parents;
	TMap<FIntVector, int32> NodeIndex;
	Nodes.Reserve(MaxNodes);
	Parents.Reserve(MaxNodes);
	NodeIndex.Reserve(MaxNodes);

	Nodes.Add(Start);
	Parents.Add(INDEX_NONE);
	NodeIndex.Add(Start, 0);

	int32 GoalIndex = IsGoal(Start) ? 0 : INDEX_NONE;

	// 이웃 확장은 **검증기와 같은 함수**로 한다 (VoxelMove). 방향당 착지 칸 하나씩 최대 4개이고,
	// 그 착지 칸은 "1칸 오르기 또는 **높이 제한 없는 낙하** 후 처음 만난 발판"이다 —
	// 여기서 봇이 산 위에서 협곡 바닥으로 한 걸음에 뛰어내리는 지름길이 나온다.
	TArray<FIntVector> Neighbors;
	for (int32 Head = 0; Head < Nodes.Num() && GoalIndex == INDEX_NONE; ++Head)
	{
		const FIntVector Current = Nodes[Head];
		VoxelMove::GatherReachableNeighbors(Grid, Current, Neighbors, BotMoveCaps());

		for (const FIntVector& Next : Neighbors)
		{
			// VoxelMove 가 이미 "설 수 있는가"를 봤으므로 IsPassable 은 그 위의 조건
			// (위험 구역 회피 등)만 얹는다 — 판정이 두 번 도는 것은 캐시된 그리드 조회라 싸다.
			if (NodeIndex.Contains(Next) || !IsPassable(Next))
			{
				continue;
			}

			Nodes.Add(Next);
			Parents.Add(Head);
			NodeIndex.Add(Next, Nodes.Num() - 1);

			if (IsGoal(Next))
			{
				GoalIndex = Nodes.Num() - 1;
				break;
			}
			if (Nodes.Num() >= MaxNodes)
			{
				break; // 탐색 상한 — 서버 스파이크 방지 (룰셋 BotMaxPathNodes)
			}
		}
		if (Nodes.Num() >= MaxNodes)
		{
			break;
		}
	}

	if (OutVisited)
	{
		*OutVisited = Nodes;
	}

	if (GoalIndex == INDEX_NONE)
	{
		return false;
	}

	// 부모를 거슬러 올라가 뒤집는다 — 결과는 Start .. Goal.
	for (int32 Index = GoalIndex; Index != INDEX_NONE; Index = Parents[Index])
	{
		OutPath.Add(Nodes[Index]);
	}
	Algo::Reverse(OutPath);
	return true;
}

TArray<FIntVector> ABotController::FindPath(const FIntVector& From, const FIntVector& To) const
{
	TArray<FIntVector> Path;

	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld)
	{
		return Path;
	}
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	auto IsPassable = [this, &Grid](const FIntVector& Each) { return IsStandable(Grid, Each); };
	auto IsGoal     = [&To](const FIntVector& Each) { return Each == To; };

	RunBFS(From, IsPassable, IsGoal, Path);
	return Path;
}

// ─── FSM ────────────────────────────────────────────────────────────────────

void ABotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority()) return; // 불변식 5 — 봇은 상태를 바꾸는 입력을 만든다. 서버 전용.

	ACA3DCharacter* BotChar = GetBotCharacter(); // 이름이 Character 면 AController::Character 를 가린다
	UStatusComponent* Status = BotChar ? BotChar->GetStatus() : nullptr;
	if (!BotChar || !Status)
	{
		return;
	}

	// ── 관전 카메라 yaw (봇 판단과 무관한 **표현** 값) ──
	//
	// 봇에게는 "보고 있는 시점"이 없다 — 사람은 Q/E 로 4방향 중 하나를 고르지만 봇은 카메라를
	// 갖지 않는다. 그래서 **이동 방향**을 대신 쓴다: 캐릭터가 bOrientRotationToMovement 라
	// 액터 회전이 곧 봇이 가고 있는 쪽이고, 관전자 입장에서 "가는 쪽이 화면 위"가 가장 덜
	// 어색하다. 멈춰 있을 때도 마지막 방향이 남아 있어(속도와 달리) 각이 떨리지 않는다.
	//
	// 90도로 스냅하는 이유는 이 게임 카메라가 4방향 고정이기 때문이다 — 스냅하지 않으면
	// 봇을 관전할 때만 격자가 비스듬히 기울어 칸 세기가 무너진다. 히스테리시스(룰셋)는
	// 경계에 걸친 방향이 미세하게 흔들릴 때 카메라가 한 칸씩 왕복하는 것을 막는다.
	//
	// 데디 가드를 걸지 **않는다** — 이 값은 복제되어 **클라의** 관전 카메라 각이 된다
	// (복제되는 값을 서버가 쓰는 것은 "시각"이 아니다).
	//
	// 예전에는 여기서 ControlRotation 의 pitch 를 CameraPitchDeg 로 덮어썼다. 이제 각을 만드는
	// 곳이 ACA3DCharacter::GetViewRotation() 한 곳이라 지웠다 — 남겨두면 같은 값을 두 곳이
	// 정한다. 지워도 봇의 이동·판단·외형은 그대로다: 캐릭터가 bUseControllerRotationYaw =
	// bUseControllerRotationPitch = bUseControllerRotationRoll = false 라 AAIController 가
	// 부르는 APawn::FaceRotation 이 아무 일도 하지 않는다.
	if (ACA3DPlayerState* BotState = GetPlayerState<ACA3DPlayerState>())
	{
		BotState->CamYawIndex = CameraYawSnap::YawDegToIndexWithHysteresis(
			static_cast<float>(BotChar->GetActorRotation().Yaw), // FRotator 는 double 기반
			BotState->CamYawIndex,
			ResolveRules()->SpectateBotCamYawHysteresisDeg);
	}

	// 사망 — 아무 입력도 만들지 않는다. 캐릭터(Move/DoJump)에도 생존 가드가 있지만
	// 여기서 끊어야 시체가 매 틱 BFS 를 돌리는 낭비가 없다 (관전 중 8명분이면 무시 못 한다).
	if (Status->LifeState == ELifeState::Dead)
	{
		PathCells.Reset();
		PathIndex = 0;
		bPlanFailed = false;
		return;
	}

	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld || !VoxelWorld->IsGridInitialized())
	{
		return; // 맵 생성 전 — 판정할 지형이 없다
	}

	// 갇힘 — 니들이 있으면 탈출을 시도한다. **플레이어와 같은 진입점**(TryUseNeedle)이라
	// 니들 소모·타이머 해제·속도 복원이 사람과 완전히 같은 규칙을 탄다. 봇은 서버에서 도므로
	// TryUseNeedle 이 RPC 없이 곧장 ServerEscape 로 들어간다 (Task 23).
	// 니들이 없으면 그냥 갇힌 채로 익사한다 (봇 전용 구제책 없음 — 그래야 검증이 된다).
	// 조건 검사를 여기 남겨 둔 이유는 낭비를 줄이기 위한 것뿐 — 판정 자체는 ServerEscape 소관.
	if (Status->LifeState == ELifeState::Trapped && Status->bHasNeedle)
	{
		BotChar->TryUseNeedle();
	}

	if (!bRandomSeeded)
	{
		// 봇마다 다른 난수열 — 참가 색 인덱스를 쓰면 같은 매치 구성에서 재현도 된다.
		const ACA3DPlayerState* BotState = GetPlayerState<ACA3DPlayerState>();
		RandomStream.Initialize(BotState ? BotState->ColorIndex + 1 : 1);
		bRandomSeeded = true;
	}

	const UCA3DRuleSet* Rules = ResolveRules();
	const FIntVector FootCell = BotChar->GetFootCell();

	TArray<FIntVector> DangerCells;
	GatherDangerCells(DangerCells);
	const bool bDanger = DangerCells.Contains(FootCell);

	TimeSinceReplan += DeltaSeconds;
	TimeSinceBombAttempt += DeltaSeconds;

	// 재계획 조건 세 가지. 세 번째(주기)가 없으면 막힌 봇이 영원히 벽을 민다.
	//   ① 위험 상태가 바뀌었다 — 즉시(우선순위 최상위라 다음 주기를 기다리면 늦다)
	//   ② 경로를 다 썼다 — 단 **직전 계획이 실패했다면 제외**(bPlanFailed 주석 참조)
	//   ③ 재계획 주기 경과 — **매 틱 BFS 를 돌리지 않기 위한 장치**가 이것이다
	const bool bStateMismatch  = bDanger != (State == EBotState::Evade);
	const bool bPathExhausted  = !PathCells.IsValidIndex(PathIndex);
	const bool bReplanDue      = TimeSinceReplan >= Rules->BotReplanInterval;

	if (bStateMismatch || bReplanDue || (bPathExhausted && !bPlanFailed))
	{
		Replan(FootCell, DangerCells);
		bPlanFailed = (PathCells.Num() == 0);
		TimeSinceReplan = 0.f;

#if !UE_BUILD_SHIPPING
		// 봇 진단 — Verbose 라 기본은 꺼져 있다 (`-LogCmds="LogCA3D Verbose"` 로 켠다).
		// 재계획 시점에만 찍는다. 이 네 값(상태·발밑·위험·경로)이면 "왜 안 움직이나"가
		// 대부분 판별된다: 경로 0 이면 BFS 가 실패한 것이고, 발밑 셀이 이상하면 GetFootCell 문제다.
		const UCharacterMovementComponent* DiagMove = BotChar->GetCharacterMovement();
		UE_LOG(LogCA3D, Verbose,
			TEXT("ABotController %s: 재계획 — 상태 %d / 발밑 (%d,%d,%d) / 위험 %d칸 / 경로 %d칸 / 지상 %d / 모드 %d / Z %.1f / vZ %.1f"),
			*GetName(), static_cast<int32>(State), FootCell.X, FootCell.Y, FootCell.Z,
			DangerCells.Num(), PathCells.Num(),
			(DiagMove && DiagMove->IsMovingOnGround()) ? 1 : 0,
			DiagMove ? static_cast<int32>(DiagMove->MovementMode.GetValue()) : -1,
			BotChar->GetActorLocation().Z,
			DiagMove ? DiagMove->Velocity.Z : 0.f);
#endif
	}

	FollowPath(FootCell);
}

void ABotController::Replan(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells)
{
	PathCells.Reset();
	PathIndex = 0;

	ACA3DCharacter* BotChar = GetBotCharacter(); // 이름이 Character 면 AController::Character 를 가린다
	UStatusComponent* Status = BotChar ? BotChar->GetStatus() : nullptr;
	if (!BotChar || !Status)
	{
		return;
	}

	// ── ① Evade — 발밑이 위험하면 다른 판단을 아예 하지 않는다 (전이 우선순위 최상위) ──
	if (DangerCells.Contains(FootCell))
	{
		State = EBotState::Evade;
		PlanEscape(FootCell, DangerCells);
		return;
	}

	// ── ② 설치 — 안전할 때만. 지상에 있을 때만 시도한다 ──
	// 공중 설치는 캐릭터가 -Z 스캔으로 자기 셀을 다시 고르므로(TryGetBombPlacementCell)
	// ShouldPlaceBombAt 이 검사한 셀과 실제 설치 셀이 달라질 수 있다 —
	// 그러면 "탈출로 확인한 자리"와 "폭탄이 놓인 자리"가 어긋나 자폭한다.
	const UCA3DRuleSet* Rules = ResolveRules();
	const UCharacterMovementComponent* Movement = BotChar->GetCharacterMovement();
	const bool bOnGround = Movement && Movement->IsMovingOnGround();

	if (bOnGround
		&& Status->LifeState == ELifeState::Alive
		&& Status->ActiveBombCount < Status->MaxBombCount
		&& TimeSinceBombAttempt >= Rules->BotBombCooldown
		&& ShouldPlaceBombAt(FootCell))
	{
		BotChar->TryPlaceBombPredicted(); // 플레이어와 같은 진입점 (권한이 있으므로 바로 서버 경로)
		TimeSinceBombAttempt = 0.f;

		// 방금 놓은 폭탄이 곧 새 위험원이다 — 위험을 **다시 모아** 탈출 경로를 잡는다.
		// (설치 전 DangerCells 로 계획하면 자기 폭탄 위에 그대로 서 있게 된다.)
		State = EBotState::Evade;
		TArray<FIntVector> NewDangerCells;
		GatherDangerCells(NewDangerCells);
		PlanEscape(FootCell, NewDangerCells);
		return;
	}

	// ── ③ Attack — 도달 가능한 상대가 있으면 접근 ──
	TArray<FIntVector> EnemyCells;
	GatherEnemyFootCells(EnemyCells);
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (EnemyCells.Num() > 0 && VoxelWorld)
	{
		const FVoxelGrid& Grid = VoxelWorld->GetGrid();

		// 상대마다 BFS 를 돌리지 않는다 — 목표 집합을 한 번의 BFS 로 찾으면
		// 그 결과가 곧 "가장 가까운(BFS 단계 수가 가장 적은) 상대" 다.
		auto IsPassable = [this, &Grid, &DangerCells](const FIntVector& Each)
		{
			return IsStandable(Grid, Each) && !DangerCells.Contains(Each);
		};
		auto IsGoal = [&EnemyCells](const FIntVector& Each) { return EnemyCells.Contains(Each); };

		TArray<FIntVector> Path;
		if (RunBFS(FootCell, IsPassable, IsGoal, Path) && Path.Num() > 1)
		{
			State = EBotState::Attack;
			PathCells = MoveTemp(Path);
			PathIndex = 1; // 0 은 현재 서 있는 칸
			return;
		}
	}

	// ── ④ Wander — 갈 곳이 없으면 돌아다닌다 ──
	State = EBotState::Wander;
	PlanWander(FootCell, DangerCells);
}

void ABotController::PlanEscape(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells)
{
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld)
	{
		return;
	}
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	// 통행은 지형만 본다 — 이미 위험 구역 안이라 "위험한 칸은 못 지나간다"로 잡으면
	// 자기가 서 있는 칸에서 한 발도 못 뗀다. 목표만 "위험하지 않은 칸"으로 둔다.
	auto IsPassable = [this, &Grid](const FIntVector& Each) { return IsStandable(Grid, Each); };
	auto IsGoal     = [&DangerCells](const FIntVector& Each) { return !DangerCells.Contains(Each); };

	TArray<FIntVector> Path;
	if (RunBFS(FootCell, IsPassable, IsGoal, Path) && Path.Num() > 1)
	{
		PathCells = MoveTemp(Path);
		PathIndex = 1;
		return;
	}

	// 탈출로 없음 — 경로를 비워 둔다. 다음 재계획에서 지형이 바뀌었을 수 있다
	// (다른 폭발이 벽을 뚫으면 길이 생긴다). 봇 전용 탈출 수단은 만들지 않는다.
	UE_LOG(LogCA3D, Verbose, TEXT("ABotController %s: 탈출로 없음 — 셀 (%d, %d, %d)"),
		*GetName(), FootCell.X, FootCell.Y, FootCell.Z);
}

void ABotController::PlanWander(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells)
{
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld)
	{
		return;
	}
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	// 도달 가능한 안전 셀을 모아(목표 없는 BFS = 플러드 필) 그중 하나를 추첨한다.
	// "무작위 좌표를 뽑아 경로를 찾는" 방식보다 확실하다 — 뽑은 칸이 벽 속이면 헛돈다.
	auto IsPassable = [this, &Grid, &DangerCells](const FIntVector& Each)
	{
		return IsStandable(Grid, Each) && !DangerCells.Contains(Each);
	};
	auto NeverGoal = [](const FIntVector&) { return false; };

	TArray<FIntVector> Path;
	TArray<FIntVector> Visited;
	RunBFS(FootCell, IsPassable, NeverGoal, Path, &Visited);

	if (Visited.Num() <= 1)
	{
		return; // 갈 수 있는 칸이 자기 자리뿐 — 다음 주기에 다시 본다
	}

	// 0번은 현재 칸이므로 제외. 방문 배열은 BFS 순서(결정론)라 인덱스 추첨이 재현된다.
	const int32 Chosen = RandomStream.RandRange(1, Visited.Num() - 1);

	auto IsGoal = [&Visited, Chosen](const FIntVector& Each) { return Each == Visited[Chosen]; };
	if (RunBFS(FootCell, IsPassable, IsGoal, Path) && Path.Num() > 1)
	{
		PathCells = MoveTemp(Path);
		PathIndex = 1;
	}
}

void ABotController::FollowPath(const FIntVector& FootCell)
{
	ACA3DCharacter* BotChar = GetBotCharacter(); // 이름이 Character 면 AController::Character 를 가린다
	const AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!BotChar || !VoxelWorld)
	{
		return;
	}

	// 도착한 웨이포인트는 소비한다. while 인 이유: 한 틱에 두 칸을 지나칠 수 있고(속도 4칸/초 ×
	// 프레임 드랍), 그때 소비를 하나만 하면 봇이 뒤로 되돌아간다.
	const int32 ConsumeFrom = PathIndex;
	while (PathCells.IsValidIndex(PathIndex) && PathCells[PathIndex] == FootCell)
	{
		++PathIndex;
	}

#if !UE_BUILD_SHIPPING
	// 2칸 이상 낙하를 **실제로 완료했을 때** 남긴다 (계획만 하고 안 뛰는 경우와 구분하려고
	// 착지 시점에 찍는다). 이 Task 의 목적이 층간 이동이므로 -game 세션에서 이 줄의 유무가
	// 곧 검증이다 — `-LogCmds="LogCA3D Verbose"` 로 켠다.
	for (int32 Index = FMath::Max(ConsumeFrom, 1); Index < PathIndex; ++Index)
	{
		const int32 Drop = PathCells[Index - 1].Z - PathCells[Index].Z;
		if (Drop >= 2)
		{
			UE_LOG(LogCA3D, Verbose, TEXT("ABotController %s: %d칸 낙하 착지 — (%d,%d,%d) → (%d,%d,%d)"),
				*GetName(), Drop,
				PathCells[Index - 1].X, PathCells[Index - 1].Y, PathCells[Index - 1].Z,
				PathCells[Index].X, PathCells[Index].Y, PathCells[Index].Z);
		}
	}
#endif

	if (!PathCells.IsValidIndex(PathIndex))
	{
		return; // 경로 소진 — 다음 재계획이 새 목적지를 잡는다
	}

	const FIntVector Next = PathCells[PathIndex];

	// **오를 수 없는 높이가 눈앞에 있으면 그 경로는 이미 낡았다.** 계획할 때 서 있던 높이가
	// 아니라는 뜻이다 (떨어졌거나, 밟고 있던 발판이 폭발로 사라졌거나). 여기서 그냥 진행하면
	// 아래의 점프 조건이 매 틱 참이 되어 **오를 수 없는 벽에 붙어 계속 튄다** — 가장 흔한
	// "봇이 벽에 끼었다" 증상이 이것이다. 경로를 버리면 다음 틱의 재계획이 지금 높이에서
	// 다시 길을 잡는다 (낙하는 제한이 없으므로 내려가는 길은 대개 남아 있다).
	if (Next.Z - FootCell.Z > VoxelMove::MaxClimbCells)
	{
		UE_LOG(LogCA3D, Verbose,
			TEXT("ABotController %s: 경로 폐기 — 발밑 (%d,%d,%d) 에서 다음 칸 (%d,%d,%d) 은 %d칸 위 (오르기 상한 %d)"),
			*GetName(), FootCell.X, FootCell.Y, FootCell.Z, Next.X, Next.Y, Next.Z,
			Next.Z - FootCell.Z, VoxelMove::MaxClimbCells);
		PathCells.Reset();
		PathIndex = 0;
		return;
	}

	// 이동은 캐릭터의 공용 진입점으로. 월드 평면 방향만 넘긴다 (Move 의 계약 — 캐릭터는 카메라를 모른다).
	FVector Delta = VoxelWorld->CellToWorld(Next) - BotChar->GetActorLocation();
	Delta.Z = 0.f;
	if (!Delta.IsNearlyZero())
	{
		const FVector Dir = Delta.GetSafeNormal();
		BotChar->Move(FVector2D(Dir.X, Dir.Y));
	}

	// 한 칸 위로 올라가야 하면 점프. 위 가드 덕분에 여기 오는 높이 차는 정확히 MaxClimbCells(1)
	// 이하다 — **2칸 이상은 시도조차 하지 않는다.** 지상에 있을 때만 부르는 이유는 공중에서 매 틱
	// 부르면 CMC 의 점프 입력이 계속 눌린 상태가 되어 체공·이동 거리가 튜닝값과 달라지기 때문이다.
	// 내려가는 방향(Next.Z < FootCell.Z)에는 아무것도 하지 않는다 — 수평 입력만으로 턱에서
	// 걸어 나가면 중력이 알아서 데려간다. 낙하 높이에 제한이 없는 것이 그래서 공짜로 성립한다.
	// (갇힘 중에는 ACA3DCharacter::DoJump 가 스스로 막는다 — 봇도 예외 없다.)
	if (Next.Z > FootCell.Z)
	{
		const UCharacterMovementComponent* Movement = BotChar->GetCharacterMovement();
		if (Movement && Movement->IsMovingOnGround())
		{
			BotChar->DoJump();
		}
	}
}
