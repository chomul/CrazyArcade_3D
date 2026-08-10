// 중도 이탈(Logout) 자동화 테스트 (2026-08-10 사용자 확정 규칙).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Framework.MatchLeave" 로 실행.
//
// 규칙: **나간 사람은 그 자리에서 사망 처리해 순위를 부여하고, 결과 화면에 "탈주" 로 표시한다.**
// 참가 인원(MatchParticipantCount)에서는 빼지 않는다 — 그 사람은 매치에 참가했고 자리를 차지했다.
//
// 이 스위트가 못 박는 경계 (하나라도 깨지면 매치가 영영 안 끝나거나 결과 화면에서 사람이 증발한다):
//   ① 살아 있는 사람이 나가면 등수 부여 + AliveCount 감소
//   ② **폰이 생기기 전에 나가도** AliveCount 가 준다  ← 이 Task 의 핵심 결함 경로
//   ③ 이미 죽은 사람이 나가도 AliveCount 가 두 번 줄지 않는다
//   ④ 3명 중 2명이 나가면 남은 1명이 우승
//   ⑤ 전원이 나가면 무승부 (기존 규약 유지)
//   ⑥ 스폰 대기 목록에서 빠진다
//   ⑦ OnDeactivated 가 PlayerState 를 파괴하지 않는다 (= 결과 화면이 그 사람을 계속 본다)
//   ⑧ 표시: 탈주 표식이 붙고 등수는 그대로, 정렬·공동등수는 탈주 유무와 무관
//
// 월드 구성은 CA3DPlayerStateTests / CA3DGameModeTests 관례를 그대로 따른다
// (InitializeStandalone → SetGameMode → InitializeActorsForPlay, World->BeginPlay 는 부르지 않는다 —
//  레벨에 AVoxelWorld 가 없어 GameMode::BeginPlay 가 에러 로그를 남긴다).
// 사망 해소는 SetTimerForNextTick 이라 타이머 매니저를 수동 Tick 한다 (PsResolveDeathsNow 와 같은 방식).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Ml~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Gameplay/Character/StatusComponent.h"
#include "UI/MatchWidget.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchLeaveTest, "CrazyArcade3D.Framework.MatchLeave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// GameMode 가 예약한 "다음 틱" 사망 해소를 강제 실행한다 (PsResolveDeathsNow 와 같은 근거:
	// FTimerManager 는 틱 밖 등록분을 Pending 에 뒀다가 틱 말미에 활성화하므로 두 번 필요하고,
	// 같은 GFrameCounter 프레임의 중복 Tick 은 무시되므로 사이에 프레임을 진행시킨다).
	void MlResolveDeathsNow(UWorld* World)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);
	}

	ACA3DPlayerController* MlSpawnController(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACA3DPlayerController>(
			ACA3DPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	// 폰은 서로 떨어뜨려 스폰한다 — 겹쳐 두면 접촉 판정(갇힘 터뜨리기)이 끼어들 여지가 생긴다.
	ACA3DCharacter* MlSpawnCharacter(UWorld* World, int32 Index)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(),
			FVector(Index * 500.f, 0.f, 0.f), FRotator::ZeroRotator, Params);
	}

	FMatchResultRow MlMakeRow(int32 Rank, const TCHAR* Name, bool bLeft)
	{
		FMatchResultRow Row;
		Row.Rank = Rank;
		Row.PlayerName = Name;
		Row.bLeft = bLeft;
		return Row;
	}
}

