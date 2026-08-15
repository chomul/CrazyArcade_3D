// HUD·매치 위젯 자동화 테스트 (Task 25/26).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.UI.MatchWidget" 로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다 — **표시 가공을 static 순수 함수로 뽑아 둔 덕에**
// 위젯 인스턴스·뷰포트·슬레이트 없이 전부 검증된다 (UExplosionSubsystem::Propagate 와 같은 이유).
// 실제 화면 표시(WBP 바인딩·뷰포트 추가·결과 화면 전환)는 PIE 검증 (체크리스트 25/26).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Hud~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Framework/CA3DGameMode.h"
#include "HAL/IConsoleManager.h"
#include "UI/CA3DHUD.h"
#include "UI/MatchWidget.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchWidgetTest, "CrazyArcade3D.UI.MatchWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 순위 원본 행 한 줄 — 정렬 전 입력을 짧게 만든다.
	FMatchResultRow HudMakeRow(int32 Rank, const TCHAR* Name)
	{
		FMatchResultRow Row;
		Row.Rank = Rank;
		Row.PlayerName = Name;
		return Row;
	}

	// 유효한 스탯 스냅샷 (룰셋 Cap 기본값 8/6/1.6 을 가정).
	FMatchStatSnapshot HudMakeStats(int32 Active, int32 MaxBombs, int32 Range, float SpeedMul)
	{
		FMatchStatSnapshot Stats;
		Stats.bValid = true;
		Stats.ActiveBombCount = Active;
		Stats.MaxBombCount = MaxBombs;
		Stats.BombRange = Range;
		Stats.MoveSpeedMul = SpeedMul;
		Stats.MaxBombCountCap = 8;
		Stats.MaxBombRangeCap = 6;
		Stats.MoveSpeedMulCap = 1.6f;
		return Stats;
	}

	FString HudTime(float Seconds)
	{
		return UMatchWidget::FormatElapsedTime(Seconds).ToString();
	}
}

