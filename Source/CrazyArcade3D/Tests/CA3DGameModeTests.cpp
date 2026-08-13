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

// ─────────────────────────────────────────────────────────────────────────────
// 캐릭터 선택 페이즈 (Task 36) — 선착순 중복 불가·재선택 자연 해제·종료 시 자동 배정·
// 봇 포함 전원 고유·스킵 경로 무회귀·종료 후 요청 거부·고정 시드 재현.
//
// 페이즈 종료는 타이머(CharacterSelectDuration) 만료가 부르지만, 여기서는 friend 로
// EndCharacterSelect 를 직접 불러 결정론적으로 진행한다 (PsResolveDeathsNow 와 같은 계열 —
// 실시간 대기는 자동화 테스트를 느리고 불안정하게 만든다).
// 실제 리플리케이션(클라 위젯이 같은 값을 보는가)·커서 전이는 PIE 검증.
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DCharacterSelectTest, "CrazyArcade3D.Framework.CharacterSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 — 접두사 Cs~.

	// 시나리오별 월드 한 벌 — friend 접근이 필요한 구성(BuildWorld 람다)은 RunTest 본문에 있다.
	struct FCsWorldFixture
	{
		UWorld* World = nullptr;
		ACA3DGameMode* GameMode = nullptr;
		ACA3DGameState* GameState = nullptr;
		UCA3DRuleSet* Rules = nullptr;

		void Destroy()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};

	// PlayerArray 의 CharacterIndex 를 고정 순서로 수집 — 전원 배정·전원 고유·재현 검사용.
	TArray<int32> CsCollectIndices(const ACA3DGameState* GameState)
	{
		TArray<int32> Out;
		for (APlayerState* Each : GameState->PlayerArray)
		{
			if (const ACA3DPlayerState* State = Cast<ACA3DPlayerState>(Each))
			{
				Out.Add(State->CharacterIndex);
			}
		}
		return Out;
	}
}

