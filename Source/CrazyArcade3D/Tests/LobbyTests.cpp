// 로비 페이즈(방장·준비·시작) 자동화 테스트 (Task 41).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Framework.Lobby" 로 실행.
//
// 2026-08-16 사용자 확정 규칙: "사람이 모이고 게임 시작 버튼이 눌린 다음에 캐릭터 선택창이
// 진행된다. 처음 들어온 사람이 방장이고 나머지는 게임 준비 버튼으로, 모두 준비가 끝나야
// 방장이 게임 시작 버튼을 누를 수 있도록."
//
// 이 스위트가 못 박는 경계 (하나라도 깨지면 판이 저절로 시작되거나 영영 시작되지 않는다):
//   ① 첫 사람이 방장, 둘째는 아니다
//   ② 봇은 방장이 되지 않는다 (봇이 방장이면 사람이 아무도 준비하지 않아도 판이 시작된다)
//   ③ 시작 가능 판정(CanStartFromLobby)의 전 분기 — 서버와 UI 버튼이 같은 공식을 쓴다
//   ④ 비방장의 시작 요청은 거부 + 상태 불변
//   ⑤ 미준비 상태에서는 방장이 눌러도 거부
//   ⑥ 전원 준비 후 방장 시작 → 로비 해제 + 캐릭터 선택 페이즈 개시
//   ⑦ 방장 이탈 시 다음 사람(ColorIndex 최소) 승계 + **자동 시작되지 않음**
//   ⑧ 로비 중에는 폰이 스폰되지 않는다 (스폰 게이트의 로비 연장 — 32-SpawnGate 의 장치 재사용)
//   ⑨ bUseLobby == false 면 기존 흐름 그대로 (무회귀 고정)
//   ⑩ 로비 + 선택 페이즈를 쓰지 않는 설정 — 시작 순간 보류 폰이 해소된다 (해소가 누락되면
//      로비를 기다린 사람이 폰을 영영 못 받는다. StartPlay 의 해소는 로비 중이라 이미 지나갔다)
//
// 월드는 CA3DGameModeTests 관례대로 GameInstance 표준 초기화 → SetGameMode →
// InitializeActorsForPlay → World->BeginPlay 순서로 엔진 LoadMap 흐름을 재현한다.
// 실제 리플리케이션(클라 위젯이 같은 값을 보는가)·커서 전이는 PIE 검증.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Lby~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "AI/BotController.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Voxel/VoxelWorld.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DLobbyTest, "CrazyArcade3D.Framework.Lobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 시나리오별 월드 한 벌 — friend 접근이 필요한 구성(BuildWorld 람다)은 RunTest 본문에 있다.
	struct FLbyWorldFixture
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

	int32 LbyCountCharacters(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACA3DCharacter> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	// 접속한 컨트롤러 하나 — 참가 등록·폰 스폰 시도는 friend 접근이 필요해 RunTest 본문에서 한다.
	ACA3DPlayerController* LbySpawnController(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<ACA3DPlayerController>(
			ACA3DPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}

	// 봇 컨트롤러 하나 — ACA3DGameMode::SpawnFillBots 와 같은 순서로 만든다
	// (PlayerState 는 bWantsPlayerState 덕분에 스폰 시점에 이미 붙어 있고, 봇 표시는 등록 전에 세운다).
	ABotController* LbySpawnBot(UWorld* World)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		ABotController* Bot = World->SpawnActor<ABotController>(ABotController::StaticClass(), Params);
		if (Bot)
		{
			if (ACA3DPlayerState* BotState = Bot->GetPlayerState<ACA3DPlayerState>())
			{
				BotState->SetIsABot(true);
			}
		}
		return Bot;
	}
}

