// 봇이 가까운 아이템을 주우러 가는 목표(EBotState::SeekItem) 자동화 테스트
// (2026-08-10 사용자 확정: "가까이 있을 때만 줍기").
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.AI.BotSeekItem" 로 실행.
//
// 검증 항목:
//   ① 가까운 아이템이 있으면 그쪽으로 경로를 잡는다
//   ② 상한(BotSeekItemMaxCells)을 넘으면 안 쫓는다 — **직선이 아니라 실제 걸음 수**
//   ③ **위험 셀을 지나는 경로는 고르지 않는다** (제일 중요 — 스탯 하나에 물줄기를 건너지 않는다)
//   ④ 아이템이 사라지면(남이 먼저 먹음·폭발) 목표를 버리고 원래 목표로 복귀한다
//   ⑤ 아이템이 둘이면 매번 같은 하나 (스폰 순서를 뒤집어도)
//   ⑥ **먹어도 아무 변화가 없는 아이템은 안 쫓는다** (상한인 스탯·이미 든 소모품)
//   ⑦ 우선순위: 갇힌 적 > 아이템, 설치 > 아이템, 아이템 > 추격
//
// 월드 구성·검증 골격은 BotPopTrappedTests 를 그대로 따른다. FSM 은 Tick 이 아니라
// PlanSeekItem/Replan 을 직접 불러 검증한다 — 재계획 주기·타이머가 끼어들지 않아야
// "무엇을 골랐는가"가 결정론적으로 드러난다.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Bsi~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/BotController.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Item/ItemPickup.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Voxel/VoxelWorld.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSeekItemTest, "CrazyArcade3D.AI.BotSeekItem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr int32 BsiSizeX = 13;
	constexpr int32 BsiSizeY = 3;
	constexpr int32 BsiSizeZ = 3;

	// z=0 전면 Floor + 그 위(z=1)는 전부 통행 가능한 평지 — 걸음 수를 손으로 셀 수 있게.
	// (1,0,1) 만 파괴 블록: 봇이 (1,1,1) 에 섰을 때 "폭탄을 놓을 이유"가 성립해야
	// **설치 > 아이템** 우선순위를 실제로 겨룰 수 있다 (⑦-b).
	void BsiBuildGrid(FVoxelGrid& Grid)
	{
		Grid.Init(FIntVector(BsiSizeX, BsiSizeY, BsiSizeZ));
		for (int32 X = 0; X < BsiSizeX; ++X)
		{
			for (int32 Y = 0; Y < BsiSizeY; ++Y)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
			}
		}
		Grid.Set(FIntVector(1, 0, 1), EBlockType::Destructible);
	}

	FVector BsiLocForFootCell(int32 X, int32 Y, int32 Z)
	{
		constexpr float HalfHeight = 88.f;
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	}

	int32 BsiCountBombs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ABomb> It(World); It; ++It) // 파괴 대기 액터는 이터레이터가 건너뛴다
		{
			++Count;
		}
		return Count;
	}
}

