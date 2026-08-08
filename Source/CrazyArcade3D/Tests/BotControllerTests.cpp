// ABotController 자동화 테스트 (Task 20).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.AI.BotController"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다:
//   ① 봇에 ACA3DPlayerState 가 붙는가 (bWantsPlayerState — Task 18 승패 판정이 봇을 사람처럼 다루는 전제)
//   ② BFS 경로가 통행 가능한 셀만 지나가고, **1칸은 오르되 2칸은 못 오른다**
//   ③ 탈출로가 없는 자리에는 폭탄을 놓지 않는다 (ShouldPlaceBombAt 의 실질)
//   ④ 위험 셀 판정이 UExplosionSubsystem::Propagate 결과와 정확히 일치한다 (불변식 2)
//   ⑤ Dead 봇은 이동·설치 입력을 만들지 않는다 (생존 대조군 포함)
//   ⑥ GameMode 가 부족 인원을 봇으로 채우고, 사람과 **같은 등록 경로**를 탄다 + ca3d.BotFill 우선
// 실제 PIE 동작(층간 이동 체감·자기 폭탄 회피·매치 완주·stat unit)은 체크리스트 20 의 PIE 항목.
//
// 월드는 CA3DGameModeTests 관례대로 GameInstance 표준 초기화 → SetGameMode → InitializeActorsForPlay
// 로 만든다 (GameMode 가 있어야 AAIController::PostInitializeComponents 가 PlayerState 를 만든다).
// World->BeginPlay() 는 부르지 않는다 — 지형은 판정이 명확하도록 손으로 구성한다(friend).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 BotTest~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).
// (BotController.cpp 자신도 Bot~ 접두사 헬퍼를 갖고 있어 Bot~ 만으로는 부족하다.)

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
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Bomb/ExplosionTypes.h"
#include "Voxel/VoxelWorld.h"
#include "HAL/IConsoleManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotControllerTest, "CrazyArcade3D.AI.BotController",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr int32 BotTestSizeX = 11;
	constexpr int32 BotTestSizeY = 3;
	constexpr int32 BotTestSizeZ = 4;

	// 판정이 손으로 따라갈 수 있는 1칸 폭 복도 (y = 1 만 통행, 나머지는 Immortal 벽).
	//
	//   x:  0   1   2   3   4   5   6   7   8   9  10
	//      ██  ..  ..  ..  ..  ▓▓  ..  ██  ..  ..  ██   z=1   (▓▓=Destructible, ██=Immortal)
	//      ██  ..  ▒▒  ..  ..  ..  ..  ██  ..  ..  ██   z=2   (▒▒=Destructible — 주머니 뚜껑)
	//      ██  ..  ██  ..  ..  ..  ..  ..  ..  ..  ██   z=3
	//
	// 의도:
	//   · x=1 은 **막다른 주머니** — 유일한 이웃 (2,1,*) 이 전부 솔리드라 탈출로가 없다.
	//     동시에 (2,1,1) 이 Destructible 이라 "부술 게 있다"는 설치 이유는 성립한다
	//     → 탈출로 확인이 없으면 봇이 여기서 자폭한다.
	//   · x=5 는 z=1 이 솔리드라 (5,1,2) 에 올라선다 — **1칸 오르기**가 되는지 본다.
	//   · x=7 은 z=1,2 가 솔리드라 (7,1,3) 은 **2칸 위** — 못 오른다.
	//   · x=8,9 는 그 너머라 도달 불가.
	void BotTestBuildGrid(FVoxelGrid& Grid)
	{
		Grid.Init(FIntVector(BotTestSizeX, BotTestSizeY, BotTestSizeZ));

		for (int32 X = 0; X < BotTestSizeX; ++X)
		{
			for (int32 Y = 0; Y < BotTestSizeY; ++Y)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor); // 전면 바닥판

				// 복도 밖(y=0, y=2)과 맵 끝(x=0, x=10)은 불멸 벽 — 폭발이 그리드 밖으로
				// 새 나가지 않게 막는다 (FVoxelGrid 는 범위 밖을 Empty 로 돌려준다).
				const bool bWall = (Y != 1) || (X == 0) || (X == BotTestSizeX - 1);
				if (bWall)
				{
					for (int32 Z = 1; Z < BotTestSizeZ; ++Z)
					{
						Grid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
					}
				}
			}
		}

		// 주머니 뚜껑: (2,1,1) 은 부술 수 있는 벽, (2,1,2) 도 막아 위로도 못 빠져나가게,
		// (2,1,3) 은 불멸 — 세 칸 다 솔리드라 x=1 에서 갈 수 있는 칸이 하나도 없다.
		Grid.Set(FIntVector(2, 1, 1), EBlockType::Destructible);
		Grid.Set(FIntVector(2, 1, 2), EBlockType::Destructible);
		Grid.Set(FIntVector(2, 1, 3), EBlockType::Immortal);

		// 1칸 턱 (오를 수 있다) — 파괴 가능 블록이라 "설치할 이유"도 만들어 준다.
		Grid.Set(FIntVector(5, 1, 1), EBlockType::Destructible);

		// 2칸 턱 (못 오른다).
		Grid.Set(FIntVector(7, 1, 1), EBlockType::Immortal);
		Grid.Set(FIntVector(7, 1, 2), EBlockType::Immortal);
	}

	// 캡슐 바닥이 셀 (X,Y,Z) 안에 오도록 배치하는 좌표 (CellSize 100, 엔진 기본 반높이 88).
	FVector BotTestLocForFootCell(int32 X, int32 Y, int32 Z)
	{
		constexpr float HalfHeight = 88.f;
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	}

	int32 BotTestCountBombs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ABomb> It(World); It; ++It) // 파괴 대기 액터는 이터레이터가 건너뛴다
		{
			++Count;
		}
		return Count;
	}

	// (VoxelWorld 캐시는 호출자가 TryApplyMovementTuning 으로 채운다 — private 이라 friend 인
	//  테스트 본문에서만 부를 수 있다. 안 채우면 GetFootCell 이 ensure 로 터진다.)
	ACA3DCharacter* BotTestSpawnCharacter(UWorld* World, const FVector& Location)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Character)
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking); // 테스트 월드엔 바닥 컬리전이 없다
			Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Character;
	}
}