bool FCA3DCharacterSelectTest::RunTest(const FString& Parameters)
{
	// ─── 구성 람다 (friend 접근이 필요해 무명 네임스페이스가 아니라 본문에 둔다 —
	//     람다는 이 friend 멤버 함수의 접근 권한을 그대로 갖는다) ───

	// 표준 월드 구성 (위 두 테스트와 같은 순서). 캐릭터 정의는 데이터만 채운다 — 이번 Task 는
	// 데이터 정의까지가 범위라 메시·애님 에셋 없이도 배정 로직 전부가 검증 가능해야 한다.
	auto BuildWorld = [](int32 CharacterCount, float SelectDuration, int32 Seed) -> FCsWorldFixture
	{
		FCsWorldFixture Out;

		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		GameInstance->InitializeStandalone();
		Out.World = GameInstance->GetWorld();
		if (!Out.World)
		{
			return Out;
		}

		Out.World->GetWorldSettings()->DefaultGameMode = ACA3DGameMode::StaticClass();

		FURL URL;
		Out.World->SetGameMode(URL);
		Out.GameMode = Out.World->GetAuthGameMode<ACA3DGameMode>();
		if (!Out.GameMode)
		{
			return Out;
		}

		Out.World->SpawnActor<AVoxelWorld>(); // 레벨 배치 액터 역할
		Out.World->InitializeActorsForPlay(URL);

		Out.Rules = NewObject<UCA3DRuleSet>(Out.GameMode);
		for (int32 i = 0; i < CharacterCount; ++i)
		{
			FCA3DCharacterDef& Def = Out.Rules->Characters.AddDefaulted_GetRef();
			Def.DisplayName = FText::FromString(FString::Printf(TEXT("Char %d"), i));
		}
		Out.Rules->CharacterSelectDuration = SelectDuration;
		Out.GameMode->Rules = Out.Rules; // friend — BP(DA_Rules_Default) 대신 주입
		Out.GameMode->bUseFixedSeed = true;
		Out.GameMode->FixedSeed = Seed;

		Out.World->BeginPlay(); // → StartPlay → GameMode::BeginPlay (페이즈 판가름 + 게이트)
		Out.GameState = Out.World->GetGameState<ACA3DGameState>();
		return Out;
	};

	// 접속 절차 재현 — SpawnGate 테스트와 같은 순서 (Login 의 UpdatePlayerStartSpot 은
	// 우리 오버라이드가 no-op 이므로 생략).
	auto Login = [](FCsWorldFixture& F) -> ACA3DPlayerController*
	{
		ACA3DPlayerController* PC = SgSpawnController(F.World);
		if (PC)
		{
			F.GameMode->RegisterParticipant(PC);     // friend — PostLogin 앞부분
			F.GameMode->HandleStartingNewPlayer(PC); // PostLogin 뒷부분 (페이즈 중이면 보류)
		}
		return PC;
	};

	// ─── 시나리오 A: 페이즈 진행 — 선착순·재선택·종료 자동 배정·봇 포함 8인 고유 ───
	{
		FCsWorldFixture A = BuildWorld(/*Characters*/ 8, /*Duration*/ 10.f, /*Seed*/ 777);
		if (!TestNotNull(TEXT("A: 월드"), A.World) || !TestNotNull(TEXT("A: GameMode"), A.GameMode)
			|| !TestNotNull(TEXT("A: GameState"), A.GameState))
		{
			A.Destroy();
			return false;
		}

		// ─ 0. 페이즈 개시 상태 ─
		TestTrue(TEXT("⓪ 페이즈 활성 플래그"), A.GameState->bCharacterSelectActive);
		TestTrue(TEXT("⓪ 종료 시각이 미래"),
			A.GameState->CharacterSelectEndServerTime > A.GameState->GetServerWorldTimeSeconds());
		TestEqual(TEXT("⓪ MatchStartServerTime == 예상 시작 시각(페이즈 종료 시각)"),
			A.GameState->MatchStartServerTime, A.GameState->CharacterSelectEndServerTime);
		TestFalse(TEXT("⓪ 봇 타이머는 페이즈 종료로 미룸 (BeginPlay 예약 없음)"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->BotFillTimer));
		TestFalse(TEXT("⓪ 서든데스 타이머도 페이즈 종료로 미룸"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->SuddenDeathTimer));

		// 사람 3명 입장 — 페이즈 중에는 폰을 받지 않는다 (스폰 게이트의 페이즈 연장).
		ACA3DPlayerController* PC0 = Login(A);
		ACA3DPlayerController* PC1 = Login(A);
		ACA3DPlayerController* PC2 = Login(A);
		if (!TestNotNull(TEXT("⓪ PC0"), PC0) || !TestNotNull(TEXT("⓪ PC1"), PC1)
			|| !TestNotNull(TEXT("⓪ PC2"), PC2))
		{
			A.Destroy();
			return false;
		}
		TestNull(TEXT("⓪ 페이즈 중 폰 없음"), PC0->GetPawn());
		TestEqual(TEXT("⓪ 3명 전원 스폰 보류"), A.GameMode->PendingSpawnControllers.Num(), 3);

		ACA3DPlayerState* PS0 = PC0->GetPlayerState<ACA3DPlayerState>();
		ACA3DPlayerState* PS1 = PC1->GetPlayerState<ACA3DPlayerState>();
		ACA3DPlayerState* PS2 = PC2->GetPlayerState<ACA3DPlayerState>();
		if (!TestNotNull(TEXT("⓪ PS0"), PS0) || !TestNotNull(TEXT("⓪ PS1"), PS1)
			|| !TestNotNull(TEXT("⓪ PS2"), PS2))
		{
			A.Destroy();
			return false;
		}
		TestEqual(TEXT("⓪ 초기값 미선택(INDEX_NONE)"), PS0->CharacterIndex, static_cast<int32>(INDEX_NONE));

		// ─ 1. 선착순 — 같은 캐릭터 중복 선택 거부 ─
		TestTrue(TEXT("① 첫 선택 성공"), A.GameMode->TryAssignCharacter(PS0, 2));
		TestEqual(TEXT("① 선택 반영"), PS0->CharacterIndex, 2);
		TestFalse(TEXT("① 같은 캐릭터 요청 거부 (선착순)"), A.GameMode->TryAssignCharacter(PS1, 2));
		TestEqual(TEXT("① 거부된 쪽은 미선택 유지"), PS1->CharacterIndex, static_cast<int32>(INDEX_NONE));
		// 클라 입력은 신뢰하지 않는다 — 범위 밖 인덱스 거부.
		TestFalse(TEXT("① 범위 밖(8) 거부"), A.GameMode->TryAssignCharacter(PS1, 8));
		TestFalse(TEXT("① 음수(-5) 거부"), A.GameMode->TryAssignCharacter(PS1, -5));
		TestFalse(TEXT("① null PlayerState 거부"), A.GameMode->TryAssignCharacter(nullptr, 0));

		// ─ 2. 재선택 — 이전 선택 자연 해제 ─
		TestTrue(TEXT("② 재선택 성공"), A.GameMode->TryAssignCharacter(PS0, 5));
		TestEqual(TEXT("② 새 값 반영"), PS0->CharacterIndex, 5);
		TestTrue(TEXT("② 비워진 이전 캐릭터를 다른 사람이 가져간다 (자연 해제)"),
			A.GameMode->TryAssignCharacter(PS1, 2));
		TestTrue(TEXT("② 자기 값 재요청은 허용 (점유 스캔이 자신을 건너뜀)"),
			A.GameMode->TryAssignCharacter(PS0, 5));

		// ─ 3+4. 종료: 미선택자 자동 배정 + 봇 채우기 → 8인 전원 고유 ─
		A.Rules->bFillWithBots = true;
		A.Rules->BotFillTargetPlayers = 8;
		A.GameMode->EndCharacterSelect(); // friend — 타이머 만료를 직접 재현

		TestFalse(TEXT("③ 페이즈 플래그 해제"), A.GameState->bCharacterSelectActive);
		TestEqual(TEXT("③ 참가 8명 (사람 3 + 봇 5)"), A.GameMode->MatchParticipantCount, 8);
		TestEqual(TEXT("③ 수동 선택 보존 (PS0=5 — 자동 배정이 덮지 않는다)"), PS0->CharacterIndex, 5);
		TestEqual(TEXT("③ 수동 선택 보존 (PS1=2)"), PS1->CharacterIndex, 2);

		{
			const TArray<int32> Indices = CsCollectIndices(A.GameState);
			TestEqual(TEXT("④ PlayerState 8개"), Indices.Num(), 8);
			TSet<int32> Unique;
			for (int32 EachIndex : Indices)
			{
				TestTrue(TEXT("③ 전원 배정 (미선택자·봇 포함 INDEX_NONE 없음)"), EachIndex != INDEX_NONE);
				TestTrue(TEXT("③ 전원 유효 범위 [0,8)"), EachIndex >= 0 && EachIndex < 8);
				Unique.Add(EachIndex);
			}
			TestEqual(TEXT("④ 봇 포함 8인 전원 고유 인덱스"), Unique.Num(), Indices.Num());
		}

		// 종료가 보류 스폰을 해소하고 시작 시각·서든데스를 확정한다.
		TestEqual(TEXT("③ 보류 목록 비움"), A.GameMode->PendingSpawnControllers.Num(), 0);
		const APawn* Pawn0 = PC0->GetPawn();
		TestNotNull(TEXT("③ 종료 후 사람 폰 스폰"), Pawn0);
		TestEqual(TEXT("③ 폰 8개 (사람 3 + 봇 5)"), SgCountCharacters(A.World), 8);
		TestTrue(TEXT("③ MatchStartServerTime 을 실제 시각으로 재기록 (예상 시각보다 앞)"),
			A.GameState->MatchStartServerTime < A.GameState->CharacterSelectEndServerTime);
		TestTrue(TEXT("③ 서든데스 타이머 예약 (페이즈 종료 후)"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->SuddenDeathTimer));

		// ─ 6. 페이즈 종료 후 사람의 선택 요청 거부 ─
		TestFalse(TEXT("⑥ 종료 후 변경 요청 거부"),
			A.GameMode->TryAssignCharacter(PS0, PS1->CharacterIndex));
		TestFalse(TEXT("⑥ 종료 후 자기 값 재요청도 거부 (게이트가 점유보다 먼저)"),
			A.GameMode->TryAssignCharacter(PS0, 5));
		TestEqual(TEXT("⑥ 값 불변"), PS0->CharacterIndex, 5);

		// 종료 중복 호출 무해 — 봇·시각·폰이 두 번 처리되지 않는다.
		A.GameMode->EndCharacterSelect();
		TestEqual(TEXT("③ 종료 재호출 — 참가 인원 불변"), A.GameMode->MatchParticipantCount, 8);
		TestEqual(TEXT("③ 종료 재호출 — 폰 불변"), SgCountCharacters(A.World), 8);

		A.Destroy();
	}

	// ─── 시나리오 B: 종료 후 요청 거부(풀이 남아 있어도) + 늦은 입장 자동 배정 ───
	{
		FCsWorldFixture B = BuildWorld(8, 10.f, 777);
		if (!TestNotNull(TEXT("B: GameMode"), B.GameMode) || !TestNotNull(TEXT("B: GameState"), B.GameState))
		{
			B.Destroy();
			return false;
		}

		ACA3DPlayerController* BPC0 = Login(B);
		ACA3DPlayerController* BPC1 = Login(B);
		ACA3DPlayerState* BPS0 = BPC0 ? BPC0->GetPlayerState<ACA3DPlayerState>() : nullptr;
		ACA3DPlayerState* BPS1 = BPC1 ? BPC1->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (!TestNotNull(TEXT("B: PS0"), BPS0) || !TestNotNull(TEXT("B: PS1"), BPS1))
		{
			B.Destroy();
			return false;
		}

		B.GameMode->EndCharacterSelect(); // 봇 없음(룰셋 기본 꺼짐) — 2인만 자동 배정

		TestTrue(TEXT("③b 종료 시 전원 자동 배정"),
			BPS0->CharacterIndex != INDEX_NONE && BPS1->CharacterIndex != INDEX_NONE);
		TestTrue(TEXT("③b 자동 배정 서로 고유"), BPS0->CharacterIndex != BPS1->CharacterIndex);

		// 아직 아무도 안 쓰는 인덱스 확보 — 종료 후 거부의 사유가 점유가 아니라 **게이트**임을 못 박는다.
		int32 FreeIndex = INDEX_NONE;
		for (int32 i = 0; i < 8; ++i)
		{
			if (i != BPS0->CharacterIndex && i != BPS1->CharacterIndex)
			{
				FreeIndex = i;
				break;
			}
		}
		TestFalse(TEXT("⑥ 풀이 남아 있어도 종료 후 사람 요청 거부"),
			B.GameMode->TryAssignCharacter(BPS0, FreeIndex));

		// 페이즈 종료 후 늦은 입장 — RegisterParticipant 가 남은 풀에서 즉시 배정 + 즉시 스폰.
		ACA3DPlayerController* BPC2 = Login(B);
		ACA3DPlayerState* BPS2 = BPC2 ? BPC2->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (TestNotNull(TEXT("늦은 입장 PS"), BPS2))
		{
			TestTrue(TEXT("늦은 입장 자동 배정"), BPS2->CharacterIndex != INDEX_NONE);
			TestTrue(TEXT("늦은 입장 배정도 고유"),
				BPS2->CharacterIndex != BPS0->CharacterIndex
				&& BPS2->CharacterIndex != BPS1->CharacterIndex);
			const APawn* LatePawn = BPC2->GetPawn();
			TestNotNull(TEXT("늦은 입장 즉시 스폰 (보류 없음)"), LatePawn);
		}

		B.Destroy();
	}

	// ─── 시나리오 C: ⑤ Duration == 0 — 페이즈 없음, 기존 흐름 그대로 ───
	{
		FCsWorldFixture C = BuildWorld(8, 0.f, 777);
		if (!TestNotNull(TEXT("C: GameMode"), C.GameMode) || !TestNotNull(TEXT("C: GameState"), C.GameState))
		{
			C.Destroy();
			return false;
		}

		TestFalse(TEXT("⑤ 페이즈 플래그가 서지 않는다"), C.GameState->bCharacterSelectActive);
		TestEqual(TEXT("⑤ 종료 시각 미기록"), C.GameState->CharacterSelectEndServerTime, 0.f);
		TestTrue(TEXT("⑤ 봇 채우기 타이머는 기존 경로대로 BeginPlay 가 예약"),
			C.GameMode->GetWorldTimerManager().IsTimerActive(C.GameMode->BotFillTimer));
		TestTrue(TEXT("⑤ 서든데스 타이머도 기존 경로대로"),
			C.GameMode->GetWorldTimerManager().IsTimerActive(C.GameMode->SuddenDeathTimer));

		ACA3DPlayerController* CPC0 = Login(C);
		if (TestNotNull(TEXT("⑤ 접속"), CPC0))
		{
			const APawn* CPawn0 = CPC0->GetPawn();
			TestNotNull(TEXT("⑤ 즉시 스폰 (기존 흐름 — 보류 없음)"), CPawn0);

			// 페이즈 없는 매치는 배정 자체가 없다 — 클라가 RPC 를 보내도 복제 값이 안 바뀐다.
			ACA3DPlayerState* CPS0 = CPC0->GetPlayerState<ACA3DPlayerState>();
			if (TestNotNull(TEXT("⑤ PS"), CPS0))
			{
				TestFalse(TEXT("⑤ 페이즈 없는 매치의 선택 요청 거부"), C.GameMode->TryAssignCharacter(CPS0, 0));
				TestEqual(TEXT("⑤ CharacterIndex 미배정 유지"),
					CPS0->CharacterIndex, static_cast<int32>(INDEX_NONE));
			}
		}

		// 잘못된 종료 호출도 무해 — 플래그가 없어 즉시 반환 (시작 시각·타이머 안 건드림).
		const float MatchStartBefore = C.GameState->MatchStartServerTime;
		C.GameMode->EndCharacterSelect();
		TestEqual(TEXT("⑤ 잘못된 종료 호출 — MatchStartServerTime 불변"),
			C.GameState->MatchStartServerTime, MatchStartBefore);

		C.Destroy();
	}

	// ─── 시나리오 D: ⑦ Characters 비어 있음 — 페이즈 스킵 + 경고 1회, 크래시 없음 ───
	{
		AddExpectedMessagePlain(TEXT("Characters 가 비어 있음"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1);

		FCsWorldFixture D = BuildWorld(/*Characters*/ 0, /*Duration*/ 10.f, 777);
		if (!TestNotNull(TEXT("D: GameMode"), D.GameMode) || !TestNotNull(TEXT("D: GameState"), D.GameState))
		{
			D.Destroy();
			return false;
		}

		TestFalse(TEXT("⑦ Characters 비어 있음 — 페이즈 스킵"), D.GameState->bCharacterSelectActive);
		TestTrue(TEXT("⑦ 스킵 시 기존 경로 타이머 (서든데스)"),
			D.GameMode->GetWorldTimerManager().IsTimerActive(D.GameMode->SuddenDeathTimer));

		ACA3DPlayerController* DPC0 = Login(D);
		if (TestNotNull(TEXT("⑦ 접속"), DPC0))
		{
			const APawn* DPawn0 = DPC0->GetPawn();
			TestNotNull(TEXT("⑦ 즉시 스폰 — 크래시 없음"), DPawn0);
		}

		D.Destroy();
	}

	// ─── 시나리오 E: ⑧ 고정 시드 자동 배정 재현 — 같은 구성 두 번 = 같은 결과 ───
	{
		TArray<int32> RunResults[2];
		for (int32 Run = 0; Run < 2; ++Run)
		{
			FCsWorldFixture E = BuildWorld(8, 10.f, /*Seed*/ 1234);
			if (!TestNotNull(TEXT("E: GameMode"), E.GameMode) || !TestNotNull(TEXT("E: GameState"), E.GameState))
			{
				E.Destroy();
				return false;
			}
			Login(E);
			Login(E);
			Login(E);
			E.GameMode->EndCharacterSelect(); // 아무도 안 고름 — 3명 전원 자동 배정
			RunResults[Run] = CsCollectIndices(E.GameState);
			E.Destroy();
		}

		TestEqual(TEXT("⑧ 두 실행의 참가 수 동일"), RunResults[0].Num(), RunResults[1].Num());
		TestEqual(TEXT("⑧ 두 실행 모두 3명"), RunResults[0].Num(), 3);
		TestTrue(TEXT("⑧ 고정 시드 자동 배정 재현 — 두 실행의 배정이 완전히 동일"),
			RunResults[0] == RunResults[1]);
		for (int32 EachIndex : RunResults[0])
		{
			TestTrue(TEXT("⑧ 전원 배정됨"), EachIndex != INDEX_NONE);
		}
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