bool FBotSeekItemTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성 (BotPopTrappedTests 와 같은 절차) ───
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();

	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("GameInstance 월드 생성"), World))
	{
		return false;
	}

	World->GetWorldSettings()->DefaultGameMode = ACA3DGameMode::StaticClass();
	FURL URL;
	World->SetGameMode(URL);

	if (!TestNotNull(TEXT("ACA3DGameMode 생성"), World->GetAuthGameMode<ACA3DGameMode>()))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ServerPlaceBomb 은 UFUNCTION RPC → ProcessEvent 를 타는데, ProcessEvent 는
	// AreActorsInitialized()==false 인 월드에서 이벤트를 조용히 버린다 (BombTests 주석).
	World->InitializeActorsForPlay(URL);

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("ACA3DGameState 존재"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GameState);
	GameState->Rules = Rules; // 봇·캐릭터·상태 컴포넌트가 전부 읽는 경로 (CDO 는 건드리지 않는다)

	TestEqual(TEXT("전제: 아이템 노리기 기본 거리는 '가까이' 다 (맵 대각선 40걸음의 20% 이하)"),
		GetDefault<UCA3DRuleSet>()->BotSeekItemMaxCells, 6);

	// ─── 손그리드 ───
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	BsiBuildGrid(VoxelWorld->Grid); // friend
	VoxelWorld->bGridInitialized = true;

	UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>();
	if (!TestNotNull(TEXT("UExplosionSubsystem 존재"), Explosion))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	auto SpawnCharacterAt = [&](const FVector& Location) -> ACA3DCharacter*
	{
		ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Character)
		{
			Character->TryApplyMovementTuning(); // friend — VoxelWorld 캐시 (GetFootCell 의 전제)
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Character;
	};

	// 아이템 노출 — 실제 경로(ServerInit)를 탄다. 레지스트리 등록도 여기서 일어난다.
	auto SpawnItemAt = [&](EItemType Type, const FIntVector& Cell) -> AItemPickup*
	{
		AItemPickup* Item = World->SpawnActor<AItemPickup>(
			AItemPickup::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Item)
		{
			Item->ServerInit(Type, Cell);
		}
		return Item;
	};

	// ─── 봇 + 폰 ───
	FActorSpawnParameters BotParams;
	BotParams.ObjectFlags |= RF_Transient;
	ABotController* Bot = World->SpawnActor<ABotController>(ABotController::StaticClass(), BotParams);
	ACA3DCharacter* BotChar = SpawnCharacterAt(BsiLocForFootCell(1, 1, 1));
	if (!TestNotNull(TEXT("ABotController 스폰"), Bot) || !TestNotNull(TEXT("봇 폰 스폰"), BotChar))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Bot->Possess(BotChar);
	TestEqual(TEXT("전제: 봇 발밑 셀 (1,1,1)"), BotChar->GetFootCell(), FIntVector(1, 1, 1));

	UStatusComponent* BotStatus = BotChar->GetStatus();
	BotStatus->BombRange = 1; // BeginPlay 가 돌지 않는 월드 — 룰셋 초기값 로드를 손으로 대신한다

	const FIntVector BotCell(1, 1, 1);
	const FIntVector ItemCell(4, 1, 1); // 평지에서 정확히 3걸음
	const TArray<FIntVector> NoDanger;

	// ─── 1. 가까운 아이템이 있으면 그쪽으로 ───
	{
		AItemPickup* Item = SpawnItemAt(EItemType::Balloon, ItemCell);
		if (!TestNotNull(TEXT("① 아이템 노출"), Item))
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return false;
		}
		TestTrue(TEXT("① 전제: 레지스트리에 등록됐다"), Explosion->FindItemAt(ItemCell) == Item);

		TestTrue(TEXT("① 가까운 아이템을 노린다"), Bot->PlanSeekItem(BotCell, NoDanger)); // friend
		TestEqual(TEXT("① 상태가 SeekItem"), Bot->State, EBotState::SeekItem);
		TestTrue(TEXT("① 경로를 잡았다"), Bot->PathCells.Num() > 1);
		if (Bot->PathCells.Num() > 1)
		{
			TestEqual(TEXT("① 경로의 시작이 현재 칸"), Bot->PathCells[0], BotCell);
			TestEqual(TEXT("① 경로의 끝이 아이템 셀"), Bot->PathCells.Last(), ItemCell);
			TestEqual(TEXT("① 평지에서 3걸음"), Bot->PathCells.Num() - 1, 3);
		}
		TestEqual(TEXT("① 다음 웨이포인트부터 따라간다"), Bot->PathIndex, 1);
		TestTrue(TEXT("① 노리는 아이템을 기록했다"), Bot->SeekTarget.Get() == Item);
		TestTrue(TEXT("① 목표가 유효하다"), Bot->IsSeekTargetValid(NoDanger));

		// ─── 2. 상한 — **직선이 아니라 실제 걸음 수** ───
		const int32 SavedMax = Rules->BotSeekItemMaxCells;

		Rules->BotSeekItemMaxCells = 2;
		TestFalse(TEXT("② 상한 2 < 3걸음 → 안 쫓는다"), Bot->PlanSeekItem(BotCell, NoDanger));
		TestNull(TEXT("② 포기했으면 목표 기록도 지운다"), Bot->SeekTarget.Get());

		Rules->BotSeekItemMaxCells = 3;
		TestTrue(TEXT("② 상한과 같으면 쫓는다 (경계 포함)"), Bot->PlanSeekItem(BotCell, NoDanger));

		// x=3 을 y=0,1 만 막아 y=2 로 우회하게 만든다 — 직선 3칸이 실제로는 5걸음이 된다.
		for (int32 Z = 1; Z < BsiSizeZ; ++Z)
		{
			VoxelWorld->Grid.Set(FIntVector(3, 0, Z), EBlockType::Immortal); // friend
			VoxelWorld->Grid.Set(FIntVector(3, 1, Z), EBlockType::Immortal);
		}

		Rules->BotSeekItemMaxCells = 4;
		TestFalse(TEXT("② 직선 3칸이어도 실제 5걸음이면 상한 4 에 걸린다 (걸음 수로 잰다)"),
			Bot->PlanSeekItem(BotCell, NoDanger));

		Rules->BotSeekItemMaxCells = 5;
		TestTrue(TEXT("② 상한을 5 로 올리면 우회로로 쫓는다"), Bot->PlanSeekItem(BotCell, NoDanger));
		TestEqual(TEXT("② 우회 경로는 5걸음"), Bot->PathCells.Num() - 1, 5);

		for (int32 Z = 1; Z < BsiSizeZ; ++Z)
		{
			VoxelWorld->Grid.Set(FIntVector(3, 0, Z), EBlockType::Empty);
			VoxelWorld->Grid.Set(FIntVector(3, 1, Z), EBlockType::Empty);
		}
		Rules->BotSeekItemMaxCells = SavedMax;

		// ─── 3. 위험 셀을 지나는 경로는 고르지 않는다 (제일 중요) ───

		// (3-a) 길을 통째로 막는 위험 — 우회로가 없으니 포기한다.
		const TArray<FIntVector> WallOfDanger = {
			FIntVector(3, 0, 1), FIntVector(3, 1, 1), FIntVector(3, 2, 1) };
		TestFalse(TEXT("③ 위험이 길을 막으면 아이템을 포기한다 (뚫고 가지 않는다)"),
			Bot->PlanSeekItem(BotCell, WallOfDanger));

		// (3-b) 일부만 위험 — 돌아가는 길이 있으면 **위험을 밟지 않는 경로**를 고른다.
		const TArray<FIntVector> PartialDanger = { FIntVector(3, 1, 1) };
		TestTrue(TEXT("③ 우회로가 있으면 노린다"), Bot->PlanSeekItem(BotCell, PartialDanger));

		int32 DangerousSteps = 0;
		for (const FIntVector& Step : Bot->PathCells)
		{
			if (PartialDanger.Contains(Step))
			{
				++DangerousSteps;
			}
		}
		TestEqual(TEXT("③ 고른 경로에 위험 셀이 하나도 없다"), DangerousSteps, 0);
		TestEqual(TEXT("③ 그래도 목표는 그 아이템 그대로"), Bot->PathCells.Last(), ItemCell);

		// (3-c) **아이템이 놓인 칸 자체가 위험**하면 안 쫓는다 — 도착하는 순간 물줄기와 만난다.
		const TArray<FIntVector> DangerOnItem = { ItemCell };
		TestFalse(TEXT("③ 곧 터질 자리의 아이템은 애초에 노리지 않는다"),
			Bot->PlanSeekItem(BotCell, DangerOnItem));

		// (3-d) 이미 노리는 중이었어도 그 칸이 위험해지면 **매 틱 판정에서 즉시** 포기한다.
		TestTrue(TEXT("③ 전제: 다시 노림"), Bot->PlanSeekItem(BotCell, NoDanger));
		TestFalse(TEXT("③ 목표 칸이 위험해지면 그 틱에 목표를 버린다"),
			Bot->IsSeekTargetValid(DangerOnItem));

		// (3-e) 발밑이 위험하면 **회피가 최우선** — Replan 은 아이템을 쳐다보지도 않는다.
		Bot->TimeSinceBombAttempt = 0.f; // 설치 분기가 끼어들지 않게
		Bot->Replan(BotCell, TArray<FIntVector>{ BotCell });
		TestEqual(TEXT("③ 발밑이 위험하면 아이템보다 회피가 먼저"), Bot->State, EBotState::Evade);

		// ─── 4. 아이템이 사라지면 목표를 버리고 원래 목표로 복귀 ───
		TestTrue(TEXT("④ 전제: 다시 노림"), Bot->PlanSeekItem(BotCell, NoDanger));
		TestTrue(TEXT("④ 전제: 목표 유효"), Bot->IsSeekTargetValid(NoDanger));

		Item->Destroy(); // 남이 먼저 먹었거나 물줄기에 탔다 — 어느 쪽이든 액터가 사라진다
		TestFalse(TEXT("④ 아이템이 사라지면 목표가 **즉시** 무효"), Bot->IsSeekTargetValid(NoDanger));
		TestFalse(TEXT("④ 사라진 아이템은 다시 노려지지도 않는다"), Bot->PlanSeekItem(BotCell, NoDanger));

		Bot->Replan(BotCell, NoDanger);
		TestTrue(TEXT("④ 원래 목표로 복귀 (더 이상 SeekItem 이 아니다)"),
			Bot->State != EBotState::SeekItem);
		TestTrue(TEXT("④ 복귀한 목표도 경로를 잡았다 (멈춰 서지 않는다)"), Bot->PathCells.Num() > 1);
	}

	// ─── 5. 아이템이 둘이면 매번 같은 하나 (스폰 순서를 뒤집어도) ───
	{
		const FIntVector MidCell(3, 1, 1);
		const FIntVector LeftCell(1, 1, 1);
		const FIntVector RightCell(5, 1, 1);
		BotChar->SetActorLocation(BsiLocForFootCell(MidCell.X, MidCell.Y, MidCell.Z),
			false, nullptr, ETeleportType::TeleportPhysics);
		TestEqual(TEXT("⑤ 전제: 봇이 두 아이템의 가운데"), BotChar->GetFootCell(), MidCell);

		// (5-a) 왼쪽 → 오른쪽 순으로 노출.
		AItemPickup* First  = SpawnItemAt(EItemType::Balloon, LeftCell);
		AItemPickup* Second = SpawnItemAt(EItemType::Potion,  RightCell);
		FIntVector ChosenA = FIntVector::ZeroValue;
		if (TestNotNull(TEXT("⑤ 아이템 A 노출"), First) && TestNotNull(TEXT("⑤ 아이템 B 노출"), Second))
		{
			TestTrue(TEXT("⑤ 노릴 아이템을 골랐다"), Bot->PlanSeekItem(MidCell, NoDanger));
			ChosenA = Bot->PathCells.Last();
			TestEqual(TEXT("⑤ 둘 중 하나를 골랐다 (양쪽 다 2걸음)"), Bot->PathCells.Num() - 1, 2);

			int32 Unstable = 0;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Bot->PlanSeekItem(MidCell, NoDanger);
				if (Bot->PathCells.Last() != ChosenA)
				{
					++Unstable;
				}
			}
			TestEqual(TEXT("⑤ 8번 반복해도 같은 아이템"), Unstable, 0);

			First->Destroy();
			Second->Destroy();
		}

		// (5-b) **노출 순서를 뒤집어도** 같은 아이템을 고른다 — 레지스트리를 좌표 사전순으로
		// 정렬해 두고 BFS 확장 순서만으로 고르기 때문이다 (액터·등록 순서가 개입할 자리가 없다).
		AItemPickup* ReversedFirst  = SpawnItemAt(EItemType::Potion,  RightCell);
		AItemPickup* ReversedSecond = SpawnItemAt(EItemType::Balloon, LeftCell);
		if (TestNotNull(TEXT("⑤ 역순 아이템 A 노출"), ReversedFirst)
			&& TestNotNull(TEXT("⑤ 역순 아이템 B 노출"), ReversedSecond))
		{
			TestTrue(TEXT("⑤ 역순에서도 골랐다"), Bot->PlanSeekItem(MidCell, NoDanger));
			TestEqual(TEXT("⑤ 노출 순서를 뒤집어도 **같은 하나**를 고른다"),
				Bot->PathCells.Last(), ChosenA);

			ReversedFirst->Destroy();
			ReversedSecond->Destroy();
		}

		BotChar->SetActorLocation(BsiLocForFootCell(BotCell.X, BotCell.Y, BotCell.Z),
			false, nullptr, ETeleportType::TeleportPhysics);
	}

	// ─── 6. 먹어도 아무 변화가 없는 아이템은 안 쫓는다 ───
	//
	// 가치 판단이 아니다("범위가 급한가 속도가 급한가"는 범위 밖) — **이미 상한이라 주워도
	// 아무 일도 안 일어나는 것**만 거른다. 판정은 UStatusComponent::HasRoomForItem 이 진다
	// (ServerApplyItem 바로 옆) — 아래 각 항목은 그 함수의 case 와 1:1 이다.
	{
		// (6-a) 폭탄 개수 — 상한이면 안 쫓고, 여유가 있으면 쫓는다.
		AItemPickup* Balloon = SpawnItemAt(EItemType::Balloon, ItemCell);
		if (TestNotNull(TEXT("⑥ 풍선 노출"), Balloon))
		{
			BotStatus->MaxBombCount = Rules->MaxBombCountCap;
			TestFalse(TEXT("⑥ 폭탄 개수가 이미 상한이면 안 쫓는다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->MaxBombCount = 1;
			TestTrue(TEXT("⑥ 여유가 있으면 쫓는다 (대조군)"), Bot->PlanSeekItem(BotCell, NoDanger));
			Balloon->Destroy();
		}

		// (6-b) 폭발 범위.
		AItemPickup* Potion = SpawnItemAt(EItemType::Potion, ItemCell);
		if (TestNotNull(TEXT("⑥ 포션 노출"), Potion))
		{
			BotStatus->BombRange = Rules->MaxBombRangeCap;
			TestFalse(TEXT("⑥ 폭발 범위가 이미 상한이면 안 쫓는다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->BombRange = 1;
			TestTrue(TEXT("⑥ 여유가 있으면 쫓는다 (대조군)"), Bot->PlanSeekItem(BotCell, NoDanger));
			Potion->Destroy();
		}

		// (6-c) 이동속도 — 상한에 붙었을 때만 거른다. **상한 직전은 여전히 이득**이다
		// (한 칸 덜 올라도 오르긴 오르므로 ServerApplyItem 이 값을 바꾼다).
		AItemPickup* Roller = SpawnItemAt(EItemType::Roller, ItemCell);
		if (TestNotNull(TEXT("⑥ 롤러 노출"), Roller))
		{
			BotStatus->MoveSpeedMul = Rules->MoveSpeedMulCap;
			TestFalse(TEXT("⑥ 이동속도가 이미 상한이면 안 쫓는다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->MoveSpeedMul = Rules->MoveSpeedMulCap - Rules->RollerSpeedStep * 0.5f;
			TestTrue(TEXT("⑥ 상한 직전(한 칸 덜 오름)은 여전히 쫓는다 — 값이 바뀌기는 한다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->MoveSpeedMul = 1.f;
			Roller->Destroy();
		}

		// (6-d) 소모품 bool 두 종 — 이미 들고 있으면 두 번째는 아무것도 아니다.
		AItemPickup* Needle = SpawnItemAt(EItemType::Needle, ItemCell);
		if (TestNotNull(TEXT("⑥ 니들 노출"), Needle))
		{
			BotStatus->bHasNeedle = true;
			TestFalse(TEXT("⑥ 니들을 이미 들고 있으면 안 쫓는다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->bHasNeedle = false;
			TestTrue(TEXT("⑥ 없으면 쫓는다 (대조군)"), Bot->PlanSeekItem(BotCell, NoDanger));
			Needle->Destroy();
		}

		AItemPickup* Kick = SpawnItemAt(EItemType::Kick, ItemCell);
		if (TestNotNull(TEXT("⑥ 킥 노출"), Kick))
		{
			BotStatus->bHasKick = true;
			TestFalse(TEXT("⑥ 킥을 이미 갖고 있으면 안 쫓는다"),
				Bot->PlanSeekItem(BotCell, NoDanger));

			BotStatus->bHasKick = false;
			TestTrue(TEXT("⑥ 없으면 쫓는다 (대조군)"), Bot->PlanSeekItem(BotCell, NoDanger));
			Kick->Destroy();
		}
	}

	// ─── 7. 우선순위 ───
	{
		// (7-a) **갇힌 적 > 아이템** — 확정 킬이 스탯 하나보다 크다.
		AItemPickup* NearItem = SpawnItemAt(EItemType::Balloon, FIntVector(2, 1, 1)); // 1걸음
		ACA3DCharacter* Captive = SpawnCharacterAt(BsiLocForFootCell(5, 1, 1));       // 4걸음
		if (TestNotNull(TEXT("⑦ 코앞 아이템 노출"), NearItem) && TestNotNull(TEXT("⑦ 갇힌 적 스폰"), Captive))
		{
			Captive->GetStatus()->ServerTrap();
			Bot->TimeSinceBombAttempt = 0.f; // 설치는 이 절의 관심사가 아니다

			Bot->Replan(BotCell, NoDanger);
			TestEqual(TEXT("⑦ 아이템이 더 가까워도 갇힌 적이 먼저"), Bot->State, EBotState::PopTrapped);

			// (7-b) **아이템 > 추격** — 갇힘이 풀리면(그냥 산 상대면) 아이템이 이긴다.
			Captive->GetStatus()->bHasNeedle = true;
			Captive->GetStatus()->ServerEscape();
			TestEqual(TEXT("⑦ 전제: 상대가 탈출해 Alive"),
				Captive->GetStatus()->LifeState, ELifeState::Alive);

			Bot->Replan(BotCell, NoDanger);
			TestEqual(TEXT("⑦ 산 상대를 쫓기보다 코앞의 아이템이 먼저"), Bot->State, EBotState::SeekItem);
			TestEqual(TEXT("⑦ 목표는 그 아이템"), Bot->PathCells.Last(), FIntVector(2, 1, 1));

			Captive->Destroy();
		}

		// (7-c) **설치 > 아이템** — 이미 놓을 자리에 서 있는데 아이템 때문에 기회를 버리지 않는다.
		// (폭탄을 놓는 것이 곧 아이템을 만들어 내는 행위이기도 하다.)
		// 폭탄을 실제로 스폰하므로 이 절을 맨 뒤에 둔다.
		{
			Bot->TimeSinceBombAttempt = 999.f; // 쿨다운을 비워 설치를 후보로 만든다
			TestTrue(TEXT("⑦ 전제: 이 자리는 놓을 만하다 (설치가 후보다)"), Bot->ShouldPlaceBombAt(BotCell));
			TestTrue(TEXT("⑦ 전제: 코앞에 아이템도 있다"), Bot->PlanSeekItem(BotCell, NoDanger));

			const int32 BombsBefore = BsiCountBombs(World);
			Bot->Replan(BotCell, NoDanger);

			TestEqual(TEXT("⑦ 아이템이 코앞이어도 설치가 먼저 — 폭탄이 놓였다"),
				BsiCountBombs(World), BombsBefore + 1);
			TestEqual(TEXT("⑦ 설치 직후에는 자기 폭탄을 피한다 (Evade)"), Bot->State, EBotState::Evade);
		}

		if (IsValid(NearItem))
		{
			NearItem->Destroy();
		}
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
