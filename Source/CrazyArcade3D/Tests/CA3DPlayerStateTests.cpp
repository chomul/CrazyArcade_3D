// ACA3DPlayerState + 승패 판정 자동화 테스트 (Task 18).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Framework.PlayerState"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 다음 틱 배칭(동시 사망 묶기), 공동 등수 부여,
// AliveCount 감소, 무승부(우승자 없음), 최소 인원 게이트, 종료 후 중복 통지 차단,
// 복제 프로퍼티 등록. 실제 리플리케이션(Listen+클라에서 클라가 같은 값을 보는가)은
// PIE 검증 (체크리스트 18).
//
// 월드는 CA3DGameModeTests 관례대로 GameInstance 표준 초기화 → SetGameMode 로 만든다.
// GameState 는 GameMode 스폰 시점(PreInitializeComponents)에 함께 생성되므로
// World->BeginPlay() 는 부르지 않는다 — 맵 생성·VoxelWorld 는 이 Task 와 무관하다.
// 사망 통지 해소는 GameMode 가 SetTimerForNextTick 으로 예약하므로 타이머 매니저를
// 수동 Tick 해 결정론적으로 진행시킨다 (BombTests 의 GFrameCounter 관례).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Ps~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Net/UnrealNetwork.h" // FLifetimeProperty — 복제 등록 개수 확인용
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DPlayerStateTest, "CrazyArcade3D.Framework.PlayerState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// GameMode 가 예약한 "다음 틱" 사망 해소를 강제 실행한다.
	// FTimerManager 는 틱 밖에서 등록된 타이머를 Pending 에 뒀다가 틱 말미에 활성화하므로
	// 활성화 틱 + 실행 틱 두 번이 필요하다. 같은 GFrameCounter 프레임의 중복 Tick 은
	// 무시되므로 사이에 프레임 카운터를 진행시킨다 (테스트 전용 조작).
	void PsResolveDeathsNow(UWorld* World)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // 다음 틱 델리게이트 실행
	}

	// FinalRank == 1 인 PlayerState 존재 여부 — 무승부 규약(우승자 부재)의 판정 기준.
	bool PsHasWinner(const ACA3DGameState* GameState)
	{
		for (APlayerState* Each : GameState->PlayerArray)
		{
			const ACA3DPlayerState* Candidate = Cast<ACA3DPlayerState>(Each);
			if (Candidate && Candidate->FinalRank == 1)
			{
				return true;
			}
		}
		return false;
	}

	// UPROPERTY(Replicated) 지정 여부 — UHT 가 CPF_Net 을 붙였는지로 확인한다.
	bool PsIsNetProperty(const TCHAR* PropertyName)
	{
		const FProperty* Prop = ACA3DPlayerState::StaticClass()->FindPropertyByName(FName(PropertyName));
		return Prop != nullptr && (Prop->PropertyFlags & CPF_Net) != 0;
	}
}

