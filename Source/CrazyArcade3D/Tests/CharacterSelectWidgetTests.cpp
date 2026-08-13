// 캐릭터 선택 위젯 자동화 테스트 (Task 36 후속 UI).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.UI.CharacterSelect" 로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다 — **표시 가공을 static 순수 함수로 뽑아 둔 덕에**
// 위젯 인스턴스·뷰포트·슬레이트 없이 전부 검증된다 (MatchWidgetTests 와 같은 이유).
// 실제 화면 표시(WBP 바인딩·버튼 클릭·페이즈 전이 표시)는 PIE 검증.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 CharSel~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Framework/CA3DRuleSet.h"
#include "HAL/IConsoleManager.h"
#include "UI/CA3DHUD.h"
#include "UI/CharacterSelectWidget.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterSelectWidgetTest, "CrazyArcade3D.UI.CharacterSelect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FString CharSelCountdown(float Seconds)
	{
		return UCharacterSelectWidget::FormatCountdown(Seconds).ToString();
	}

	// 마스크를 int32 로 좁혀 TestEqual 오버로드 모호성을 피한다 (하위 8비트만 쓰므로 안전).
	int32 CharSelMask(const TArray<int32>& Indices)
	{
		return static_cast<int32>(UCharacterSelectWidget::BuildOccupiedMask(Indices));
	}

	// 세 이름짜리 표시 목록 — FormatOccupancyLine 입력을 짧게 만든다.
	TArray<FText> CharSelThreeNames(const TCHAR* First, const TCHAR* Second, const TCHAR* Third)
	{
		TArray<FText> Names;
		Names.Add(FText::FromString(First));
		Names.Add(FText::FromString(Second));
		Names.Add(FText::FromString(Third));
		return Names;
	}
}

