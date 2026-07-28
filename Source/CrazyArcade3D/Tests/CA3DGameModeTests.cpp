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
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
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

#endif // WITH_AUTOMATION_TESTS
