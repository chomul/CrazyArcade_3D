// ACA3DGameMode / ACA3DGameState 자동화 테스트 (Task 08/09).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Framework.GameMode"로 실행.
//
// GameInstance 표준 초기화(InitializeStandalone) → SetGameMode → InitializeActorsForPlay
// → World->BeginPlay 순서로 엔진 LoadMap 흐름을 재현해, GameMode 가 GameState 세팅과
// VoxelWorld 초기화를 주도하는지, ChoosePlayerStart 가 스폰 셀을 순서대로 배정하는지 검증한다.
// 복제(클라 Rules 수신·OnRep 순서)는 PIE 필요 — 여기서는 검증하지 않는다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Voxel/VoxelTypes.h"
#include "Voxel/VoxelWorld.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DGameModeTest, "CrazyArcade3D.Framework.GameMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCA3DGameModeTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성: 엔진 LoadMap 순서 재현 ───
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

	// 레벨 배치 액터 역할 — BeginPlay 전에 스폰해 둔다.
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld);

	World->InitializeActorsForPlay(URL);

	// friend 접근: BP(DA_Rules_Default) 대신 룰셋 주입 + 고정 시드 모드 (경고 로그 없이 결정 경로).
	UCA3DRuleSet* InjectedRules = NewObject<UCA3DRuleSet>(GameMode);
	GameMode->Rules = InjectedRules;
	GameMode->bUseFixedSeed = true;
	GameMode->FixedSeed = 777;

	World->BeginPlay(); // → GameMode->StartPlay → 액터 BeginPlay 일괄 실행

	// ─── 1. GameState 세팅: Rules 포인터 + 매치 시작 시각 ───
	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("GameState 가 ACA3DGameState"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("GameState->Rules == GameMode 주입 룰셋"),
		GameState->Rules.Get(), InjectedRules);
	TestTrue(TEXT("MatchStartServerTime 기록됨 (>= 0)"), GameState->MatchStartServerTime >= 0.f);

	// ─── 2. GameMode 가 VoxelWorld 초기화를 주도 (임시 자동 초기화 제거 확인) ───
	TestTrue(TEXT("BeginPlay 후 그리드 초기화됨"), VoxelWorld->GetGrid().Blocks.Num() > 0);

	const TArray<FIntVector>& SpawnCells = VoxelWorld->GetSpawnCells();
	if (!TestEqual(TEXT("폴백 생성기 스폰 셀 8개"), SpawnCells.Num(), 8))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ─── 3. ChoosePlayerStart: 스폰 셀 순서 배정 + CellToWorldFloor + 반높이 보정 ───
	const APawn* PawnCDO = GameMode->DefaultPawnClass
		? GameMode->DefaultPawnClass->GetDefaultObject<APawn>() : nullptr;
	const float HalfHeight = PawnCDO ? PawnCDO->GetDefaultHalfHeight() : 0.f;

	TArray<AActor*> Starts;
	for (int32 i = 0; i < SpawnCells.Num(); ++i)
	{
		// 이 테스트는 Player 를 쓰지 않는 자체 배정 경로만 통과한다 (SpawnCells 검증 후라 안전).
		AActor* Start = GameMode->ChoosePlayerStart(nullptr);
		if (!TestNotNull(*FString::Printf(TEXT("%d번째 배정 반환 액터"), i), Start))
		{
			continue;
		}
		Starts.Add(Start);

		const FVector Expected =
			VoxelWorld->CellToWorldFloor(SpawnCells[i]) + FVector(0.f, 0.f, HalfHeight);
		TestTrue(*FString::Printf(
			TEXT("%d번째 배정 위치 == 스폰 셀 (%d,%d,%d) 바닥 + 폰 반높이"),
			i, SpawnCells[i].X, SpawnCells[i].Y, SpawnCells[i].Z),
			Start->GetActorLocation().Equals(Expected, KINDA_SMALL_NUMBER));
	}

	// 8명 전원 서로 다른 스폰 배정.
	for (int32 A = 0; A < Starts.Num(); ++A)
	{
		for (int32 B = A + 1; B < Starts.Num(); ++B)
		{
			TestTrue(*FString::Printf(TEXT("배정 %d, %d 위치가 서로 다름"), A, B),
				!Starts[A]->GetActorLocation().Equals(Starts[B]->GetActorLocation(), KINDA_SMALL_NUMBER));
		}
	}

	// 정원 초과(9번째) → 첫 셀부터 재순환 + 임시 PlayerStart 캐시 재사용 (액터 누적 없음).
	if (Starts.Num() == SpawnCells.Num())
	{
		AActor* Wrapped = GameMode->ChoosePlayerStart(nullptr);
		TestEqual(TEXT("9번째 배정은 첫 배정 액터 재사용"), Wrapped, Starts[0]);
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 스폰 게이트 — "지형이 준비되기 전에는 폰을 지형 밖에 스폰하지 않는다"
//
// 이 결함은 **PIE 로는 절대 안 잡힌다.** 엔진은 PIE 월드에 APlayerStartPIE 를 뷰포트 카메라
// 위치에 미리 스폰해 두고(UGameInstance::InitializeForPlayInEditor → SpawnPlayFromHereStart),
// 엔진 폴백(AGameModeBase::ChoosePlayerStart_Implementation)이 그것을 최우선으로 고른다 —
// 카메라는 보통 아레나 위라 지형이 완성된 뒤 그 위로 떨어진다. `-game`·패키징 빌드에는 그
// 그물이 없어 FindPlayerStart 가 WorldSettings(원점)를 돌려주고 사람이 매번 낙사한다
// (2026-08-09 실측). 그래서 회귀 방벽을 **PIE 없이** 세운다: 지형이 없는 상태에서
// 로그인 절차(참가 등록 + 폰 스폰 시도)를 그대로 태우고, 폰이 생기지 않는지 본다.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DSpawnGateTest, "CrazyArcade3D.Framework.SpawnGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
	// 접두사 Sg~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).
	int32 SgCountCharacters(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACA3DCharacter> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	// 접속한 컨트롤러 하나 — 참가 등록·폰 스폰 시도는 friend 접근이 필요해 RunTest 본문에서 한다.
	ACA3DPlayerController* SgSpawnController(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACA3DPlayerController>(
			ACA3DPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
}

bool FCA3DSpawnGateTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성: 엔진 LoadMap 순서 재현 (위 테스트와 동일) ───
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
	AVoxelWorld* VoxelWorld = nullptr;
	if (TestNotNull(TEXT("ACA3DGameMode 생성"), GameMode))
	{
		VoxelWorld = World->SpawnActor<AVoxelWorld>(); // 레벨 배치 액터 역할
	}
	if (!GameMode || !TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	World->InitializeActorsForPlay(URL); // GameState 는 이 이후에야 존재한다

	UCA3DRuleSet* InjectedRules = NewObject<UCA3DRuleSet>(GameMode);
	GameMode->Rules = InjectedRules;   // friend
	GameMode->bUseFixedSeed = true;
	GameMode->FixedSeed = 777;

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("GameState 가 ACA3DGameState"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ─── 1. 지형이 없는 상태에서 로그인 (= -game 의 실제 순서) ───
	//
	// 엔진 접속 순서는 Login → PostLogin 이고, 이 두 단계가 **각각** 이 결함에 기여했다:
	//   Login   : AGameModeBase::InitNewPlayer → UpdatePlayerStartSpot (GameModeBase.cpp:787)
	//             → Player->StartSpot 에 자리를 굳힌다 (:848)
	//   PostLogin: RegisterParticipant → HandleStartingNewPlayer → 폰 스폰
	// 실제 InitNewPlayer/PostLogin 은 NewPlayer->Player(UPlayer)를 요구해 헤드리스에서 부를 수
	// 없으므로, **GameMode 가 하는 일만** 같은 순서로 그대로 태운다.
	//
	// ⚠️ 이 절이 처음에는 Login 단계를 빼먹었고, 그래서 26스위트가 전부 통과하는데도 실전은
	// 깨져 있었다 (2026-08-09). 폰이 안 생기는 것만 봤지 **"어디에" 생기는가**를 정하는 값이
	// 이미 오염돼 있다는 걸 재현하지 않았다. 순서 재현을 줄이면 그만큼 결함이 통과한다.
	ACA3DPlayerController* PC0 = SgSpawnController(World);
	ACA3DPlayerController* PC1 = SgSpawnController(World);
	if (!TestNotNull(TEXT("컨트롤러 0 스폰"), PC0) || !TestNotNull(TEXT("컨트롤러 1 스폰"), PC1))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// (1-a) Login 단계 — 우리 오버라이드는 자리를 **정하지 않는다**.
	FString StartSpotError;
	GameMode->UpdatePlayerStartSpot(PC0, FString(), StartSpotError); // friend
	GameMode->UpdatePlayerStartSpot(PC1, FString(), StartSpotError);
	TestFalse(TEXT("①-a 로그인 단계가 StartSpot 을 굳히지 않는다 (컨트롤러 0)"), PC0->StartSpot.IsValid());
	TestFalse(TEXT("①-a 로그인 단계가 StartSpot 을 굳히지 않는다 (컨트롤러 1)"), PC1->StartSpot.IsValid());
	TestEqual(TEXT("①-a 로그인이 스폰 셀 인덱스를 헛돌리지 않는다"), GameMode->NextSpawnIndex, 0);

	// (1-b) 그래도 굳어 버린 경우를 강제로 만든다 — 엔진 원본 UpdatePlayerStartSpot 이
	// 저장했을 **바로 그 액터**(FindPlayerStart 폴백 = World->GetWorldSettings(), 원점)를 넣는다.
	// 이게 남아 있으면 FindPlayerStart 가 ShouldSpawnAtStartSpot 을 보고 ChoosePlayerStart 를
	// 건너뛴다 (GameModeBase.cpp:1142/1149) — 게이트가 지형을 기다린 의미가 통째로 사라진다.
	// AActor* 로 받는 이유: AWorldSettings 는 AInfo 를 거치며 GetActorLocation 을 private 으로
	// 감춘다("위치가 없는 액터" 라는 뜻이다). 엔진 폴백은 그 사실과 무관하게 이 액터를
	// 돌려주고 폰은 그 좌표(원점)에 스폰된다 — 그게 이 결함의 실체다.
	AActor* OriginActor = World->GetWorldSettings();
	if (TestNotNull(TEXT("①-b 원점 액터(WorldSettings)"), OriginActor))
	{
		TestTrue(TEXT("①-b WorldSettings 는 원점에 있다 (엔진 폴백이 여기로 떨어진다)"),
			OriginActor->GetActorLocation().IsNearlyZero());
		PC0->StartSpot = OriginActor;
		PC1->StartSpot = OriginActor;
	}

	// (1-c) PostLogin 단계.
	GameMode->RegisterParticipant(PC0);      // friend — PostLogin 의 앞부분
	GameMode->HandleStartingNewPlayer(PC0);  // PostLogin 의 뒷부분 (폰 스폰 시도)
	GameMode->RegisterParticipant(PC1);
	GameMode->HandleStartingNewPlayer(PC1);

	TestFalse(TEXT("① 전제: BeginPlay 전이라 그리드가 아직 없다"), VoxelWorld->IsGridInitialized());
	TestNull(TEXT("① 지형 준비 전 로그인 — 컨트롤러 0 에 폰이 생기지 않는다"), PC0->GetPawn());
	TestNull(TEXT("① 지형 준비 전 로그인 — 컨트롤러 1 에 폰이 생기지 않는다"), PC1->GetPawn());
	TestEqual(TEXT("① 월드에 폰이 하나도 없다 (원점 스폰 없음)"), SgCountCharacters(World), 0);
	TestEqual(TEXT("① 두 컨트롤러가 대기 목록에 정확히 한 번씩"),
		GameMode->PendingSpawnControllers.Num(), 2);

	// ─── 2. 참가 등록은 대기 여부와 무관하게 정확하다 ───
	// 색 인덱스 배정이 PostLogin 에서 Super 보다 먼저 도는 근거(헤더 주석)를 흔들지 않았음을 못 박는다.
	TestEqual(TEXT("② 참가 인원 2명"), GameMode->MatchParticipantCount, 2);
	TestEqual(TEXT("② AliveCount 2 (대기 중이어도 살아 있는 참가자다)"), GameState->AliveCount, 2);

	const ACA3DPlayerState* State0 = PC0->GetPlayerState<ACA3DPlayerState>();
	const ACA3DPlayerState* State1 = PC1->GetPlayerState<ACA3DPlayerState>();
	if (TestNotNull(TEXT("② 컨트롤러 0 PlayerState"), State0)
		&& TestNotNull(TEXT("② 컨트롤러 1 PlayerState"), State1))
	{
		TestEqual(TEXT("② ColorIndex 는 접속 순서 그대로 0"), State0->ColorIndex, 0);
		TestEqual(TEXT("② ColorIndex 는 접속 순서 그대로 1"), State1->ColorIndex, 1);
		TestTrue(TEXT("② 대기 중에도 생존 상태"), State0->bAlive && State1->bAlive);
	}

	// 맵 크기 티어 판정이 읽는 값 — 대기 중인 사람도 "접속 인원" 이다.
	TestEqual(TEXT("② GetNumPlayers() 가 대기 중인 사람을 센다 (티어 판정 입력)"),
		GameMode->GetNumPlayers(), 2);

	// ─── 3. 지형 준비 직후 정확히 한 번 스폰된다 ───
	World->BeginPlay(); // → GameMode->StartPlay → (액터 BeginPlay 일괄) → FlushPendingSpawns

	TestTrue(TEXT("③ 그리드 초기화됨"), VoxelWorld->IsGridInitialized());
	TestEqual(TEXT("③ 대기 목록이 비었다"), GameMode->PendingSpawnControllers.Num(), 0);
	// 지역 변수로 받는다 — GetPawn() 반환형을 TestNotNull 의 템플릿 추론에 바로 넘기면
	// 오버로드가 잡히지 않는다 (이 파일의 다른 TestNotNull 은 전부 변수를 넘긴다).
	const APawn* Pawn0 = PC0->GetPawn();
	const APawn* Pawn1 = PC1->GetPawn();
	TestNotNull(TEXT("③ 대기하던 컨트롤러 0 이 폰을 받았다"), Pawn0);
	TestNotNull(TEXT("③ 대기하던 컨트롤러 1 이 폰을 받았다"), Pawn1);
	TestEqual(TEXT("③ 폰은 정확히 2개 — 두 번 스폰되지 않았다"), SgCountCharacters(World), 2);

	// ─── 4. 스폰 위치: 셀 바닥 + 폰 반높이, 발밑이 솔리드 ───
	const TArray<FIntVector>& SpawnCells = VoxelWorld->GetSpawnCells();
	const APawn* PawnCDO = GameMode->DefaultPawnClass
		? GameMode->DefaultPawnClass->GetDefaultObject<APawn>() : nullptr;
	const float HalfHeight = PawnCDO ? PawnCDO->GetDefaultHalfHeight() : 0.f;

	if (TestTrue(TEXT("④ 스폰 셀이 2개 이상"), SpawnCells.Num() >= 2))
	{
		const APlayerController* Ordered[2] = { PC0, PC1 };
		for (int32 i = 0; i < 2; ++i)
		{
			const APawn* Pawn = Ordered[i]->GetPawn();
			if (!Pawn)
			{
				continue;
			}
			const FIntVector Cell = SpawnCells[i]; // 배정 순서 = 로그인 순서
			const FVector Expected = VoxelWorld->CellToWorldFloor(Cell) + FVector(0.f, 0.f, HalfHeight);

			// **원점 배제를 따로 본다.** "셀 바닥 + 반높이" 검사만으로는 이 결함이 통과했다 —
			// 원점 스폰이면 Equals 가 실패하긴 하지만, 실패 메시지가 "위치가 다르다" 라서
			// 원인(굳은 StartSpot)이 안 보였다. 원점을 명시적으로 배제해 증상을 이름 붙인다.
			TestFalse(*FString::Printf(
				TEXT("④ 폰 %d 이 원점(굳은 StartSpot)에 스폰되지 않았다"), i),
				Pawn->GetActorLocation().IsNearlyZero());

			TestTrue(*FString::Printf(TEXT("④ 폰 %d 위치 == 스폰 셀 (%d,%d,%d) 바닥 + 반높이"),
				i, Cell.X, Cell.Y, Cell.Z),
				Pawn->GetActorLocation().Equals(Expected, KINDA_SMALL_NUMBER));

			// FMapValidator 의 스폰 계약(VoxelMove::IsStandable)이 **실제 스폰 위치에서도**
			// 성립하는가: 발밑이 솔리드여야 캡슐이 뜨지도 파묻히지도 않는다.
			TestTrue(*FString::Printf(TEXT("④ 폰 %d 발밑 셀이 솔리드"), i),
				VoxelWorld->IsSolid(Cell + FIntVector(0, 0, -1)));
			TestTrue(*FString::Printf(TEXT("④ 폰 %d 스폰 셀 자체는 비어 있다"), i),
				VoxelWorld->GetBlock(Cell) == EBlockType::Empty);
			// 캡슐 전고(반높이 × 2 = 176)가 셀 하나(100)보다 커서 머리가 윗 칸으로 들어간다 —
			// 윗 칸이 막혀 있으면 캡슐이 블록에 파묻힌다. 검증기는 머리 공간을 보지 않으므로
			// (VoxelMove::FMoveCaps — 봇 전용) 실제 스폰 위치에서 여기서 확인한다.
			TestTrue(*FString::Printf(TEXT("④ 폰 %d 머리 칸이 비어 있다 (캡슐 파묻힘 없음)"), i),
				!VoxelWorld->IsSolid(Cell + FIntVector(0, 0, 1)));
		}
		if (PC0->GetPawn() && PC1->GetPawn())
		{
			TestTrue(TEXT("④ 두 폰의 스폰 위치가 서로 다르다"),
				!PC0->GetPawn()->GetActorLocation().Equals(
					PC1->GetPawn()->GetActorLocation(), KINDA_SMALL_NUMBER));
		}
	}

	// (1-b) 에서 손으로 굳혀 둔 값을 되돌린다 — 아래 ⑧ 의 "정상 흐름에서는 StartSpot 이
	// 비어 있다" 전제 검사가 이 테스트가 만든 값에 가려지면 안 된다.
	PC0->StartSpot.Reset();
	PC1->StartSpot.Reset();

	// ─── 5. 정확히 한 번 — 해소를 다시 돌려도 폰이 늘지 않는다 ───
	// 두 번 스폰되면 폰이 둘 남거나 AliveCount 가 어긋나 승패 판정이 조용히 깨진다.
	GameMode->FlushPendingSpawns();
	TestEqual(TEXT("⑤ 해소 재호출 — 폰 개수 불변"), SgCountCharacters(World), 2);

	GameMode->PendingSpawnControllers.Add(PC0); // 어떤 경로로든 다시 큐에 들어간 경우
	GameMode->FlushPendingSpawns();
	TestEqual(TEXT("⑤ 이미 폰이 있는 컨트롤러는 다시 스폰하지 않는다"), SgCountCharacters(World), 2);
	TestEqual(TEXT("⑤ 참가 인원도 그대로"), GameMode->MatchParticipantCount, 2);

	// ─── 6. 지형 준비 후 로그인은 그 자리에서 스폰된다 (데디 서버 경로) ───
	// 사람이 BeginPlay 이후에 접속하는 경우 — 게이트가 이미 열려 있으니 대기가 없다.
	ACA3DPlayerController* PC2 = SgSpawnController(World);
	if (TestNotNull(TEXT("⑥ 늦게 접속한 컨트롤러 스폰"), PC2))
	{
		// 이때는 SpawnCells 가 이미 차 있다. 엔진 원본이라면 Login 의 UpdatePlayerStartSpot 이
		// ChoosePlayerStart 를 한 번 돌려 **아무도 안 쓰는 셀을 한 칸 태워 버린다** — 1인당 2칸을
		// 먹으므로 셀 8개에 사람이 5명 이상이면 인덱스가 되감겨 두 명이 같은 칸에 겹쳐 스폰된다
		// (SpawnStartActors 캐시가 같은 액터를 돌려주므로 좌표가 완전히 같아진다).
		const int32 IndexBeforeLogin = GameMode->NextSpawnIndex; // friend
		GameMode->UpdatePlayerStartSpot(PC2, FString(), StartSpotError);
		TestEqual(TEXT("⑥ 늦은 로그인도 스폰 셀 인덱스를 태우지 않는다"),
			GameMode->NextSpawnIndex, IndexBeforeLogin);
		TestFalse(TEXT("⑥ 늦은 로그인도 StartSpot 을 굳히지 않는다"), PC2->StartSpot.IsValid());

		GameMode->RegisterParticipant(PC2);
		GameMode->HandleStartingNewPlayer(PC2);

		TestEqual(TEXT("⑥ 대기 목록에 들어가지 않는다"), GameMode->PendingSpawnControllers.Num(), 0);
		const APawn* Pawn2 = PC2->GetPawn();
		TestNotNull(TEXT("⑥ 그 자리에서 폰을 받는다"), Pawn2);
		TestEqual(TEXT("⑥ 폰 3개"), SgCountCharacters(World), 3);

		if (Pawn2 && SpawnCells.Num() >= 3)
		{
			// 인덱스를 태우지 않았으므로 세 번째 사람은 **세 번째** 셀을 받는다.
			const FVector Expected =
				VoxelWorld->CellToWorldFloor(SpawnCells[2]) + FVector(0.f, 0.f, HalfHeight);
			TestTrue(TEXT("⑥ 세 번째 사람은 세 번째 스폰 셀 (인덱스 건너뛰기 없음)"),
				Pawn2->GetActorLocation().Equals(Expected, KINDA_SMALL_NUMBER));
		}
	}

	// ─── 7. 봇 채우기는 대기했던 사람까지 포함해 센다 ───
	InjectedRules->bFillWithBots = true;
	InjectedRules->BotFillTargetPlayers = 5;
	GameMode->SpawnFillBots(); // friend
	TestEqual(TEXT("⑦ 사람 3명 + 봇 2대 = 목표 5명"), GameMode->MatchParticipantCount, 5);
	TestEqual(TEXT("⑦ 폰 5개 (사람 3 + 봇 2)"), SgCountCharacters(World), 5);
	if (GameState)
	{
		TestEqual(TEXT("⑦ AliveCount 5"), GameState->AliveCount, 5);
	}

	// ─── 8. 굳은 StartSpot 은 어떤 경로로 채워지든 재사용하지 않는다 (부활 경로 대비) ───
	// 스폰 셀 인덱스를 한 칸 소비하므로 다른 절의 배정 검사에 영향이 없도록 **맨 뒤**에 둔다.
	//
	// UE 5.8 에서 Player->StartSpot 을 채우는 곳은 UpdatePlayerStartSpot 한 곳뿐이고
	// (GameModeBase.cpp:848 — 스폰 후 훅인 InitStartSpot_Implementation 은 :1388 에서 비어 있다),
	// 우리는 그걸 막았다. 아래 첫 검사가 그 전제를 명시한다 — **전제가 깨지면 여기서 먼저 알게 된다.**
	// 그리고 전제가 깨지더라도(BP 가 InitStartSpot 을 구현, 부활 경로 추가, 엔진 갱신) 배정이
	// 우회되지 않는다는 것을 ShouldSpawnAtStartSpot 오버라이드로 보장한다.
	TestFalse(TEXT("⑧ 정상 스폰 후에도 StartSpot 은 비어 있다 (UE 5.8 전제)"), PC0->StartSpot.IsValid());

	if (OriginActor)
	{
		PC0->StartSpot = OriginActor; // 굳은 상태를 강제로 재현
		TestFalse(TEXT("⑧ 굳어 있어도 재사용하지 않는다"),
			GameMode->ShouldSpawnAtStartSpot(PC0)); // friend

		// 엔진 실제 경로로 확인 — FindPlayerStart 가 StartSpot 을 건너뛰고 ChoosePlayerStart 로 간다.
		const AActor* Resolved = GameMode->FindPlayerStart(PC0);
		TestTrue(TEXT("⑧ FindPlayerStart 가 굳은 원점 액터를 돌려주지 않는다"),
			Resolved != static_cast<const AActor*>(OriginActor));
		if (Resolved)
		{
			TestFalse(TEXT("⑧ 돌려준 자리는 원점이 아니다 (스폰 셀)"),
				Resolved->GetActorLocation().IsNearlyZero());
		}
		PC0->StartSpot.Reset();
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