bool FBotControllerTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성 ───
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

	ACA3DGameMode* GameMode = World->GetAuthGameMode<ACA3DGameMode>();
	if (!TestNotNull(TEXT("ACA3DGameMode 생성"), GameMode))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// GameState 는 InitializeActorsForPlay 이후에야 존재한다 (CA3DPlayerStateTests 주석).
	World->InitializeActorsForPlay(URL);

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	UCA3DRuleSet* InjectedRules = NewObject<UCA3DRuleSet>(GameMode);
	GameMode->Rules = InjectedRules; // friend — BP(DA_Rules_Default) 대신 주입
	if (GameState)
	{
		GameState->Rules = InjectedRules; // 봇의 ResolveRules 가 읽는 경로
	}

	// ─── 손그리드 ───
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	BotTestBuildGrid(VoxelWorld->Grid); // friend
	VoxelWorld->bGridInitialized = true;

	UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>();
	if (!TestNotNull(TEXT("UExplosionSubsystem 존재"), Explosion))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ─── 1. 봇 스폰 — ACA3DPlayerState 가 붙는가 (bWantsPlayerState) ───
	// 이게 이 Task 의 핵심이다: PlayerState 가 있어야 Task 18 의 승패 판정
	// (GameState->PlayerArray 순회 · 공동 등수 · 최후 1인 종료)이 봇을 사람과 똑같이 취급한다.
	FActorSpawnParameters BotParams;
	BotParams.ObjectFlags |= RF_Transient;
	ABotController* Bot = World->SpawnActor<ABotController>(ABotController::StaticClass(), BotParams);
	if (!TestNotNull(TEXT("① ABotController 스폰"), Bot))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	TestTrue(TEXT("① 봇이 PlayerState 를 원한다 (bWantsPlayerState)"), Bot->bWantsPlayerState != 0);
	ACA3DPlayerState* BotState = Bot->GetPlayerState<ACA3DPlayerState>();
	TestNotNull(TEXT("① 봇에 ACA3DPlayerState 부착 — 승패 판정이 봇을 사람처럼 센다"), BotState);
	if (BotState)
	{
		TestTrue(TEXT("① 봇 PlayerState 가 GameState->PlayerArray 에 등록"),
			GameState != nullptr && GameState->PlayerArray.Contains(BotState));
	}

	// ─── 2. BFS 경로 (friend 로 직접 호출) ───
	{
		const FIntVector Start(1, 1, 1);

		// (2-a) 막다른 주머니 — 나갈 칸이 없다.
		TestEqual(TEXT("② 주머니에서 복도로 가는 경로 없음 (사방이 솔리드)"),
			Bot->FindPath(Start, FIntVector(4, 1, 1)).Num(), 0);

		// (2-b) 평지 직선 — 시작 셀 포함해서 반환한다.
		const FIntVector Open(3, 1, 1);
		const TArray<FIntVector> FlatPath = Bot->FindPath(Open, FIntVector(4, 1, 1));
		TestEqual(TEXT("② 평지 한 칸 경로 = [시작, 목표]"), FlatPath.Num(), 2);
		if (FlatPath.Num() == 2)
		{
			TestEqual(TEXT("② 경로 시작이 From"), FlatPath[0], Open);
			TestEqual(TEXT("② 경로 끝이 To"), FlatPath[1], FIntVector(4, 1, 1));
		}

		// (2-c) **1칸 턱은 오른다** — (5,1,1) 이 솔리드라 (5,1,2) 를 밟고 (6,1,1) 로 내려선다.
		const TArray<FIntVector> ClimbPath = Bot->FindPath(Open, FIntVector(6, 1, 1));
		TestTrue(TEXT("② 1칸 턱 넘는 경로 존재"), ClimbPath.Num() > 0);
		TestTrue(TEXT("② 1칸 턱 경로가 (5,1,2) 를 밟는다"), ClimbPath.Contains(FIntVector(5, 1, 2)));

		// 경로 무결성: 모든 칸이 실제로 설 수 있고, 한 걸음은 평면 1칸 + 높이 차 1칸 이내.
		const FVoxelGrid& Grid = VoxelWorld->GetGrid();
		bool bAllStandable = true;
		bool bAllSteps = true;
		for (int32 Index = 0; Index < ClimbPath.Num(); ++Index)
		{
			if (!Bot->IsStandable(Grid, ClimbPath[Index]))
			{
				bAllStandable = false;
			}
			if (Index > 0)
			{
				const FIntVector Step = ClimbPath[Index] - ClimbPath[Index - 1];
				const int32 Planar = FMath::Abs(Step.X) + FMath::Abs(Step.Y);
				if (Planar != 1 || FMath::Abs(Step.Z) > 1)
				{
					bAllSteps = false;
				}
			}
		}
		TestTrue(TEXT("② 경로의 모든 칸이 설 수 있는 칸"), bAllStandable);
		TestTrue(TEXT("② 한 걸음 = 평면 1칸 + 높이 차 1칸 이내"), bAllSteps);

		// (2-d) **2칸 턱은 못 오른다** — (7,1,3) 과 그 너머는 도달 불가.
		TestEqual(TEXT("② 2칸 위 칸으로 가는 경로 없음 (점프 높이 1칸)"),
			Bot->FindPath(Open, FIntVector(7, 1, 3)).Num(), 0);
		TestEqual(TEXT("② 2칸 턱 너머도 도달 불가"),
			Bot->FindPath(Open, FIntVector(8, 1, 1)).Num(), 0);
	}

	// ─── 3. 봇에 폰 부착 — 여기부터는 Status(범위·슬롯)가 필요하다 ───
	// 봇은 복도 끝 격리 구역(x=8,9)에 둔다: 폭탄·상대와 무관하게 "스스로 움직이는가"만 본다.
	ACA3DCharacter* BotChar = BotTestSpawnCharacter(World, BotTestLocForFootCell(8, 1, 1));
	if (!TestNotNull(TEXT("③ 봇 폰 스폰"), BotChar))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	BotChar->TryApplyMovementTuning(); // friend — VoxelWorld 캐시 (GetFootCell 의 전제)
	Bot->Possess(BotChar);
	TestTrue(TEXT("③ 봇이 폰을 조종한다"), Bot->GetPawn() == BotChar);
	TestEqual(TEXT("③ 발밑 셀 (8,1,1)"), BotChar->GetFootCell(), FIntVector(8, 1, 1));

	UStatusComponent* BotStatus = BotChar->GetStatus();
	if (!TestNotNull(TEXT("③ StatusComponent 부착"), BotStatus))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	BotStatus->BombRange = 1; // BeginPlay 가 돌지 않는 월드 — 룰셋 초기값 로드를 손으로 대신한다

	// ─── 4. 설치 판단 — 탈출로가 판단의 실질이다 ───
	{
		const FVoxelGrid& Grid = VoxelWorld->GetGrid();
		const TArray<FIntVector> NoBombs;

		// (4-a) 막다른 주머니 (1,1,1): 부술 블록은 있다(= 설치할 이유는 성립) — 대조 확인.
		const FExplosionResult PocketResult = UExplosionSubsystem::Propagate(
			Grid, FIntVector(1, 1, 1), 1, InjectedRules->bFloorDestructible, NoBombs);
		TestTrue(TEXT("④ 주머니에도 부술 블록은 있다 — 거부 사유가 '이득 없음'이 아님을 못 박는다"),
			PocketResult.BrokenCells.Num() > 0);
		TestFalse(TEXT("④ 탈출로 없는 자리에는 놓지 않는다 (없으면 자폭만 반복한다)"),
			Bot->ShouldPlaceBombAt(FIntVector(1, 1, 1)));

		// (4-b) 복도 (4,1,1): 옆이 부술 블록(5,1,1) + 뒤로 빠질 칸이 남는다 → 놓는다.
		TestTrue(TEXT("④ 이득 + 탈출로가 있으면 놓는다"), Bot->ShouldPlaceBombAt(FIntVector(4, 1, 1)));

		// (4-c) 이득이 없는 자리 — 사거리 안에 부술 블록도 상대도 없다.
		TestFalse(TEXT("④ 이득 없는 자리에는 놓지 않는다"), Bot->ShouldPlaceBombAt(FIntVector(9, 1, 1)));
	}

	// ─── 5. 위험 판정이 Propagate 결과와 정확히 일치 (불변식 2) ───
	{
		ACA3DCharacter* Placer = BotTestSpawnCharacter(World, BotTestLocForFootCell(3, 1, 1));
		if (TestNotNull(TEXT("⑤ 폭탄 설치용 캐릭터 스폰"), Placer))
		{
			Placer->TryApplyMovementTuning(); // friend — 설치 경로가 VoxelWorld 캐시를 요구한다
			UStatusComponent* PlacerStatus = Placer->GetStatus();
			PlacerStatus->MaxBombCount = 1;
			PlacerStatus->BombRange = 2;

			Placer->ServerPlaceBomb(FIntVector(3, 1, 1)); // 플레이어와 같은 서버 경로
			TestEqual(TEXT("⑤ 폭탄 1개 설치됨"), BotTestCountBombs(World), 1);

			const ABomb* Bomb = Explosion->FindBombAt(FIntVector(3, 1, 1));
			if (TestNotNull(TEXT("⑤ 레지스트리에서 폭탄 조회"), Bomb))
			{
				// 봇이 봐야 하는 정답 = 실폭발이 쓰는 바로 그 함수의 출력.
				const FExplosionResult Expected = UExplosionSubsystem::Propagate(
					VoxelWorld->GetGrid(), FIntVector(3, 1, 1), Bomb->GetRange(),
					InjectedRules->bFloorDestructible, Explosion->GetActiveBombCellsSorted());

				TestTrue(TEXT("⑤ 물줄기가 원점 외에도 퍼진다 (판정이 자명해지지 않게)"),
					Expected.WaterCells.Num() > 1);

				int32 Mismatch = 0;
				for (int32 X = 0; X < BotTestSizeX; ++X)
				{
					for (int32 Y = 0; Y < BotTestSizeY; ++Y)
					{
						for (int32 Z = 0; Z < BotTestSizeZ; ++Z)
						{
							const FIntVector Cell(X, Y, Z);
							if (Bot->IsCellDangerous(Cell) != Expected.WaterCells.Contains(Cell))
							{
								++Mismatch;
							}
						}
					}
				}
				TestEqual(TEXT("⑤ 전 셀에서 봇의 위험 판정 == Propagate 물줄기 (프리뷰·실폭발과 같은 답)"),
					Mismatch, 0);

				// 범위는 **폭탄이 들고 있는 값** — 설치자가 나중에 포션을 먹어도 흔들리면 안 된다.
				PlacerStatus->BombRange = 5;
				int32 MismatchAfterBuff = 0;
				for (const FIntVector& Water : Expected.WaterCells)
				{
					if (!Bot->IsCellDangerous(Water))
					{
						++MismatchAfterBuff;
					}
				}
				TestEqual(TEXT("⑤ 설치자 범위가 커져도 위험 구역 불변 (ABomb 의 Range 를 쓴다)"),
					MismatchAfterBuff, 0);
			}
		}
	}

	// ─── 6. Tick — 살아 있으면 움직이고, 죽으면 아무 입력도 만들지 않는다 ───
	{
		UCharacterMovementComponent* Movement = BotChar->GetCharacterMovement();

		// (6-a) 생존 대조군 — 사망 후 "안 한다"가 의미를 가지려면 살아서는 해야 한다.
		Movement->ConsumeInputVector();
		Bot->Tick(0.5f);
		TestFalse(TEXT("⑥ 생존 봇: 스스로 이동 입력을 만든다"),
			Movement->GetPendingInputVector().IsNearlyZero());
		TestTrue(TEXT("⑥ 생존 봇: 경로를 잡았다"), Bot->PathCells.Num() > 0);
		Movement->ConsumeInputVector();

		// (6-b) 사망 — 컨트롤러 단계에서 끊는다 (시체가 매 틱 BFS 를 돌리지 않게).
		const int32 BombsBeforeDeath = BotTestCountBombs(World);
		BotStatus->ServerKill(EDeathCause::Fall);
		TestEqual(TEXT("⑥ 사망 처리됨"), BotStatus->LifeState, ELifeState::Dead);

		Bot->Tick(0.5f);
		TestTrue(TEXT("⑥ 사망 봇: 이동 입력 없음"),
			Movement->GetPendingInputVector().IsNearlyZero());
		TestFalse(TEXT("⑥ 사망 봇: 점프 입력 없음"), BotChar->bPressedJump);
		TestEqual(TEXT("⑥ 사망 봇: 폭탄 설치 없음"), BotTestCountBombs(World), BombsBeforeDeath);
		TestEqual(TEXT("⑥ 사망 봇: 경로 비움 (BFS 도 돌지 않는다)"), Bot->PathCells.Num(), 0);
	}

	// ─── 7. GameMode 봇 채우기 — 사람과 같은 등록 경로 + ca3d.BotFill 우선 ───
	// (이 절은 폰을 여럿 스폰하므로 맨 뒤에 둔다.)
	{
		GameMode->VoxelWorld = VoxelWorld; // friend — BeginPlay 를 돌리지 않았으므로 손으로 배선
		GameMode->SpawnCells = { FIntVector(3, 1, 1), FIntVector(4, 1, 1),
			FIntVector(6, 1, 1), FIntVector(8, 1, 1) };
		GameMode->MatchParticipantCount = 0;
		if (GameState)
		{
			GameState->AliveCount = 0;
		}

		// (7-a) 룰셋 기본은 꺼짐 — 켜져 있으면 기존 PIE·테스트의 인원수가 조용히 바뀐다.
		TestFalse(TEXT("⑦ 룰셋 기본값: 봇 채우기 꺼짐"), InjectedRules->bFillWithBots);
		GameMode->SpawnFillBots();
		TestEqual(TEXT("⑦ 꺼져 있으면 봇을 만들지 않는다"), GameMode->MatchParticipantCount, 0);

		// (7-b) 룰셋으로 켜기 — 부족분만큼 채운다.
		InjectedRules->bFillWithBots = true;
		InjectedRules->BotFillTargetPlayers = 3;
		GameMode->SpawnFillBots();
		TestEqual(TEXT("⑦ 룰셋 목표 3명까지 채움"), GameMode->MatchParticipantCount, 3);
		if (GameState)
		{
			TestEqual(TEXT("⑦ AliveCount 도 같은 등록 경로로 증가 (RegisterParticipant 공용)"),
				GameState->AliveCount, 3);
		}

		// (7-c) 콘솔 변수가 룰셋을 덮어쓴다 — 헤드리스 데디에서 -ExecCmds 로 켜는 문.
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ca3d.BotFill"));
		if (TestNotNull(TEXT("⑦ 콘솔 변수 ca3d.BotFill 등록됨"), CVar))
		{
			TestEqual(TEXT("⑦ 기본값 -1 = 룰셋 따름"), CVar->GetInt(), -1);
			CVar->Set(5, ECVF_SetByCode);
			GameMode->SpawnFillBots();
			TestEqual(TEXT("⑦ ca3d.BotFill 5 → 총 5명"), GameMode->MatchParticipantCount, 5);
			CVar->Set(-1, ECVF_SetByCode); // 다른 테스트에 새지 않게 원복
		}

		// 채운 봇들이 사람과 같은 것들을 갖췄는가: 이름·봇 표시·색 인덱스·폰.
		int32 NamedBots = 0;
		int32 PossessedBots = 0;
		TArray<int32> ColorIndices;
		for (TActorIterator<ABotController> It(World); It; ++It)
		{
			ABotController* Each = *It;
			if (Each == Bot)
			{
				continue; // 1번 절에서 손으로 만든 봇 — 등록 대상이 아니다
			}
			if (const ACA3DPlayerState* EachState = Each->GetPlayerState<ACA3DPlayerState>())
			{
				if (EachState->IsABot() && EachState->GetPlayerName().StartsWith(TEXT("Bot ")))
				{
					++NamedBots;
				}
				ColorIndices.AddUnique(EachState->ColorIndex);
			}
			if (ACA3DCharacter* EachPawn = Cast<ACA3DCharacter>(Each->GetPawn()))
			{
				++PossessedBots;
				EachPawn->TryApplyMovementTuning(); // friend — 이후 GetFootCell 이 ensure 로 터지지 않게
			}
		}
		TestEqual(TEXT("⑦ 봇 5대 전부 `Bot N` 이름 + 봇 표시"), NamedBots, 5);
		TestEqual(TEXT("⑦ 봇 5대 전부 ChoosePlayerStart 로 폰 배정 (봇 전용 스폰 경로 없음)"),
			PossessedBots, 5);
		TestEqual(TEXT("⑦ ColorIndex 가 참가 순서대로 겹치지 않게 배정"), ColorIndices.Num(), 5);
	}

	// ─── 8. 층간 이동 — **2칸 이상 낙하는 지름길, 2칸 오르기는 여전히 불가** ───
	//
	// 봇 층간 이동 개선의 본론이다. 예전 BFS 는 dz ∈ {0,±1} 고정이라 오르기 제약(1칸)이
	// 내려가기에도 걸려 있었다 — 절차 맵(Task 22)의 산 위에서 못 내려오거나 빙 돌아갔다.
	// 이제 인접 규칙은 FMapValidator 와 **같은 함수**(VoxelMove)를 쓴다.
	//
	// 지형을 여기서 고치는 이유는 앞 절들의 판정을 건드리지 않기 위해서다 (맨 뒤에 둔다).
	// x=8 을 2칸 기둥으로 세워 (8,1,3) 발판을 만든다 — 그 옆 (9,1,1) 은 **2칸 아래**다.
	{
		VoxelWorld->Grid.Set(FIntVector(8, 1, 1), EBlockType::Immortal); // friend
		VoxelWorld->Grid.Set(FIntVector(8, 1, 2), EBlockType::Immortal);

		const FIntVector Ledge(8, 1, 3);
		const FIntVector Ground(9, 1, 1);

		const FVoxelGrid& Grid = VoxelWorld->GetGrid();
		TestTrue(TEXT("⑧ 기둥 위가 설 수 있는 칸"),  Bot->IsStandable(Grid, Ledge));
		TestTrue(TEXT("⑧ 그 옆 바닥도 설 수 있는 칸"), Bot->IsStandable(Grid, Ground));

		// 내려가기: 한 걸음에 2칸 떨어진다 (낙하는 높이 제한이 없다).
		const TArray<FIntVector> DownPath = Bot->FindPath(Ledge, Ground);
		TestEqual(TEXT("⑧ **2칸 낙하는 한 걸음** — 경로 = [기둥 위, 바닥]"), DownPath.Num(), 2);
		if (DownPath.Num() == 2)
		{
			TestEqual(TEXT("⑧ 낙하 폭이 정확히 2칸"), DownPath[0].Z - DownPath[1].Z, 2);
		}

		// 올라가기: 여전히 못 한다 (점프 높이 1칸). 이걸 허용하면 봇이 오를 수 없는 벽에
		// 붙어 계속 점프한다 — 낙하 완화가 오르기 완화로 새지 않았음을 못 박는다.
		TestEqual(TEXT("⑧ 2칸 오르기는 여전히 불가 — 반대 방향 경로 없음"),
			Bot->FindPath(Ground, Ledge).Num(), 0);
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