bool FMatchLeaveTest::RunTest(const FString& Parameters)
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

	World->InitializeActorsForPlay(URL); // GameState 는 이 이후에야 존재한다

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("GameState 가 ACA3DGameState"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// friend 접근: BP(DA_Rules_Default) 대신 룰셋 주입 (MinPlayersForMatchEnd 기본 2).
	UCA3DRuleSet* InjectedRules = NewObject<UCA3DRuleSet>(GameMode);
	GameMode->Rules = InjectedRules;

	// ─── 매치 구성 헬퍼 ───
	// 실제 접속(넷 커넥션)은 헤드리스에서 재현할 수 없으므로, PostLogin 이 하는 일
	// (RegisterParticipant)만 직접 태우고 이탈은 Logout 을 직접 부른다 — 엔진이
	// AController::Destroyed 에서 부르는 바로 그 함수다 (Controller.cpp:603).
	TArray<ACA3DPlayerController*> Controllers;
	TArray<ACA3DPlayerState*> States;
	TArray<ACA3DCharacter*> Pawns;

	auto ResetMatch = [&](int32 Count, bool bWithPawns)
	{
		// ⚠️ 정리 중 컨트롤러 파괴는 AController::Destroyed → GameMode->Logout 을 **다시** 태운다.
		// 종료 플래그를 잠시 세워 그 경로가 아무 일도 하지 않게 만든다 (테스트 전용 조작).
		GameState->bMatchEnded = true;

		for (ACA3DCharacter* Each : Pawns)      { if (IsValid(Each)) { Each->Destroy(); } }
		Pawns.Reset();
		for (ACA3DPlayerController* Each : Controllers) { if (IsValid(Each)) { Each->Destroy(); } }
		Controllers.Reset();
		// PlayerState 는 OnDeactivated 오버라이드 때문에 컨트롤러와 함께 사라지지 않는다
		// (그게 이 Task 의 핵심이다) — 시나리오 간 오염을 막으려면 여기서 명시적으로 파괴한다.
		for (ACA3DPlayerState* Each : States)   { if (IsValid(Each)) { Each->Destroy(); } }
		States.Reset();

		GameMode->PendingDeaths.Reset();
		GameMode->PendingSpawnControllers.Reset();
		GameMode->bDeathResolveScheduled = false;
		GameMode->MatchParticipantCount = 0;
		GameState->AliveCount = 0;
		GameState->MatchWinner = nullptr;
		GameState->bMatchEnded = false;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			ACA3DPlayerController* PC = MlSpawnController(World);
			if (!PC)
			{
				continue;
			}
			Controllers.Add(PC);

			if (bWithPawns)
			{
				if (ACA3DCharacter* Pawn = MlSpawnCharacter(World, Index))
				{
					PC->Possess(Pawn); // 폰의 PlayerState 배선까지 엔진 경로 그대로
					Pawns.Add(Pawn);
				}
			}

			GameMode->RegisterParticipant(PC); // friend — PostLogin 의 등록부
			States.Add(PC->GetPlayerState<ACA3DPlayerState>());
		}
	};

	// ─── ① 살아 있는 사람이 나가면 등수 부여 + AliveCount 감소 ───
	ResetMatch(3, /*bWithPawns*/true);
	if (!TestEqual(TEXT("① 3인 구성 (컨트롤러)"), Controllers.Num(), 3)
		|| !TestEqual(TEXT("① 3인 구성 (폰)"), Pawns.Num(), 3)
		|| !TestEqual(TEXT("① 3인 구성 (PlayerState)"), States.Num(), 3))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("① 전제: AliveCount 3"), GameState->AliveCount, 3);
	TestFalse(TEXT("① 전제: 아직 탈주자 없음"), States[0]->bLeftMatch);

	GameMode->Logout(Controllers[0]); // 엔진이 AController::Destroyed 에서 부르는 그 함수

	TestTrue(TEXT("① 이탈 즉시 탈주 표시"), States[0]->bLeftMatch);
	TestEqual(TEXT("① 사망 통지가 버퍼에 1건 (기존 사망 경로와 같은 배칭)"), GameMode->PendingDeaths.Num(), 1);
	if (Pawns.Num() == 3)
	{
		const UStatusComponent* Status0 = Pawns[0]->GetStatus();
		if (TestNotNull(TEXT("① StatusComponent 존재"), Status0))
		{
			TestEqual(TEXT("① 기존 사망 경로를 그대로 탔다 — LifeState == Dead"),
				Status0->LifeState, ELifeState::Dead);
			TestEqual(TEXT("① 사인이 Left 로 기록"), Status0->LastDeathCause, EDeathCause::Left);
			TestTrue(TEXT("① 폰 숨김까지 기존 경로가 처리 (ApplyDeathState)"), Pawns[0]->IsHidden());
		}
	}

	MlResolveDeathsNow(World);
	TestEqual(TEXT("① 해소 후 FinalRank 3 (3인 중 첫 탈락)"), States[0]->FinalRank, 3);
	TestFalse(TEXT("① 해소 후 bAlive false"), States[0]->bAlive);
	TestTrue(TEXT("① 탈주 표시 유지"), States[0]->bLeftMatch);
	TestEqual(TEXT("① AliveCount 3 → 2"), GameState->AliveCount, 2);
	TestFalse(TEXT("① 아직 매치 진행 중"), GameState->bMatchEnded);
	// 참가 인원은 되돌리지 않는다 — 줄이면 MinPlayersForMatchEnd 게이트가 소급 적용된다.
	TestEqual(TEXT("① MatchParticipantCount 는 3 유지 (나가도 참가는 사실이다)"),
		GameMode->MatchParticipantCount, 3);
	// 남은 사람은 아무 영향도 받지 않는다.
	TestTrue(TEXT("① 남은 사람 생존 유지"), States[1]->bAlive && States[2]->bAlive);
	TestFalse(TEXT("① 남은 사람에 탈주 표시 없음"), States[1]->bLeftMatch || States[2]->bLeftMatch);

	// ─── ② 폰이 없는(스폰 전) 사람이 나가도 AliveCount 가 준다 ← 핵심 결함 경로 ───
	// 지형이 준비되기 전에 접속하면 폰이 없다(스폰 게이트). 그 상태로 나갔을 때 통지를
	// 빠뜨리면 AliveCount 가 영영 안 줄어 남은 사람이 아무리 죽어도 매치가 끝나지 않는다.
	ResetMatch(3, /*bWithPawns*/false);
	TestEqual(TEXT("② 전제: AliveCount 3"), GameState->AliveCount, 3);
	TestNull(TEXT("② 전제: 폰이 없다"), Controllers[0]->GetPawn());

	GameMode->Logout(Controllers[0]);
	TestTrue(TEXT("② 탈주 표시"), States[0]->bLeftMatch);
	TestEqual(TEXT("② 폰이 없어도 사망 통지가 들어간다"), GameMode->PendingDeaths.Num(), 1);

	MlResolveDeathsNow(World);
	TestEqual(TEXT("② 폰 없이 이탈해도 AliveCount 감소 3 → 2"), GameState->AliveCount, 2);
	TestEqual(TEXT("② 폰 없이 이탈해도 등수 부여"), States[0]->FinalRank, 3);
	TestFalse(TEXT("② 폰 없이 이탈해도 bAlive false"), States[0]->bAlive);

	// ─── ③ 이미 죽은 사람이 나가면 AliveCount 가 또 줄지 않는다 ───
	ResetMatch(3, /*bWithPawns*/true);
	if (Pawns.Num() == 3 && Pawns[0]->GetStatus())
	{
		Pawns[0]->GetStatus()->ServerKill(EDeathCause::Water); // 먼저 익사
	}
	MlResolveDeathsNow(World);
	TestEqual(TEXT("③ 전제: 익사로 이미 탈락 (FinalRank 3)"), States[0]->FinalRank, 3);
	TestEqual(TEXT("③ 전제: AliveCount 2"), GameState->AliveCount, 2);

	GameMode->Logout(Controllers[0]); // 죽은 뒤(관전 중) 접속 종료
	TestTrue(TEXT("③ 탈주 표시는 붙는다"), States[0]->bLeftMatch);
	TestEqual(TEXT("③ 중복 통지를 애초에 보내지 않는다 (버퍼 비어 있음)"), GameMode->PendingDeaths.Num(), 0);

	MlResolveDeathsNow(World);
	TestEqual(TEXT("③ AliveCount 이중 감산 없음 — 여전히 2"), GameState->AliveCount, 2);
	TestEqual(TEXT("③ 등수 불변 (3등 그대로)"), States[0]->FinalRank, 3);
	if (Pawns.Num() == 3 && Pawns[0]->GetStatus())
	{
		TestEqual(TEXT("③ 사인도 덮어쓰지 않는다 (익사 그대로)"),
			Pawns[0]->GetStatus()->LastDeathCause, EDeathCause::Water);
	}
	TestFalse(TEXT("③ 매치가 끝나지도 않는다 (생존 2명)"), GameState->bMatchEnded);

	// ─── ④ 3명 중 2명이 나가면 남은 1명이 우승 ───
	ResetMatch(3, /*bWithPawns*/true);
	GameMode->Logout(Controllers[0]);
	MlResolveDeathsNow(World);
	TestEqual(TEXT("④ 1명 이탈 후 AliveCount 2"), GameState->AliveCount, 2);
	TestFalse(TEXT("④ 1명 이탈로는 안 끝난다"), GameState->bMatchEnded);

	GameMode->Logout(Controllers[1]);
	MlResolveDeathsNow(World);
	TestEqual(TEXT("④ 2명 이탈 후 AliveCount 1"), GameState->AliveCount, 1);
	TestTrue(TEXT("④ 매치 종료"), GameState->bMatchEnded);
	TestEqual(TEXT("④ 남은 1명이 우승 (FinalRank 1)"), States[2]->FinalRank, 1);
	TestTrue(TEXT("④ 우승자는 GameState 에도 실린다"), GameState->MatchWinner == States[2]);
	TestFalse(TEXT("④ 우승자에게는 탈주 표시가 없다"), States[2]->bLeftMatch);
	TestTrue(TEXT("④ 이탈자 둘 다 탈주 표시"), States[0]->bLeftMatch && States[1]->bLeftMatch);
	TestEqual(TEXT("④ 이탈자 등수 3"), States[0]->FinalRank, 3);
	TestEqual(TEXT("④ 이탈자 등수 2"), States[1]->FinalRank, 2);

	// ─── ⑤ 전원이 같은 프레임에 나가면 무승부 (기존 규약 유지) ───
	// 같은 프레임의 이탈은 동시 사망과 **같은 버퍼**에 묶인다 — 그래서 우승자가 생기지 않는다.
	ResetMatch(3, /*bWithPawns*/true);
	GameMode->Logout(Controllers[0]);
	GameMode->Logout(Controllers[1]);
	GameMode->Logout(Controllers[2]);
	TestEqual(TEXT("⑤ 세 건이 한 버퍼에 묶임"), GameMode->PendingDeaths.Num(), 3);

	MlResolveDeathsNow(World);
	TestEqual(TEXT("⑤ 무승부: AliveCount 0"), GameState->AliveCount, 0);
	TestTrue(TEXT("⑤ 무승부: 매치 종료"), GameState->bMatchEnded);
	TestTrue(TEXT("⑤ 무승부: MatchWinner == nullptr"), GameState->MatchWinner == nullptr);
	TestEqual(TEXT("⑤ 무승부: 전원 공동 2등 (1등 자리를 비워 둔다)"), States[0]->FinalRank, 2);
	TestEqual(TEXT("⑤ 무승부: 전원 공동 2등 (B)"), States[1]->FinalRank, 2);
	TestEqual(TEXT("⑤ 무승부: 전원 공동 2등 (C)"), States[2]->FinalRank, 2);
	TestTrue(TEXT("⑤ 전원 탈주 표시"),
		States[0]->bLeftMatch && States[1]->bLeftMatch && States[2]->bLeftMatch);
	TestEqual(TEXT("⑤ 참가 인원은 3 그대로"), GameMode->MatchParticipantCount, 3);

	// ─── ⑥ 스폰 대기 목록에서 빠진다 ───
	// 남아 있으면 FlushPendingSpawns 가 파괴 중인 컨트롤러에 폰을 붙이려 든다.
	ResetMatch(2, /*bWithPawns*/false);
	GameMode->PendingSpawnControllers.AddUnique(Controllers[0]); // friend — 게이트가 넣는 것과 같은 방식
	GameMode->PendingSpawnControllers.AddUnique(Controllers[1]);
	TestEqual(TEXT("⑥ 전제: 대기 2명"), GameMode->PendingSpawnControllers.Num(), 2);

	GameMode->Logout(Controllers[0]);
	TestEqual(TEXT("⑥ 이탈자가 대기 목록에서 빠진다"), GameMode->PendingSpawnControllers.Num(), 1);
	if (GameMode->PendingSpawnControllers.Num() == 1)
	{
		TestTrue(TEXT("⑥ 남은 항목은 안 나간 사람"),
			GameMode->PendingSpawnControllers[0].Get() == Controllers[1]);
	}
	// 해소를 돌려도 나간 사람에게는 폰이 생기지 않는다 (목록에 없으므로 후보조차 아니다).
	MlResolveDeathsNow(World);

	// ─── ⑦ OnDeactivated 가 PlayerState 를 파괴하지 않는다 ───
	// 파괴되면 PlayerArray 에서 빠져 **결과 화면에서 그 사람이 통째로 증발한다** —
	// 등수를 부여해 놓고 그 등수를 보여줄 데이터가 없어지는 셈이다.
	ResetMatch(2, /*bWithPawns*/false);
	{
		ACA3DPlayerState* Leaver = States[0];
		GameMode->Logout(Controllers[0]);
		MlResolveDeathsNow(World);

		Leaver->OnDeactivated(); // 엔진이 APlayerController::CleanupPlayerState 에서 부르는 훅

		TestTrue(TEXT("⑦ OnDeactivated 후에도 PlayerState 가 살아 있다"), IsValid(Leaver));
		TestTrue(TEXT("⑦ PlayerArray 에 남아 있다 (결과 화면의 데이터 출처)"),
			GameState->PlayerArray.Contains(Leaver));
		TestTrue(TEXT("⑦ 부여된 순위가 그대로 읽힌다"), Leaver->FinalRank > 0);
		TestTrue(TEXT("⑦ 탈주 표시도 그대로"), Leaver->bLeftMatch);
	}

	// ─── ⑦b 결과 행 수집이 탈주 표시를 그대로 옮긴다 ───
	{
		GameState->bMatchEnded = true; // 결과 게이트 통과용
		const TArray<FMatchResultRow> Rows = UMatchWidget::CollectResultRows(GameState);
		if (TestEqual(TEXT("⑦b 나간 사람도 결과 행에 남는다 (2행)"), Rows.Num(), 2))
		{
			const FMatchResultRow* LeftRow = Rows.FindByPredicate(
				[&](const FMatchResultRow& Row) { return Row.bLeft; });
			if (TestNotNull(TEXT("⑦b 탈주 행이 존재"), LeftRow))
			{
				TestTrue(TEXT("⑦b 탈주 행에도 등수가 있다"), LeftRow->Rank > 0);
				TestTrue(TEXT("⑦b 표시에 (탈주) 가 붙는다"),
					UMatchWidget::FormatResultRow(*LeftRow).ToString().Contains(TEXT("(탈주)")));
			}
		}
		// 일부러 되돌리지 않는다 — 아래 ⑧ 은 순수 함수만 쓰고, 종료 플래그를 세워 둔 채
		// 월드를 정리해야 액터 파괴가 유발하는 Logout 이 죽어가는 월드에서 타이머를 예약하지 않는다.
	}

	// ─── ⑧ 표시 순수 함수: 탈주는 표시일 뿐 순위 규칙이 아니다 ───
	{
		// 등수는 그대로 보이고, 이름 뒤에 표식만 붙는다.
		TArray<FMatchResultRow> Raw;
		Raw.Add(MlMakeRow(1, TEXT("Winner"), /*bLeft*/false));
		Raw.Add(MlMakeRow(2, TEXT("Stayer"), false));
		Raw.Add(MlMakeRow(3, TEXT("Runner"), true));

		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, /*bMatchEnded*/true);
		if (TestEqual(TEXT("⑧ 행 수 보존"), Rows.Num(), 3))
		{
			TestEqual(TEXT("⑧ 탈주자 행 — 등수는 그대로"),
				UMatchWidget::FormatResultRow(Rows[2]).ToString(), FString(TEXT("   3등   Runner (탈주)")));
			TestEqual(TEXT("⑧ 완주자 행에는 표식 없음"),
				UMatchWidget::FormatResultRow(Rows[0]).ToString(), FString(TEXT("   1등   Winner")));
		}

		// 공동 등수 + 본인 표식과도 겹쳐서 동작한다.
		TArray<FMatchResultRow> TiedRaw;
		TiedRaw.Add(MlMakeRow(2, TEXT("A"), true));
		TiedRaw.Add(MlMakeRow(2, TEXT("B"), false));
		TiedRaw[0].bIsLocal = true;
		const TArray<FMatchResultRow> TiedRows = UMatchWidget::BuildResultRows(TiedRaw, true);
		if (TestEqual(TEXT("⑧ 공동 행 수"), TiedRows.Num(), 2))
		{
			TestEqual(TEXT("⑧ 본인 표식 + 공동 등수 + 탈주 표식이 함께"),
				UMatchWidget::FormatResultRow(TiedRows[0]).ToString(), FString(TEXT("▶ 공동 2등   A (탈주)")));
		}

		// **정렬·공동등수 규칙은 탈주 유무와 완전히 무관하다** — 같은 입력에서 bLeft 만
		// 뒤집었을 때 Rank·bTied·순서가 한 칸도 달라지면 안 된다 (탈주가 순위 규칙에 새어든 것).
		TArray<FMatchResultRow> PlainRaw;
		PlainRaw.Add(MlMakeRow(3, TEXT("C"), false));
		PlainRaw.Add(MlMakeRow(1, TEXT("A"), false));
		PlainRaw.Add(MlMakeRow(3, TEXT("D"), false));
		PlainRaw.Add(MlMakeRow(2, TEXT("B"), false));

		TArray<FMatchResultRow> LeftRaw = PlainRaw;
		for (FMatchResultRow& Row : LeftRaw)
		{
			Row.bLeft = true; // 전원 탈주로 뒤집는다
		}

		const TArray<FMatchResultRow> PlainRows = UMatchWidget::BuildResultRows(PlainRaw, true);
		const TArray<FMatchResultRow> LeftRows  = UMatchWidget::BuildResultRows(LeftRaw, true);
		if (TestEqual(TEXT("⑧ 두 결과의 행 수가 같다"), PlainRows.Num(), LeftRows.Num()))
		{
			bool bSameOrdering = true;
			for (int32 Index = 0; Index < PlainRows.Num(); ++Index)
			{
				bSameOrdering &= (PlainRows[Index].Rank == LeftRows[Index].Rank)
					&& (PlainRows[Index].bTied == LeftRows[Index].bTied)
					&& (PlainRows[Index].PlayerName == LeftRows[Index].PlayerName);
			}
			TestTrue(TEXT("⑧ 정렬·공동등수 결과가 탈주 유무와 무관"), bSameOrdering);
			TestTrue(TEXT("⑧ (대조군) 공동 3등이 실제로 묶여 있다"),
				PlainRows[2].bTied && PlainRows[3].bTied);
		}

		// 무승부 판정도 탈주를 보지 않는다 — 1등 유무 하나로만 정해진다.
		TestTrue(TEXT("⑧ 전원 탈주라도 1등이 있으면 무승부가 아니다"),
			!UMatchWidget::IsDrawResult(LeftRows, true));
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