bool FMatchWidgetTest::RunTest(const FString& Parameters)
{
	// ─── ① 경과 시간 포맷 (경계) ──────────────────────────────────────────
	// 복제 지연으로 음수가 나올 수 있다 — 0 으로 눌러야 "-1:59" 같은 표시가 안 나온다.
	TestEqual(TEXT("① 음수는 0 으로 클램프"), HudTime(-5.f), TEXT("0:00"));
	TestEqual(TEXT("① 0초"), HudTime(0.f), TEXT("0:00"));
	TestEqual(TEXT("① 소수는 버림 (0.9초는 아직 0초)"), HudTime(0.9f), TEXT("0:00"));
	TestEqual(TEXT("① 59초"), HudTime(59.f), TEXT("0:59"));
	TestEqual(TEXT("① 60초 = 1분 경계"), HudTime(60.f), TEXT("1:00"));
	TestEqual(TEXT("① 9분 59초"), HudTime(599.f), TEXT("9:59"));
	TestEqual(TEXT("① 59분 59초 (시 경계 직전)"), HudTime(3599.f), TEXT("59:59"));
	TestEqual(TEXT("① 1시간 = 시 표기로 전환"), HudTime(3600.f), TEXT("1:00:00"));
	TestEqual(TEXT("① 1시간 1분 1초"), HudTime(3661.f), TEXT("1:01:01"));

	// ─── ② 스탯 표시 문자열 ───────────────────────────────────────────────
	{
		const FMatchStatSnapshot Stats = HudMakeStats(/*Active*/1, /*Max*/3, /*Range*/2, /*Speed*/1.2f);
		TestEqual(TEXT("② 폭탄 현재/최대"), UMatchWidget::FormatBombCount(Stats).ToString(), TEXT("폭탄 1/3"));
		TestEqual(TEXT("② 범위"), UMatchWidget::FormatBombRange(Stats).ToString(), TEXT("범위 2"));
		TestEqual(TEXT("② 속도 배율"), UMatchWidget::FormatMoveSpeed(Stats).ToString(), TEXT("속도 x1.20"));
		TestEqual(TEXT("② 니들 미보유"), UMatchWidget::FormatNeedle(Stats).ToString(), TEXT("니들 X"));
		TestEqual(TEXT("② 킥 미보유"), UMatchWidget::FormatKick(Stats).ToString(), TEXT("킥 X"));

		// 한 줄 조립은 개별 함수를 재사용한다 — 폴백 HUD 와 위젯이 같은 값을 쓰는 근거.
		const FString Line = UMatchWidget::FormatStatLine(Stats);
		TestTrue(TEXT("② 한 줄 조립에 폭탄 포함"), Line.Contains(TEXT("폭탄 1/3")));
		TestTrue(TEXT("② 한 줄 조립에 범위 포함"), Line.Contains(TEXT("범위 2")));
		TestTrue(TEXT("② 한 줄 조립에 속도 포함"), Line.Contains(TEXT("속도 x1.20")));
	}
	{
		// Cap 도달 표기 — 아이템을 더 먹어도 안 오르는 이유를 화면에서 알 수 있어야 한다.
		FMatchStatSnapshot Capped = HudMakeStats(0, /*Max*/8, /*Range*/6, /*Speed*/1.6f);
		Capped.bHasNeedle = true;
		Capped.bHasKick = true;
		TestEqual(TEXT("② 폭탄 상한 도달"), UMatchWidget::FormatBombCount(Capped).ToString(), TEXT("폭탄 0/8 (MAX)"));
		TestEqual(TEXT("② 범위 상한 도달"), UMatchWidget::FormatBombRange(Capped).ToString(), TEXT("범위 6 (MAX)"));
		TestEqual(TEXT("② 속도 상한 도달"), UMatchWidget::FormatMoveSpeed(Capped).ToString(), TEXT("속도 x1.60 (MAX)"));
		TestEqual(TEXT("② 니들 보유"), UMatchWidget::FormatNeedle(Capped).ToString(), TEXT("니들 O"));
		TestEqual(TEXT("② 킥 보유"), UMatchWidget::FormatKick(Capped).ToString(), TEXT("킥 O"));
	}
	{
		// 폰이 없는 동안(사망 후·접속 직후)에는 마지막 값이 아니라 "-" 를 보여 준다.
		const FMatchStatSnapshot Invalid; // bValid = false
		TestEqual(TEXT("② 출처 없음 — 폭탄"), UMatchWidget::FormatBombCount(Invalid).ToString(), TEXT("폭탄 -"));
		TestEqual(TEXT("② 출처 없음 — 범위"), UMatchWidget::FormatBombRange(Invalid).ToString(), TEXT("범위 -"));
		TestEqual(TEXT("② 출처 없음 — 속도"), UMatchWidget::FormatMoveSpeed(Invalid).ToString(), TEXT("속도 -"));
		TestEqual(TEXT("② 출처 없음 — 니들"), UMatchWidget::FormatNeedle(Invalid).ToString(), TEXT("니들 X"));
	}
	{
		// 폴링 최적화의 근거: 표시 자릿수보다 작은 부동소수 차이는 "같다" — 아니면 매 틱 문자열을 만든다.
		const FMatchStatSnapshot A = HudMakeStats(0, 1, 1, 1.0f);
		const FMatchStatSnapshot B = HudMakeStats(0, 1, 1, 1.0f + 0.0001f);
		TestTrue(TEXT("② 미세 오차는 같은 값으로 본다"), A == B);
		const FMatchStatSnapshot C = HudMakeStats(0, 1, 1, 1.1f);
		TestTrue(TEXT("② 표시가 달라지는 차이는 다른 값"), A != C);
	}

	// ─── ③ 순위 정렬 · 공동 등수 ──────────────────────────────────────────
	{
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(3, TEXT("Bot 1")));
		Raw.Add(HudMakeRow(1, TEXT("Player")));
		Raw.Add(HudMakeRow(3, TEXT("Bot 2")));
		Raw.Add(HudMakeRow(2, TEXT("Bot 3")));

		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, /*bMatchEnded*/true);
		if (TestEqual(TEXT("③ 행 수 보존"), Rows.Num(), 4))
		{
			TestEqual(TEXT("③ 1등이 맨 앞"), Rows[0].PlayerName, FString(TEXT("Player")));
			TestEqual(TEXT("③ 오름차순 2등"), Rows[1].Rank, 2);
			TestEqual(TEXT("③ 공동 3등이 나란히 (앞)"), Rows[2].Rank, 3);
			TestEqual(TEXT("③ 공동 3등이 나란히 (뒤)"), Rows[3].Rank, 3);
			// StableSort 라 같은 등수는 입력 순서를 유지한다 — 실행마다 순서가 바뀌면 안 된다.
			TestEqual(TEXT("③ 공동 등수 내 순서는 입력 순서 유지"), Rows[2].PlayerName, FString(TEXT("Bot 1")));

			TestFalse(TEXT("③ 단독 1등은 공동 아님"), Rows[0].bTied);
			TestFalse(TEXT("③ 단독 2등은 공동 아님"), Rows[1].bTied);
			TestTrue(TEXT("③ 공동 3등 표시 (앞)"), Rows[2].bTied);
			TestTrue(TEXT("③ 공동 3등 표시 (뒤)"), Rows[3].bTied);

			// 앞 3칸은 본인 표식(▶) 자리 — 남의 행은 공백으로 들여쓴다 (열 정렬 유지).
			TestEqual(TEXT("③ 표시 문자열 — 단독"), UMatchWidget::FormatResultRow(Rows[0]).ToString(),
				FString(TEXT("   1등   Player")));
			TestEqual(TEXT("③ 표시 문자열 — 공동"), UMatchWidget::FormatResultRow(Rows[2]).ToString(),
				FString(TEXT("   공동 3등   Bot 1")));
		}
	}

	// ─── ③b 본인 표식·내 순위 헤드라인 (2026-08-06 사용자 요청) ──────────────
	{
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(1, TEXT("Winner")));
		Raw.Add(HudMakeRow(2, TEXT("Me")));
		Raw.Add(HudMakeRow(3, TEXT("Loser")));
		Raw[1].bIsLocal = true;

		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, true);
		if (TestEqual(TEXT("③b 행 수 보존"), Rows.Num(), 3))
		{
			TestEqual(TEXT("③b 본인 행에 표식"), UMatchWidget::FormatResultRow(Rows[1]).ToString(),
				FString(TEXT("▶ 2등   Me")));
			TestEqual(TEXT("③b 헤드라인 = 내 순위"),
				UMatchWidget::FormatLocalHeadline(Rows, /*bDraw*/false).ToString(),
				FString(TEXT("내 순위  2등")));
		}

		// 내가 1등이면 등수 대신 우승 문구.
		TArray<FMatchResultRow> WinRaw;
		WinRaw.Add(HudMakeRow(1, TEXT("Me")));
		WinRaw.Add(HudMakeRow(2, TEXT("Other")));
		WinRaw[0].bIsLocal = true;
		const TArray<FMatchResultRow> WinRows = UMatchWidget::BuildResultRows(WinRaw, true);
		TestEqual(TEXT("③b 내가 1등 → 우승 문구"),
			UMatchWidget::FormatLocalHeadline(WinRows, false).ToString(), FString(TEXT("우승!")));

		// 무승부는 등수보다 우선 — 전원 공동 2등이라 "공동 2등"만 보이면 이긴 줄 안다.
		TArray<FMatchResultRow> DrawRaw;
		DrawRaw.Add(HudMakeRow(2, TEXT("Me")));
		DrawRaw.Add(HudMakeRow(2, TEXT("Other")));
		DrawRaw[0].bIsLocal = true;
		const TArray<FMatchResultRow> DrawRows = UMatchWidget::BuildResultRows(DrawRaw, true);
		TestEqual(TEXT("③b 무승부가 등수보다 우선"),
			UMatchWidget::FormatLocalHeadline(DrawRows, /*bDraw*/true).ToString(), FString(TEXT("무승부")));

		// 관전자·PlayerState 미도착 — 본인 행이 없어도 목록은 정상, 헤드라인만 중립 문구.
		TestEqual(TEXT("③b 본인 행 없음 → 중립 문구"),
			UMatchWidget::FormatLocalHeadline(Rows, false).ToString(), FString(TEXT("내 순위  2등")));
		TArray<FMatchResultRow> NoLocal;
		NoLocal.Add(HudMakeRow(1, TEXT("Someone")));
		TestEqual(TEXT("③b 본인 행이 아예 없으면 매치 종료"),
			UMatchWidget::FormatLocalHeadline(UMatchWidget::BuildResultRows(NoLocal, true), false).ToString(),
			FString(TEXT("매치 종료")));
	}
	{
		// 아직 순위가 없는(생존 중) 행은 뒤로. 결과 화면 상단이 "0등"으로 시작하면 안 된다.
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(0, TEXT("Unranked")));
		Raw.Add(HudMakeRow(2, TEXT("Second")));
		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, true);
		if (TestEqual(TEXT("③ 미확정 포함 행 수"), Rows.Num(), 2))
		{
			TestEqual(TEXT("③ 순위 있는 쪽이 앞"), Rows[0].Rank, 2);
			TestEqual(TEXT("③ 미확정은 맨 뒤"), Rows[1].Rank, 0);
			TestFalse(TEXT("③ 미확정은 공동 표시 안 함"), Rows[1].bTied);
		}
	}

	// ─── ④ 무승부 규약 (ACA3DGameState 주석의 회귀) ───────────────────────
	{
		// 마지막 전원 동시 사망 → 1등이 공석. 별도 플래그 없이 이것만으로 무승부다.
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(2, TEXT("A")));
		Raw.Add(HudMakeRow(2, TEXT("B")));
		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, true);
		TestTrue(TEXT("④ 1등 없음 = 무승부"), UMatchWidget::IsDrawResult(Rows, true));
		TestTrue(TEXT("④ 공동 2등으로 묶임"), Rows[0].bTied && Rows[1].bTied);
	}
	{
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(1, TEXT("Winner")));
		Raw.Add(HudMakeRow(2, TEXT("Loser")));
		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, true);
		TestFalse(TEXT("④ 1등이 있으면 무승부 아님"), UMatchWidget::IsDrawResult(Rows, true));
	}

	// ─── ⑤ 종료 전에는 결과가 비어 있다 ──────────────────────────────────
	{
		TArray<FMatchResultRow> Raw;
		Raw.Add(HudMakeRow(1, TEXT("Winner")));
		Raw.Add(HudMakeRow(2, TEXT("Loser")));
		const TArray<FMatchResultRow> Rows = UMatchWidget::BuildResultRows(Raw, /*bMatchEnded*/false);
		TestEqual(TEXT("⑤ 종료 전 결과는 빈 배열 (순위 선노출 차단)"), Rows.Num(), 0);
		TestFalse(TEXT("⑤ 종료 전에는 무승부 판정도 하지 않는다"), UMatchWidget::IsDrawResult(Rows, false));

		// GameState 가 없을 때도 안전해야 한다 (접속 직후 폴링).
		TestEqual(TEXT("⑤ GameState 없음 → 빈 배열"), UMatchWidget::CollectResultRows(nullptr).Num(), 0);
	}

	// ─── ⑤b 결과 확정 게이트 (Task 40) ────────────────────────────────────
	// bMatchEnded(GameState)와 FinalRank(PlayerState)는 다른 액터라 복제 도착 순서 보장이
	// 없다 — 랭크 0 이 섞인 중간 상태에서 결과 창이 먼저 뜨면 안 된다. ShowResult 와
	// DrawHUD 폴백이 이 게이트를 통과할 때만 결과를 표시한다.
	{
		// 종료 전 — 데이터가 다 있어도 결과는 미완성 취급 (선노출 차단).
		TArray<FMatchResultRow> Complete;
		Complete.Add(HudMakeRow(1, TEXT("Winner")));
		Complete.Add(HudMakeRow(2, TEXT("Loser")));
		TestFalse(TEXT("⑤b 종료 전에는 미완성"),
			UMatchWidget::IsResultDataComplete(Complete, /*bMatchEnded*/false));

		// 종료 + 빈 배열 — PlayerState 가 하나도 안 온 클라가 빈 결과 창을 띄우면 안 된다.
		TestFalse(TEXT("⑤b 종료 + 빈 배열은 미완성"),
			UMatchWidget::IsResultDataComplete(TArray<FMatchResultRow>(), true));

		// 종료 + 랭크 0 섞임 — bMatchEnded 가 FinalRank 보다 먼저 도착한 전형적 중간 상태.
		TArray<FMatchResultRow> Partial;
		Partial.Add(HudMakeRow(0, TEXT("RankNotArrived")));
		Partial.Add(HudMakeRow(2, TEXT("Loser")));
		TestFalse(TEXT("⑤b 랭크 0 이 섞이면 미완성"),
			UMatchWidget::IsResultDataComplete(Partial, true));

		// 종료 + 전원 랭크 > 0 — 완성본. 이 틱부터 결과 창이 뜬다.
		TestTrue(TEXT("⑤b 종료 + 전원 랭크 확정 = 완성"),
			UMatchWidget::IsResultDataComplete(Complete, true));

		// 무승부 형태(1등 공석, 전원 ≥ 2)도 완성이다 — 서버는 무승부에도 전원 등수를 매긴다.
		TArray<FMatchResultRow> Draw;
		Draw.Add(HudMakeRow(2, TEXT("A")));
		Draw.Add(HudMakeRow(2, TEXT("B")));
		TestTrue(TEXT("⑤b 무승부 형태(전원 ≥ 2)도 완성"),
			UMatchWidget::IsResultDataComplete(Draw, true));
	}

	// ─── ⑥ 배선 회귀 — 에디터 작업 0 으로도 HUD 가 뜬다 ──────────────────
	{
		const ACA3DGameMode* GameModeCDO = GetDefault<ACA3DGameMode>();
		TestTrue(TEXT("⑥ GameMode 기본 HUDClass 가 ACA3DHUD"),
			GameModeCDO->HUDClass == ACA3DHUD::StaticClass());

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ca3d.DebugHUD"));
		if (TestNotNull(TEXT("⑥ 콘솔 변수 ca3d.DebugHUD 등록됨"), CVar))
		{
			TestEqual(TEXT("⑥ 기본값 -1 = 자동(위젯 없을 때만 폴백)"), CVar->GetInt(), -1);
		}

		// 위젯 클래스는 BP 지정 프로퍼티 — 미지정이 정상 경로(폴백)다.
		const ACA3DHUD* HudCDO = GetDefault<ACA3DHUD>();
		TestTrue(TEXT("⑥ MatchWidgetClass 기본 미지정 (WBP 제작 전 폴백 경로)"),
			HudCDO->MatchWidgetClass == nullptr);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
