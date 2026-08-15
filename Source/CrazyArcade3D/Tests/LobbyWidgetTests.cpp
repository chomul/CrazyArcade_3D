// 로비 위젯 자동화 테스트 (Task 41 — 2026-08-16 사용자 요청 ①).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.UI.Lobby" 로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다 — **표시 가공을 static 순수 함수로 뽑아 둔 덕에**
// 위젯 인스턴스·뷰포트·슬레이트 없이 전부 검증된다 (MatchWidgetTests 와 같은 이유).
// 실제 화면 표시(WBP 바인딩·버튼 클릭·방장 전환)는 리슨 서버 + 클라 실전 검증.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Lbw~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Framework/CA3DGameMode.h"
#include "HAL/IConsoleManager.h"
#include "UI/CA3DHUD.h"
#include "UI/LobbyWidget.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyWidgetTest, "CrazyArcade3D.UI.Lobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString LbwStatus(int32 NonHostCount, int32 ReadyNonHostCount, bool bIsHost)
	{
		return ULobbyWidget::FormatLobbyStatus(NonHostCount, ReadyNonHostCount, bIsHost).ToString();
	}

	FString LbwReadyLabel(bool bReady)
	{
		return ULobbyWidget::FormatReadyButtonLabel(bReady).ToString();
	}
}

bool FLobbyWidgetTest::RunTest(const FString& Parameters)
{
	// ─── ① 참가자 한 줄 (방장 / 준비 / 대기 / 본인 표식) ──────────────────
	// 앞 3칸은 본인 표식(▶) 자리 — 남의 행은 공백으로 들여써 열이 어긋나지 않는다
	// (FormatResultRow 와 같은 관례).
	TestEqual(TEXT("① 방장 행"),
		ULobbyWidget::FormatRosterLine(TEXT("Player 1"), /*bIsHost*/true, /*bReady*/false, /*bIsLocal*/false),
		FString(TEXT("   ★ 방장  Player 1")));

	TestEqual(TEXT("① 준비 완료 행"),
		ULobbyWidget::FormatRosterLine(TEXT("Player 2"), false, /*bReady*/true, false),
		FString(TEXT("   [준비]  Player 2")));

	TestEqual(TEXT("① 대기 행"),
		ULobbyWidget::FormatRosterLine(TEXT("Player 3"), false, false, false),
		FString(TEXT("   [대기]  Player 3")));

	TestEqual(TEXT("① 본인 행에 표식"),
		ULobbyWidget::FormatRosterLine(TEXT("Me"), false, true, /*bIsLocal*/true),
		FString(TEXT("▶ [준비]  Me")));

	TestEqual(TEXT("① 본인이 방장인 행"),
		ULobbyWidget::FormatRosterLine(TEXT("Me"), true, false, true),
		FString(TEXT("▶ ★ 방장  Me")));

	// 방장은 준비 대상이 아니다 — bReady 가 뭐든 표기가 같아야 한다
	// (서버의 인원 집계 CountLobbyReadiness 도 방장을 세지 않는다).
	TestEqual(TEXT("① 방장 행은 bReady 를 보지 않는다"),
		ULobbyWidget::FormatRosterLine(TEXT("Host"), true, /*bReady*/true, false),
		ULobbyWidget::FormatRosterLine(TEXT("Host"), true, /*bReady*/false, false));

	// ─── ② 안내 문구 (방장 / 비방장 · 전원 준비 / 일부 준비) ───────────────
	{
		// 시작 가능 판정의 **단일 공식은 서버(GameMode)** 다 — 위젯은 부르기만 한다.
		// 이 두 줄이 그 계약을 고정한다 (여기가 깨지면 버튼 활성 규칙도 함께 깨진 것이다).
		TestTrue(TEXT("② 비방장 전원 준비 → 시작 가능"), ACA3DGameMode::CanStartFromLobby(3, 3));
		TestFalse(TEXT("② 일부만 준비 → 시작 불가"), ACA3DGameMode::CanStartFromLobby(3, 1));

		TestEqual(TEXT("② 방장 · 전원 준비"), LbwStatus(3, 3, /*bIsHost*/true),
			FString(TEXT("3/3 명 준비 완료 — 시작할 수 있습니다")));

		TestEqual(TEXT("② 방장 · 일부 준비"), LbwStatus(3, 1, true),
			FString(TEXT("1/3 명 준비 완료 — 준비를 기다리는 중")));

		// 비방장은 시작 가능 여부와 무관하게 "기다리는 중" — 시작 권한이 없기 때문이다.
		TestEqual(TEXT("② 비방장 · 일부 준비"), LbwStatus(3, 1, /*bIsHost*/false),
			FString(TEXT("1/3 명 준비 완료 — 방장이 시작하기를 기다리는 중")));

		TestEqual(TEXT("② 비방장 · 전원 준비여도 문구는 대기"), LbwStatus(3, 3, false),
			FString(TEXT("3/3 명 준비 완료 — 방장이 시작하기를 기다리는 중")));
	}

	// ─── ③ 준비 버튼 라벨 (누르면 일어날 일) ──────────────────────────────
	TestEqual(TEXT("③ 미준비 → 누르면 준비"), LbwReadyLabel(false), FString(TEXT("준비")));
	TestEqual(TEXT("③ 준비 → 누르면 해제"), LbwReadyLabel(true), FString(TEXT("준비 해제")));

	// ─── ④ 배선 회귀 — 에디터 작업 0 으로도 로비가 보인다 ─────────────────
	{
		// 위젯 클래스는 BP 지정 프로퍼티 — 미지정이 정상 경로(캔버스 폴백)다 (MatchWidget ⑥ 관례).
		const ACA3DHUD* HudCDO = GetDefault<ACA3DHUD>();
		TestTrue(TEXT("④ LobbyWidgetClass 기본 미지정 (WBP 제작 전 폴백 경로)"),
			HudCDO->LobbyWidgetClass == nullptr);

		// 폴백 상태의 진행 경로 — 콘솔 명령이 있어야 WBP 없이 준비·시작까지 검증된다.
		TestNotNull(TEXT("④ 콘솔 명령 ca3d.Ready 등록됨"),
			IConsoleManager::Get().FindConsoleObject(TEXT("ca3d.Ready")));
		TestNotNull(TEXT("④ 콘솔 명령 ca3d.StartMatch 등록됨"),
			IConsoleManager::Get().FindConsoleObject(TEXT("ca3d.StartMatch")));

		// 헤드라인이 그 콘솔 경로를 안내하는지 — 폴백만 보고도 무엇을 쳐야 할지 알아야 한다.
		TestTrue(TEXT("④ 방장 헤드라인은 시작 명령을 안내"),
			ULobbyWidget::FormatLobbyHeadline(/*bIsHost*/true).Contains(TEXT("ca3d.StartMatch")));
		TestTrue(TEXT("④ 비방장 헤드라인은 준비 명령을 안내"),
			ULobbyWidget::FormatLobbyHeadline(false).Contains(TEXT("ca3d.Ready")));

		// GameState 가 없을 때(접속 직후 폴링)도 안전해야 한다 — 목록·폴백 둘 다 빈 배열.
		TestEqual(TEXT("④ GameState 없음 → 빈 참가자 목록"),
			ULobbyWidget::CollectRosterLines(nullptr, nullptr).Num(), 0);
		TestEqual(TEXT("④ GameState 없음 → 빈 폴백 줄"),
			ULobbyWidget::BuildLobbyFallbackLines(nullptr, nullptr).Num(), 0);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