bool FCharacterSelectWidgetTest::RunTest(const FString& Parameters)
{
	// ─── ① 남은 초 가공 (경계) ────────────────────────────────────────────
	// 카운트다운은 **올림**이다 — 내림이면 마지막 1초 내내 "0초"가 떠 있다 (위젯 헤더 주석).
	TestEqual(TEXT("① 음수는 0 으로 클램프"), UCharacterSelectWidget::CountdownWholeSeconds(-5.f), 0);
	TestEqual(TEXT("① 정확히 0 = 종료"), UCharacterSelectWidget::CountdownWholeSeconds(0.f), 0);
	TestEqual(TEXT("① 0.2초 남음 → 아직 1초"), UCharacterSelectWidget::CountdownWholeSeconds(0.2f), 1);
	TestEqual(TEXT("① 정확히 1초"), UCharacterSelectWidget::CountdownWholeSeconds(1.f), 1);
	TestEqual(TEXT("① 1초 조금 넘음 → 2초"), UCharacterSelectWidget::CountdownWholeSeconds(1.01f), 2);
	TestEqual(TEXT("① 9.7초 → 10초"), UCharacterSelectWidget::CountdownWholeSeconds(9.7f), 10);

	TestEqual(TEXT("① 문자열 — 음수"), CharSelCountdown(-3.f), TEXT("0초"));
	TestEqual(TEXT("① 문자열 — 올림"), CharSelCountdown(2.5f), TEXT("3초"));

	// ─── ② 점유 마스크 유도 (중복·INDEX_NONE 혼재) ────────────────────────
	{
		TestEqual(TEXT("② 빈 목록 = 점유 없음"), CharSelMask(TArray<int32>()), 0);

		// 미선택(INDEX_NONE)·중복 인덱스가 섞인 입력 — 실전의 PlayerArray 순회 결과가 이렇다
		// (서버가 중복을 막지만 복제 도착 순서에 따라 한 프레임 겹쳐 보일 수 있다).
		TArray<int32> Mixed;
		Mixed.Add(INDEX_NONE);
		Mixed.Add(2);
		Mixed.Add(2);          // 중복 — 같은 비트에 겹칠 뿐 오류가 아니다
		Mixed.Add(0);
		Mixed.Add(INDEX_NONE);
		TestEqual(TEXT("② 중복·미선택 혼재 → 비트 0·2 만"),
			CharSelMask(Mixed), (1 << 0) | (1 << 2));

		// 범위 밖 인덱스는 무시 — 잘못된 복제값이 와도 마스크가 깨지지 않는다.
		TArray<int32> OutOfRange;
		OutOfRange.Add(UCharacterSelectWidget::MaxCharacterSlots); // 8 = 슬롯 밖
		OutOfRange.Add(-7);
		TestEqual(TEXT("② 범위 밖은 무시"), CharSelMask(OutOfRange), 0);

		// 8종 전부 점유 — 하위 8비트가 전부 선다.
		TArray<int32> Full;
		for (int32 Index = 0; Index < UCharacterSelectWidget::MaxCharacterSlots; ++Index)
		{
			Full.Add(Index);
		}
		TestEqual(TEXT("② 전원 점유 = 하위 8비트"), CharSelMask(Full), 0xFF);
	}

	// ─── ③ 점유 현황 문자열 (캔버스 폴백과 위젯이 공유) ───────────────────
	{
		const TArray<FText> Names = CharSelThreeNames(TEXT("A"), TEXT("B"), TEXT("C"));
		// 내 선택(2)도 점유 마스크에 들어 있다 — "(내 선택)" 표기가 "(점유)"보다 우선해야 한다.
		const uint32 Mask = (1u << 1) | (1u << 2);
		TestEqual(TEXT("③ 점유·내 선택 표기"),
			UCharacterSelectWidget::FormatOccupancyLine(Names, Mask, /*LocalIndex*/2),
			FString(TEXT("[1]A  [2]B(점유)  [3]C(내 선택)")));

		TestEqual(TEXT("③ 미선택 로컬(INDEX_NONE)은 표식 없음"),
			UCharacterSelectWidget::FormatOccupancyLine(Names, 0u, INDEX_NONE),
			FString(TEXT("[1]A  [2]B  [3]C")));

		// 룰셋 미지정(빈 목록) — 페이즈 자체가 스킵되는 상태라 줄을 만들지 않는다.
		TestEqual(TEXT("③ 빈 목록 → 빈 문자열"),
			UCharacterSelectWidget::FormatOccupancyLine(TArray<FText>(), 0u, INDEX_NONE), FString());
	}

	// ─── ③b 이름 목록 유도 — 빈 DisplayName 은 "캐릭터 N" 으로 채운다 ─────
	{
		UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GetTransientPackage());
		FCA3DCharacterDef Named;
		Named.DisplayName = FText::FromString(TEXT("배찌"));
		Rules->Characters.Add(Named);
		Rules->Characters.Add(FCA3DCharacterDef()); // DisplayName 비움 — 에셋 연결 전 상태

		const TArray<FText> Names = UCharacterSelectWidget::CollectCharacterNames(Rules);
		if (TestEqual(TEXT("③b 이름 수 = 캐릭터 수"), Names.Num(), 2))
		{
			TestEqual(TEXT("③b 지정 이름은 그대로"), Names[0].ToString(), FString(TEXT("배찌")));
			TestEqual(TEXT("③b 빈 이름은 캐릭터 N"), Names[1].ToString(), FString(TEXT("캐릭터 2")));
		}

		TestEqual(TEXT("③b 룰셋 없음 → 빈 목록"),
			UCharacterSelectWidget::CollectCharacterNames(nullptr).Num(), 0);

		// GameState 없음(접속 직후 폴링)도 안전해야 한다.
		TestEqual(TEXT("③b GameState 없음 → 빈 인덱스 목록"),
			UCharacterSelectWidget::CollectCharacterIndices(nullptr).Num(), 0);
	}

	// ─── ④ 배선 회귀 — 에디터 작업 0 으로도 페이즈가 보인다 ───────────────
	{
		// 위젯 클래스는 BP 지정 프로퍼티 — 미지정이 정상 경로(캔버스 폴백)다 (MatchWidget ⑥ 관례).
		const ACA3DHUD* HudCDO = GetDefault<ACA3DHUD>();
		TestTrue(TEXT("④ CharacterSelectWidgetClass 기본 미지정 (WBP 제작 전 폴백 경로)"),
			HudCDO->CharacterSelectWidgetClass == nullptr);

		// 폴백 상태의 선택 경로 — 개발용 콘솔 명령이 등록돼 있어야 WBP 없이 선택까지 검증된다.
		TestNotNull(TEXT("④ 콘솔 명령 ca3d.SelectCharacter 등록됨"),
			IConsoleManager::Get().FindConsoleObject(TEXT("ca3d.SelectCharacter")));

		// 헤드라인이 카운트다운과 같은 가공 함수를 쓰는지 — 폴백·위젯 표기 불일치 회귀 방지.
		TestTrue(TEXT("④ 폴백 헤드라인에 남은 초 포함"),
			UCharacterSelectWidget::FormatSelectHeadline(2.5f).Contains(TEXT("3초")));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