bool FCA3DPlayerStateTest::RunTest(const FString& Parameters)
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
	World->SetGameMode(URL); // GameMode 액터 스폰 (컴포넌트 초기화는 아직)

	ACA3DGameMode* GameMode = World->GetAuthGameMode<ACA3DGameMode>();
	if (!TestNotNull(TEXT("ACA3DGameMode 생성"), GameMode))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// GameState 는 GameMode 의 PreInitializeComponents 에서 스폰되는데, AActor::PostActorConstruction
	// 은 월드가 AreActorsInitialized() 가 되기 전에는 그 단계를 미룬다 — 즉 GameState 는
	// InitializeActorsForPlay 이후에야 존재한다. (World->BeginPlay() 는 부르지 않는다:
	// 맵 생성·VoxelWorld 는 이 Task 와 무관하고, 없으면 GameMode::BeginPlay 가 에러 로그를 남긴다.)
	World->InitializeActorsForPlay(URL);

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("GameState 가 ACA3DGameState"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// friend 접근: BP(DA_Rules_Default) 대신 룰셋 주입 — MinPlayersForMatchEnd 를 테스트가 조작한다.
	UCA3DRuleSet* InjectedRules = NewObject<UCA3DRuleSet>(GameMode);
	GameMode->Rules = InjectedRules;
	TestEqual(TEXT("룰셋 기본 최소 인원 == 2"), InjectedRules->MinPlayersForMatchEnd, 2);

	// ─── 0. PlayerStateClass 배선 ───
	TestEqual(TEXT("⓪ GameMode 가 ACA3DPlayerState 를 PlayerStateClass 로 지정"),
		GameMode->PlayerStateClass.Get(), ACA3DPlayerState::StaticClass());

	// ─── 매치 구성 헬퍼 ───
	// PostLogin 은 실제 접속(플레이어 컨트롤러·넷 커넥션)을 요구하므로 헤드리스에서 재현하지
	// 않는다. 대신 PostLogin 이 세팅하는 것(참가 인원·AliveCount·ColorIndex)을 friend 로
	// 직접 구성해 판정 로직만 떼어 검증한다.
	TArray<ACA3DPlayerState*> Players;

	auto ResetMatch = [&](int32 Participants)
	{
		for (ACA3DPlayerState* Each : Players)
		{
			if (IsValid(Each))
			{
				Each->Destroy(); // Destroyed() 가 GameState->PlayerArray 에서 즉시 제거한다
			}
		}
		Players.Reset();

		GameMode->PendingDeaths.Reset();
		GameMode->bDeathResolveScheduled = false;
		GameMode->MatchParticipantCount = Participants;
		GameState->AliveCount = Participants;
		GameState->bMatchEnded = false;

		for (int32 i = 0; i < Participants; ++i)
		{
			ACA3DPlayerState* Each = World->SpawnActor<ACA3DPlayerState>();
			if (Each)
			{
				Each->ColorIndex = i; // PostLogin 의 접속 순서 배정을 대신한다
				Players.Add(Each);
			}
		}
	};

	// ─── 1. 순차 사망 4인 — 등수 4 → 3 → 2, 마지막 생존자 1등 ───
	ResetMatch(4);
	if (!TestEqual(TEXT("① 4인 구성"), Players.Num(), 4))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// 배칭 확인 — 통지 직후에는 아직 아무것도 바뀌지 않는다 (다음 틱에 해소).
	GameMode->NotifyPlayerDeath(Players[0]);
	TestEqual(TEXT("① 배칭: 통지 직후 PendingDeaths 1건"), GameMode->PendingDeaths.Num(), 1);
	TestEqual(TEXT("① 배칭: 해소 전 FinalRank 미부여"), Players[0]->FinalRank, 0);
	TestTrue(TEXT("① 배칭: 해소 전 bAlive 유지"), Players[0]->bAlive);
	TestEqual(TEXT("① 배칭: 해소 전 AliveCount 불변"), GameState->AliveCount, 4);

	PsResolveDeathsNow(World);
	TestEqual(TEXT("① 1번째 사망: FinalRank 4"), Players[0]->FinalRank, 4);
	TestFalse(TEXT("① 1번째 사망: bAlive false"), Players[0]->bAlive);
	TestEqual(TEXT("① 1번째 사망: AliveCount 3"), GameState->AliveCount, 3);
	TestFalse(TEXT("① 1번째 사망: 매치 계속"), GameState->bMatchEnded);
	TestEqual(TEXT("① 해소 후 PendingDeaths 비움"), GameMode->PendingDeaths.Num(), 0);

	GameMode->NotifyPlayerDeath(Players[1]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("① 2번째 사망: FinalRank 3"), Players[1]->FinalRank, 3);
	TestEqual(TEXT("① 2번째 사망: AliveCount 2"), GameState->AliveCount, 2);
	TestFalse(TEXT("① 2번째 사망: 매치 계속"), GameState->bMatchEnded);

	GameMode->NotifyPlayerDeath(Players[2]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("① 3번째 사망: FinalRank 2"), Players[2]->FinalRank, 2);
	TestEqual(TEXT("① 3번째 사망: AliveCount 1"), GameState->AliveCount, 1);
	TestTrue(TEXT("① 최후 1인: 매치 종료"), GameState->bMatchEnded);
	TestEqual(TEXT("① 최후 1인: 우승 FinalRank 1"), Players[3]->FinalRank, 1);
	TestTrue(TEXT("① 우승자: bAlive 유지"), Players[3]->bAlive);
	// 우승자는 GameState 에도 실린다 — bMatchEnded 와 같은 액터라 클라에 원자 도착 (무승부 오표시 수정).
	TestTrue(TEXT("① 우승자: GameState->MatchWinner 일치"), GameState->MatchWinner == Players[3]);

	// ─── 2. 동시 사망 — 3명 생존 중 2명이 같은 프레임에 죽으면 둘 다 공동 2등 ───
	ResetMatch(4);
	GameMode->NotifyPlayerDeath(Players[0]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("② 선행 사망: FinalRank 4"), Players[0]->FinalRank, 4);

	GameMode->NotifyPlayerDeath(Players[1]);
	GameMode->NotifyPlayerDeath(Players[2]);
	TestEqual(TEXT("② 같은 프레임 2건이 한 버퍼에 묶임"), GameMode->PendingDeaths.Num(), 2);
	GameMode->NotifyPlayerDeath(Players[1]); // 같은 대상 중복 통지 — AddUnique 로 흡수
	TestEqual(TEXT("② 중복 통지: PendingDeaths 그대로 2"), GameMode->PendingDeaths.Num(), 2);

	PsResolveDeathsNow(World);
	TestEqual(TEXT("② 동시 사망 A: 공동 2등"), Players[1]->FinalRank, 2);
	TestEqual(TEXT("② 동시 사망 B: 공동 2등"), Players[2]->FinalRank, 2);
	TestFalse(TEXT("② 동시 사망 A: bAlive false"), Players[1]->bAlive);
	TestFalse(TEXT("② 동시 사망 B: bAlive false"), Players[2]->bAlive);
	TestEqual(TEXT("② 동시 사망: AliveCount 3 → 1 (2명분 감소)"), GameState->AliveCount, 1);
	TestEqual(TEXT("② 생존자: FinalRank 1"), Players[3]->FinalRank, 1);
	TestTrue(TEXT("② 매치 종료"), GameState->bMatchEnded);
	TestTrue(TEXT("② 우승자: GameState->MatchWinner 일치"), GameState->MatchWinner == Players[3]);
	// 3등은 아무도 갖지 않는다 — 공동 2등이 2·3 자리를 함께 차지했기 때문 (경기 순위 관례).
	for (const ACA3DPlayerState* Each : Players)
	{
		TestTrue(TEXT("② 공동 2등이 3등 자리를 흡수 — FinalRank 3 인 사람 없음"), Each->FinalRank != 3);
	}

	// ─── 3. 무승부 — 마지막 2명이 같은 프레임에 죽으면 우승자가 없다 ───
	ResetMatch(4);
	GameMode->NotifyPlayerDeath(Players[0]);
	PsResolveDeathsNow(World);
	GameMode->NotifyPlayerDeath(Players[1]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("③ 사전 상태: AliveCount 2"), GameState->AliveCount, 2);
	TestFalse(TEXT("③ 사전 상태: 아직 매치 진행 중"), GameState->bMatchEnded);

	GameMode->NotifyPlayerDeath(Players[2]);
	GameMode->NotifyPlayerDeath(Players[3]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("③ 무승부: 마지막 2명 모두 공동 2등"), Players[2]->FinalRank, 2);
	TestEqual(TEXT("③ 무승부: 마지막 2명 모두 공동 2등 (B)"), Players[3]->FinalRank, 2);
	TestEqual(TEXT("③ 무승부: AliveCount 0"), GameState->AliveCount, 0);
	TestTrue(TEXT("③ 무승부: 매치 종료"), GameState->bMatchEnded);
	TestFalse(TEXT("③ 무승부 규약: FinalRank == 1 인 PlayerState 없음"), PsHasWinner(GameState));
	TestTrue(TEXT("③ 무승부: GameState->MatchWinner == nullptr"), GameState->MatchWinner == nullptr);

	// ─── 4. AliveCount 는 사망 수만큼 정확히 감소 (3명 동시 사망) ───
	ResetMatch(4);
	GameMode->NotifyPlayerDeath(Players[0]);
	GameMode->NotifyPlayerDeath(Players[1]);
	GameMode->NotifyPlayerDeath(Players[2]);
	TestEqual(TEXT("④ 3명 동시 통지"), GameMode->PendingDeaths.Num(), 3);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("④ AliveCount 4 → 1 (3명분 정확히 감소)"), GameState->AliveCount, 1);
	// 3명이 2·3·4 자리를 차지 → 전원 가장 좋은 자리인 공동 2등.
	TestEqual(TEXT("④ 3명 공동 2등 (A)"), Players[0]->FinalRank, 2);
	TestEqual(TEXT("④ 3명 공동 2등 (B)"), Players[1]->FinalRank, 2);
	TestEqual(TEXT("④ 3명 공동 2등 (C)"), Players[2]->FinalRank, 2);
	TestEqual(TEXT("④ 마지막 생존자 우승"), Players[3]->FinalRank, 1);

	// ─── 5. 최소 인원 게이트 — 참가 인원이 MinPlayersForMatchEnd 미만이면 종료 판정 없음 ───
	// (5-a) 1인 매치: 죽어도 매치가 끝나지 않는다 (1인 PIE 튜닝 시나리오).
	ResetMatch(1);
	GameMode->NotifyPlayerDeath(Players[0]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("⑤a 1인 매치: AliveCount 0 까지는 반영"), GameState->AliveCount, 0);
	TestFalse(TEXT("⑤a 1인 매치: 종료 판정 없음"), GameState->bMatchEnded);
	TestFalse(TEXT("⑤a 1인 매치: 우승자 없음"), PsHasWinner(GameState));

	// (5-b) 최소 인원을 3으로 올리면 2인 매치의 "최후 1인"도 종료로 보지 않는다.
	InjectedRules->MinPlayersForMatchEnd = 3;
	ResetMatch(2);
	GameMode->NotifyPlayerDeath(Players[0]);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("⑤b 2인 매치 < 최소 3: 사망자 순위는 부여"), Players[0]->FinalRank, 2);
	TestEqual(TEXT("⑤b 2인 매치 < 최소 3: AliveCount 1"), GameState->AliveCount, 1);
	TestFalse(TEXT("⑤b 2인 매치 < 최소 3: 종료 판정 없음"), GameState->bMatchEnded);
	TestEqual(TEXT("⑤b 2인 매치 < 최소 3: 생존자에게 우승 미부여"), Players[1]->FinalRank, 0);
	InjectedRules->MinPlayersForMatchEnd = 2; // 원복

	// ─── 6. 종료된 매치에 추가 사망 통지 → 상태 불변 (중복 종료 방지) ───
	ResetMatch(2);
	GameMode->NotifyPlayerDeath(Players[0]);
	PsResolveDeathsNow(World);
	TestTrue(TEXT("⑥ 사전 상태: 매치 종료"), GameState->bMatchEnded);
	TestEqual(TEXT("⑥ 사전 상태: 우승자 FinalRank 1"), Players[1]->FinalRank, 1);

	GameMode->NotifyPlayerDeath(Players[1]); // 종료 후 우승자 사망 통지 (관전 중 KillZ 등)
	TestEqual(TEXT("⑥ 종료 후 통지: 버퍼에 쌓이지도 않음"), GameMode->PendingDeaths.Num(), 0);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("⑥ 종료 후 통지: 우승 등수 불변"), Players[1]->FinalRank, 1);
	TestTrue(TEXT("⑥ 종료 후 통지: bAlive 불변"), Players[1]->bAlive);
	TestEqual(TEXT("⑥ 종료 후 통지: AliveCount 불변"), GameState->AliveCount, 1);
	TestTrue(TEXT("⑥ 종료 후 통지: bMatchEnded 유지"), GameState->bMatchEnded);

	// ─── 7. 복제 등록 — 프로퍼티가 Replicated 지정 + GetLifetimeReplicatedProps 에 추가됨 ───
	TestTrue(TEXT("⑦ ColorIndex 가 Replicated"), PsIsNetProperty(TEXT("ColorIndex")));
	TestTrue(TEXT("⑦ FinalRank 가 Replicated"), PsIsNetProperty(TEXT("FinalRank")));
	TestTrue(TEXT("⑦ bAlive 가 Replicated"), PsIsNetProperty(TEXT("bAlive")));
	TestTrue(TEXT("⑦ bLeftMatch 가 Replicated"), PsIsNetProperty(TEXT("bLeftMatch")));
	TestTrue(TEXT("⑦ CharacterIndex 가 Replicated"), PsIsNetProperty(TEXT("CharacterIndex")));
	TestTrue(TEXT("⑦ bReady 가 Replicated"), PsIsNetProperty(TEXT("bReady")));
	TestTrue(TEXT("⑦ bIsHost 가 Replicated"), PsIsNetProperty(TEXT("bIsHost")));

	if (Players.Num() > 0 && IsValid(Players[0]))
	{
		// ⚠️ UClass 의 복제 데이터(FProperty::RepIndex)는 실제 넷 세션 직전에 지연 초기화된다.
		// 넷드라이버 없는 테스트 월드에서는 아직 세팅되지 않아 RepIndex 가 전부 0 이고,
		// 그 상태로 GetLifetimeReplicatedProps 를 부르면 부모 등록분과 인덱스가 충돌해
		// 엔진 check(bIsPushBased == ...) 에 걸려 크래시한다. 엔진과 같은 초기화를 먼저 강제한다
		// (부모 클래스까지 재귀로 세팅되므로 아래 부모 호출도 함께 안전해진다).
		ACA3DPlayerState::StaticClass()->SetUpRuntimeReplicationData();

		// 부모(APlayerState) 등록분과의 개수 차이가 정확히 8 이어야
		// DOREPLIFETIME 8줄이 실제로 실행된 것이다.
		// 2026-08-09: CamYawIndex(관전 카메라 각) 추가로 3 → 4. 이 숫자를 올릴 때는
		// **왜 새 값이 복제돼야 하는지**를 먼저 답할 것 — 복제 대상이 늘어나는 것은
		// 대역폭이 아니라 "누가 이 값의 주인인가" 가 늘어나는 것이다.
		// 2026-08-10: bLeftMatch(중도 이탈 표시) 추가로 4 → 5. 답: **주인은 서버의
		// ACA3DGameMode::Logout 하나뿐**이고, 정작 이 값을 화면에 그려야 하는 사람은 나간
		// 본인이 아니라 **남아 있는 사람들**이다 — 서버만 아는 사실이라 복제 외에 전달 수단이 없다.
		// 2026-08-14: CharacterIndex(캐릭터 선택, Task 36) 추가로 5 → 6. 답: 주인은 서버의
		// ACA3DGameMode::TryAssignCharacter 하나뿐이고, 선택 화면이 "남이 고른 캐릭터" 를
		// 잠금 표시해야 하며(선착순의 화면 표현) 외형(Task 37)은 모든 클라가 그린다.
		// 2026-08-16: bReady·bIsHost(로비, Task 41) 추가로 6 → 8. 답: 주인은 서버의
		// ACA3DGameMode(TrySetReady / RegisterParticipant·HandleParticipantLeft) 하나뿐이고,
		// 로비 화면은 **남들의** 준비 현황과 누가 방장인지를 그려야 한다 — 내 것만 알면
		// "누구를 기다리는지" 를 알 수 없고, 시작 버튼 활성 판정(CanStartFromLobby)도
		// 서버와 같은 입력으로 계산할 수 없다.
		TArray<FLifetimeProperty> ChildProps;
		Players[0]->GetLifetimeReplicatedProps(ChildProps);

		TArray<FLifetimeProperty> ParentProps;
		Players[0]->APlayerState::GetLifetimeReplicatedProps(ParentProps);

		TestEqual(TEXT("⑦ GetLifetimeReplicatedProps 가 부모 대비 8개 추가 등록"),
			ChildProps.Num() - ParentProps.Num(), 8);
	}

	// GameState 의 종료 플래그도 복제 대상인지 (결과 화면 Task 26 의 트리거).
	{
		const FProperty* MatchEndedProp =
			ACA3DGameState::StaticClass()->FindPropertyByName(FName(TEXT("bMatchEnded")));
		TestTrue(TEXT("⑦ GameState::bMatchEnded 가 Replicated"),
			MatchEndedProp != nullptr && (MatchEndedProp->PropertyFlags & CPF_Net) != 0);
	}

	// ─── 8. 권한 가드 — 통지 함수가 상태를 바꾸는 유일한 진입점이고 서버 전용이다 (불변식 5) ───
	// (GameMode 는 서버에만 존재해 클라 경로를 헤드리스로 재현할 수 없다. 대신 null 통지가
	//  조용히 무시되는지 — 봇/PlayerState 없는 폰 경로 — 를 확인한다.)
	ResetMatch(3);
	GameMode->NotifyPlayerDeath(nullptr);
	TestEqual(TEXT("⑧ null 통지: 버퍼 불변"), GameMode->PendingDeaths.Num(), 0);
	PsResolveDeathsNow(World);
	TestEqual(TEXT("⑧ null 통지: AliveCount 불변"), GameState->AliveCount, 3);
	TestFalse(TEXT("⑧ null 통지: 매치 종료 없음"), GameState->bMatchEnded);

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