bool FCA3DLobbyTest::RunTest(const FString& Parameters)
{
	// ─── 구성 람다 (friend 접근이 필요해 무명 네임스페이스가 아니라 본문에 둔다 —
	//     람다는 이 friend 멤버 함수의 접근 권한을 그대로 갖는다) ───

	auto BuildWorld = [](bool bUseLobby, int32 CharacterCount, float SelectDuration, int32 Seed) -> FLbyWorldFixture
	{
		FLbyWorldFixture Out;

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
		Out.Rules->bUseLobby = bUseLobby;
		Out.GameMode->Rules = Out.Rules; // friend — BP(DA_Rules_Default) 대신 주입
		Out.GameMode->bUseFixedSeed = true;
		Out.GameMode->FixedSeed = Seed;

		Out.World->BeginPlay(); // → StartPlay → GameMode::BeginPlay (로비 판가름 + 스폰 게이트)
		Out.GameState = Out.World->GetGameState<ACA3DGameState>();
		return Out;
	};

	// 접속 절차 재현 — SpawnGate 테스트와 같은 순서 (Login 의 UpdatePlayerStartSpot 은
	// 우리 오버라이드가 no-op 이므로 생략).
	auto Login = [](FLbyWorldFixture& F) -> ACA3DPlayerController*
	{
		ACA3DPlayerController* PC = LbySpawnController(F.World);
		if (PC)
		{
			F.GameMode->RegisterParticipant(PC);     // friend — PostLogin 앞부분
			F.GameMode->HandleStartingNewPlayer(PC); // PostLogin 뒷부분 (로비 중이면 보류)
		}
		return PC;
	};

	// ─── 시나리오 A: 로비 진행 — 방장·준비·시작 조건·스폰 게이트 ───
	{
		FLbyWorldFixture A = BuildWorld(/*bUseLobby*/ true, /*Characters*/ 8, /*Duration*/ 10.f, /*Seed*/ 777);
		if (!TestNotNull(TEXT("A: 월드"), A.World) || !TestNotNull(TEXT("A: GameMode"), A.GameMode)
			|| !TestNotNull(TEXT("A: GameState"), A.GameState))
		{
			A.Destroy();
			return false;
		}

		// ─ ⓪ 로비 개시 상태 — 매치의 어떤 시계도 흐르지 않는다 ─
		TestTrue(TEXT("⓪ 로비 활성 플래그"), A.GameState->bLobbyActive);
		TestFalse(TEXT("⓪ 캐릭터 선택은 아직 시작되지 않았다"), A.GameState->bCharacterSelectActive);
		TestFalse(TEXT("⓪ 봇 채우기 타이머 없음 (로비 종료로 미룸)"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->BotFillTimer));
		TestFalse(TEXT("⓪ 서든데스 타이머 없음 (로비를 오래 끌어도 시계가 안 흐른다)"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->SuddenDeathTimer));
		TestFalse(TEXT("⓪ 선택 페이즈 타이머도 없음"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->CharacterSelectTimer));

		ACA3DPlayerController* PC0 = Login(A);
		ACA3DPlayerController* PC1 = Login(A);
		ACA3DPlayerController* PC2 = Login(A);
		if (!TestNotNull(TEXT("A: PC0"), PC0) || !TestNotNull(TEXT("A: PC1"), PC1)
			|| !TestNotNull(TEXT("A: PC2"), PC2))
		{
			A.Destroy();
			return false;
		}

		ACA3DPlayerState* PS0 = PC0->GetPlayerState<ACA3DPlayerState>();
		ACA3DPlayerState* PS1 = PC1->GetPlayerState<ACA3DPlayerState>();
		ACA3DPlayerState* PS2 = PC2->GetPlayerState<ACA3DPlayerState>();
		if (!TestNotNull(TEXT("A: PS0"), PS0) || !TestNotNull(TEXT("A: PS1"), PS1)
			|| !TestNotNull(TEXT("A: PS2"), PS2))
		{
			A.Destroy();
			return false;
		}

		// ─ ① 첫 사람이 방장 ─
		TestTrue(TEXT("① 첫 입장자가 방장"), PS0->bIsHost);
		TestFalse(TEXT("① 둘째는 방장이 아니다"), PS1->bIsHost);
		TestFalse(TEXT("① 셋째도 방장이 아니다"), PS2->bIsHost);
		TestFalse(TEXT("① 초기 준비 상태는 전원 미준비"), PS1->bReady || PS2->bReady);

		// ─ ⑧ 로비 중에는 폰이 없다 (스폰 게이트 연장) ─
		const APawn* LobbyPawn0 = PC0->GetPawn();
		TestNull(TEXT("⑧ 로비 중 폰 없음 (방장도 예외 없다)"), LobbyPawn0);
		TestEqual(TEXT("⑧ 3명 전원 스폰 보류"), A.GameMode->PendingSpawnControllers.Num(), 3);
		TestEqual(TEXT("⑧ 월드에 폰이 하나도 없다"), LbyCountCharacters(A.World), 0);

		// ─ ④ 비방장의 시작 요청 거부 + 상태 불변 ─
		TestFalse(TEXT("④ 비방장의 시작 요청 거부"), A.GameMode->TryStartMatchFromLobby(PS1));
		TestTrue(TEXT("④ 로비 플래그 불변"), A.GameState->bLobbyActive);
		TestFalse(TEXT("④ 선택 페이즈가 시작되지 않았다"), A.GameState->bCharacterSelectActive);

		// ─ ⑤ 미준비 상태에서는 방장이 눌러도 거부 ─
		TestFalse(TEXT("⑤ 전원 미준비 — 방장 시작 거부"), A.GameMode->TryStartMatchFromLobby(PS0));
		TestTrue(TEXT("⑤ 로비 플래그 불변"), A.GameState->bLobbyActive);

		// ─ 준비 단일 경로의 거부 규칙 ─
		TestFalse(TEXT("준비: 방장은 준비 대상이 아니다"), A.GameMode->TrySetReady(PS0, true));
		TestFalse(TEXT("준비: 거부된 방장의 값 불변"), PS0->bReady);
		TestFalse(TEXT("준비: null 요청 거부"), A.GameMode->TrySetReady(nullptr, true));

		TestTrue(TEXT("준비: 비방장 준비 성공"), A.GameMode->TrySetReady(PS1, true));
		TestTrue(TEXT("준비: 값 반영"), PS1->bReady);
		TestFalse(TEXT("준비: 같은 값 재요청 거부 (복제 갱신을 만들지 않는다)"),
			A.GameMode->TrySetReady(PS1, true));
		TestTrue(TEXT("준비: 거부돼도 값은 그대로 준비"), PS1->bReady);
		TestTrue(TEXT("준비: 해제도 단일 경로"), A.GameMode->TrySetReady(PS1, false));
		TestFalse(TEXT("준비: 해제 반영"), PS1->bReady);
		TestTrue(TEXT("준비: 다시 준비"), A.GameMode->TrySetReady(PS1, true));

		// 한 명만 준비된 상태 — 세는 함수와 판정 함수가 같은 결론을 낸다.
		{
			int32 NonHost = 0;
			int32 Ready = 0;
			ACA3DGameMode::CountLobbyReadiness(A.GameState, NonHost, Ready);
			TestEqual(TEXT("⑤ 비방장 2명"), NonHost, 2);
			TestEqual(TEXT("⑤ 그중 준비 1명"), Ready, 1);
			TestFalse(TEXT("⑤ 일부만 준비 — 시작 불가"), ACA3DGameMode::CanStartFromLobby(NonHost, Ready));
		}
		TestFalse(TEXT("⑤ 일부만 준비 — 방장 시작 요청도 거부"), A.GameMode->TryStartMatchFromLobby(PS0));
		TestTrue(TEXT("⑤ 로비 플래그 불변"), A.GameState->bLobbyActive);

		// ─ ⑥ 전원 준비 → 방장 시작 ─
		TestTrue(TEXT("⑥ 나머지 한 명 준비"), A.GameMode->TrySetReady(PS2, true));
		{
			int32 NonHost = 0;
			int32 Ready = 0;
			ACA3DGameMode::CountLobbyReadiness(A.GameState, NonHost, Ready);
			TestEqual(TEXT("⑥ 비방장 2명 전원 준비"), Ready, NonHost);
			TestTrue(TEXT("⑥ 시작 조건 충족"), ACA3DGameMode::CanStartFromLobby(NonHost, Ready));
		}

		TestTrue(TEXT("⑥ 방장의 시작 요청 수락"), A.GameMode->TryStartMatchFromLobby(PS0));
		TestFalse(TEXT("⑥ 로비 해제"), A.GameState->bLobbyActive);
		TestTrue(TEXT("⑥ 캐릭터 선택 페이즈 개시"), A.GameState->bCharacterSelectActive);
		TestTrue(TEXT("⑥ 선택 페이즈 타이머 예약"),
			A.GameMode->GetWorldTimerManager().IsTimerActive(A.GameMode->CharacterSelectTimer));

		// 로비가 끝나도 폰은 아직 없다 — 선택 페이즈가 같은 게이트를 이어받는다.
		const APawn* SelectPawn0 = PC0->GetPawn();
		TestNull(TEXT("⑥ 선택 페이즈로 넘어가도 폰은 아직 없다"), SelectPawn0);
		TestEqual(TEXT("⑥ 보류 목록 유지 (해소는 선택 페이즈 종료 한 곳)"),
			A.GameMode->PendingSpawnControllers.Num(), 3);

		// 중복 시작·로비 종료 후 준비 요청은 전부 거부 (악성 클라 RPC 로도 상태가 안 바뀐다).
		TestFalse(TEXT("⑥ 중복 시작 요청 거부"), A.GameMode->TryStartMatchFromLobby(PS0));
		TestFalse(TEXT("⑥ 로비 종료 후 준비 요청 거부"), A.GameMode->TrySetReady(PS1, false));
		TestTrue(TEXT("⑥ 거부된 준비 값 불변"), PS1->bReady);

		// 선택 페이즈 종료 → 그때 폰이 생긴다 (해소 지점이 한 곳임을 확인).
		A.GameMode->EndCharacterSelect(); // friend — 타이머 만료를 직접 재현
		TestEqual(TEXT("⑥ 선택 종료 후 보류 목록 비움"), A.GameMode->PendingSpawnControllers.Num(), 0);
		TestEqual(TEXT("⑥ 폰 3개 (로비를 거쳐도 전원 스폰)"), LbyCountCharacters(A.World), 3);

		A.Destroy();
	}

	// ─── 시나리오 B: ② 봇은 방장이 되지 않는다 + 방장 혼자면 시작 가능 ───
	{
		FLbyWorldFixture B = BuildWorld(true, 8, 10.f, 777);
		if (!TestNotNull(TEXT("B: GameMode"), B.GameMode) || !TestNotNull(TEXT("B: GameState"), B.GameState))
		{
			B.Destroy();
			return false;
		}

		// 사람보다 **먼저** 봇을 등록한다 — "첫 등록자가 방장" 규칙만 있으면 봇이 방장이 된다.
		ABotController* Bot = LbySpawnBot(B.World);
		if (!TestNotNull(TEXT("② 봇 컨트롤러 스폰"), Bot))
		{
			B.Destroy();
			return false;
		}
		B.GameMode->RegisterParticipant(Bot); // friend — 사람과 같은 등록 경로

		ACA3DPlayerState* BotState = Bot->GetPlayerState<ACA3DPlayerState>();
		if (TestNotNull(TEXT("② 봇 PlayerState"), BotState))
		{
			TestTrue(TEXT("② 전제: 봇 표시"), BotState->IsABot());
			TestFalse(TEXT("② 봇은 방장이 되지 않는다"), BotState->bIsHost);
		}

		ACA3DPlayerController* PC0 = Login(B);
		ACA3DPlayerState* PS0 = PC0 ? PC0->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (!TestNotNull(TEXT("B: PS0"), PS0))
		{
			B.Destroy();
			return false;
		}
		TestTrue(TEXT("② 봇 뒤에 들어온 첫 사람이 방장"), PS0->bIsHost);

		// 봇은 로비 인원이 아니다 — 세지 않으므로 방장 혼자인 것과 같다.
		{
			int32 NonHost = 0;
			int32 Ready = 0;
			ACA3DGameMode::CountLobbyReadiness(B.GameState, NonHost, Ready);
			TestEqual(TEXT("② 봇은 로비 인원에서 제외 — 비방장 0명"), NonHost, 0);
			TestTrue(TEXT("③ 비방장 0명이면 시작 가능"), ACA3DGameMode::CanStartFromLobby(NonHost, Ready));
		}
		TestTrue(TEXT("② 방장 혼자(봇 제외) 시작 가능"), B.GameMode->TryStartMatchFromLobby(PS0));
		TestFalse(TEXT("② 로비 해제"), B.GameState->bLobbyActive);

		B.Destroy();
	}

	// ─── 시나리오 C: ③ 시작 가능 판정의 전 분기 (순수 함수 — 월드 불필요) ───
	{
		TestTrue(TEXT("③ 비방장 0명 = true (방장 혼자)"), ACA3DGameMode::CanStartFromLobby(0, 0));
		TestFalse(TEXT("③ 2명 중 0명 준비 = false"), ACA3DGameMode::CanStartFromLobby(2, 0));
		TestFalse(TEXT("③ 2명 중 1명 준비 = false"), ACA3DGameMode::CanStartFromLobby(2, 1));
		TestTrue(TEXT("③ 2명 중 2명 준비 = true"), ACA3DGameMode::CanStartFromLobby(2, 2));
		TestFalse(TEXT("③ 1명 중 0명 준비 = false"), ACA3DGameMode::CanStartFromLobby(1, 0));
		TestTrue(TEXT("③ 1명 중 1명 준비 = true"), ACA3DGameMode::CanStartFromLobby(1, 1));
		TestTrue(TEXT("③ 세는 쪽이 어긋나 준비가 더 많아도 막지 않는다"),
			ACA3DGameMode::CanStartFromLobby(1, 2));
	}

	// ─── 시나리오 D: ⑦ 방장 이탈 → 승계 + **자동 시작 없음** ───
	{
		FLbyWorldFixture D = BuildWorld(true, 8, 10.f, 777);
		if (!TestNotNull(TEXT("D: GameMode"), D.GameMode) || !TestNotNull(TEXT("D: GameState"), D.GameState))
		{
			D.Destroy();
			return false;
		}

		ACA3DPlayerController* PC0 = Login(D);
		ACA3DPlayerController* PC1 = Login(D);
		ACA3DPlayerController* PC2 = Login(D);
		ACA3DPlayerState* PS0 = PC0 ? PC0->GetPlayerState<ACA3DPlayerState>() : nullptr;
		ACA3DPlayerState* PS1 = PC1 ? PC1->GetPlayerState<ACA3DPlayerState>() : nullptr;
		ACA3DPlayerState* PS2 = PC2 ? PC2->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (!TestNotNull(TEXT("D: PS0"), PS0) || !TestNotNull(TEXT("D: PS1"), PS1)
			|| !TestNotNull(TEXT("D: PS2"), PS2))
		{
			D.Destroy();
			return false;
		}

		// **승계 시 시작 조건이 자동 충족되는 상황을 일부러 만든다** — 남은 두 명이 전원 준비.
		TestTrue(TEXT("⑦ 전제: PS1 준비"), D.GameMode->TrySetReady(PS1, true));
		TestTrue(TEXT("⑦ 전제: PS2 준비"), D.GameMode->TrySetReady(PS2, true));
		TestTrue(TEXT("⑦ 전제: 방장은 PS0"), PS0->bIsHost);

		D.GameMode->Logout(PC0); // 엔진이 AController::Destroyed 에서 부르는 그 함수

		TestFalse(TEXT("⑦ 나간 사람의 방장 표시 해제"), PS0->bIsHost);
		TestTrue(TEXT("⑦ 이탈 표시"), PS0->bLeftMatch);
		TestTrue(TEXT("⑦ 남은 사람 중 가장 먼저 들어온 사람(ColorIndex 1)에게 승계"), PS1->bIsHost);
		TestFalse(TEXT("⑦ 그 뒤 사람은 방장이 아니다"), PS2->bIsHost);
		TestFalse(TEXT("⑦ 승계받은 사람의 준비 표시는 내려간다 (방장은 준비 대상이 아니다)"), PS1->bReady);

		// **자동으로 시작되지 않는다** — 이것이 이 절의 핵심이다.
		TestTrue(TEXT("⑦ 승계 직후에도 로비는 계속된다 (자동 시작 없음)"), D.GameState->bLobbyActive);
		TestFalse(TEXT("⑦ 선택 페이즈가 저절로 시작되지 않았다"), D.GameState->bCharacterSelectActive);
		TestEqual(TEXT("⑦ 폰도 생기지 않았다"), LbyCountCharacters(D.World), 0);

		// 나간 사람은 로비 인원에서 빠진다 — 그를 기다리면 로비가 영영 안 끝난다.
		{
			int32 NonHost = 0;
			int32 Ready = 0;
			ACA3DGameMode::CountLobbyReadiness(D.GameState, NonHost, Ready);
			TestEqual(TEXT("⑦ 이탈자 제외 — 비방장 1명(PS2)"), NonHost, 1);
			TestEqual(TEXT("⑦ 그 1명은 준비 완료"), Ready, 1);
		}

		// 예전 방장의 시작 요청은 이제 거부되고, 새 방장의 명시적 요청만 통한다.
		TestFalse(TEXT("⑦ 예전 방장의 시작 요청 거부"), D.GameMode->TryStartMatchFromLobby(PS0));
		TestTrue(TEXT("⑦ 새 방장의 명시적 요청으로만 시작"), D.GameMode->TryStartMatchFromLobby(PS1));
		TestFalse(TEXT("⑦ 로비 해제"), D.GameState->bLobbyActive);
		TestTrue(TEXT("⑦ 선택 페이즈 개시"), D.GameState->bCharacterSelectActive);

		D.Destroy();
	}

	// ─── 시나리오 E: ⑨ bUseLobby == false — 기존 흐름 그대로 (무회귀 고정) ───
	{
		FLbyWorldFixture E = BuildWorld(/*bUseLobby*/ false, 8, 10.f, 777);
		if (!TestNotNull(TEXT("E: GameMode"), E.GameMode) || !TestNotNull(TEXT("E: GameState"), E.GameState))
		{
			E.Destroy();
			return false;
		}

		TestFalse(TEXT("⑨ 로비 플래그가 서지 않는다"), E.GameState->bLobbyActive);
		TestTrue(TEXT("⑨ 곧바로 캐릭터 선택 페이즈부터 (기존 흐름)"), E.GameState->bCharacterSelectActive);

		ACA3DPlayerController* PC0 = Login(E);
		ACA3DPlayerState* PS0 = PC0 ? PC0->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (!TestNotNull(TEXT("E: PS0"), PS0))
		{
			E.Destroy();
			return false;
		}

		// 방장 배정 자체는 로비 유무와 무관하게 참("첫 입장 사람")이지만, 로비가 없으면 그 값을
		// 읽는 경로가 아예 돌지 않는다 — 아래 두 요청이 그 사실을 못 박는다.
		TestTrue(TEXT("⑨ 방장 배정은 로비 유무와 무관 (첫 입장 사람)"), PS0->bIsHost);
		TestFalse(TEXT("⑨ 로비가 아니면 준비 요청 거부"), E.GameMode->TrySetReady(PS0, true));
		TestFalse(TEXT("⑨ 거부된 준비 값 불변"), PS0->bReady);
		TestFalse(TEXT("⑨ 로비가 아니면 시작 요청 거부"), E.GameMode->TryStartMatchFromLobby(PS0));
		TestTrue(TEXT("⑨ 선택 페이즈는 그대로 진행 중"), E.GameState->bCharacterSelectActive);

		// 선택 페이즈의 기존 보류·해소 경로도 그대로다.
		TestEqual(TEXT("⑨ 선택 페이즈 중 보류 (기존 동작)"), E.GameMode->PendingSpawnControllers.Num(), 1);
		E.GameMode->EndCharacterSelect();
		TestEqual(TEXT("⑨ 선택 종료 시 스폰"), LbyCountCharacters(E.World), 1);

		E.Destroy();
	}

	// ─── 시나리오 F: ⑩ 로비 + 선택 페이즈 없음(Duration 0) — 시작 순간 보류가 해소된다 ───
	// **이 절이 가장 잘 깨지는 자리다**: StartPlay 의 해소는 로비 중이라 이미 지나갔고,
	// EndCharacterSelect 는 이 설정에서 영영 불리지 않는다. EndLobby 가 해소를 두드리지 않으면
	// 로비를 기다린 사람이 폰을 영영 못 받는다 (증상: 시작했는데 아무도 안 나타난다).
	{
		FLbyWorldFixture F = BuildWorld(/*bUseLobby*/ true, /*Characters*/ 8, /*Duration*/ 0.f, 777);
		if (!TestNotNull(TEXT("F: GameMode"), F.GameMode) || !TestNotNull(TEXT("F: GameState"), F.GameState))
		{
			F.Destroy();
			return false;
		}

		TestTrue(TEXT("⑩ 로비 활성"), F.GameState->bLobbyActive);
		TestFalse(TEXT("⑩ 로비 중에는 봇 타이머도 없다"),
			F.GameMode->GetWorldTimerManager().IsTimerActive(F.GameMode->BotFillTimer));

		ACA3DPlayerController* PC0 = Login(F);
		ACA3DPlayerState* PS0 = PC0 ? PC0->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (!TestNotNull(TEXT("F: PS0"), PS0))
		{
			F.Destroy();
			return false;
		}
		TestEqual(TEXT("⑩ 로비 중 보류"), F.GameMode->PendingSpawnControllers.Num(), 1);
		TestEqual(TEXT("⑩ 로비 중 폰 없음"), LbyCountCharacters(F.World), 0);

		TestTrue(TEXT("⑩ 방장 혼자 시작"), F.GameMode->TryStartMatchFromLobby(PS0));

		TestFalse(TEXT("⑩ 로비 해제"), F.GameState->bLobbyActive);
		TestFalse(TEXT("⑩ 선택 페이즈는 쓰지 않는 설정"), F.GameState->bCharacterSelectActive);
		TestEqual(TEXT("⑩ 보류 목록 해소"), F.GameMode->PendingSpawnControllers.Num(), 0);
		TestEqual(TEXT("⑩ 폰 1개 — 시작 순간 스폰된다"), LbyCountCharacters(F.World), 1);
		TestTrue(TEXT("⑩ 봇 채우기 타이머는 시작 시점부터 흐른다"),
			F.GameMode->GetWorldTimerManager().IsTimerActive(F.GameMode->BotFillTimer));
		TestTrue(TEXT("⑩ 서든데스 타이머도 시작 시점부터"),
			F.GameMode->GetWorldTimerManager().IsTimerActive(F.GameMode->SuddenDeathTimer));

		F.Destroy();
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
