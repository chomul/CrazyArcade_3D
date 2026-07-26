# Task 26 — MatchWidget (3주차)

> 선행: Task 25 · 후행: 데모 빌드 아트 패스
> 체크리스트: `mds/Checklists/26-MatchWidget.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UMatchWidget` |
| 부모 클래스 | `UUserWidget` |
| 역할 | 매치 HUD의 C++ 베이스. **레이아웃·비주얼은 전부 BP(WBP_Match)**, C++는 바인딩 필드와 갱신 로직만 (GDD 5장 HUD 3요소 + 결과 화면) |

## 생성 파일

- `Source/CrazyArcade3D/UI/MatchWidget.h/.cpp`
- (에디터) `Content/UI/WBP_Match` — UMatchWidget 서브클래스, 레이아웃 제작

## 구현 명세

```cpp
// MatchWidget.h
// HUD 표시 항목 (GDD 5장): ① 내 아이템 상태 ② 생존자 수·남은 시간 ③ 서든데스 경고.
// 데이터 흐름: GameState/PlayerState/StatusComponent(읽기 전용) → 여기서 텍스트/아이콘 갱신.
UCLASS(Abstract)
class UMatchWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& G, float Dt) override;  // 타이머류만. 무거운 조회 금지

    // 결과 화면 (순위 + 간단 통계). HUD::ShowResult가 호출.
    void ShowResult(const TArray<ACA3DPlayerState*>& Ranking);

protected:
    // WBP_Match 의 같은 이름 위젯과 자동 바인딩 — 이름이 다르면 컴파일 단계에서 잡힌다.
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> AliveCountText;
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> MatchTimeText;
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UPanelWidget> ItemPanel;        // 풍선·물약·롤러 수치, 니들·킥 아이콘
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UWidget>     SuddenDeathWarning; // 평소 Collapsed
    UPROPERTY(meta=(BindWidget)) TObjectPtr<UWidget>     ResultPanel;        // 평소 Collapsed
};
```

**주의**
- BP(WBP_Match)에는 로직 금지 — 애니메이션·스타일만. 값 갱신은 전부 C++.
- 표시 값 출처: 생존자/시간 = `ACA3DGameState`, 내 스탯 = 로컬 폰의 `UStatusComponent`, 순위 = `ACA3DPlayerState`. 전부 읽기 전용.
- 아이콘 에셋은 ComfyUI 산출물(GDD 5장 파이프라인) — 없으면 플레이스홀더로 진행.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 아이템 획득 → 즉시 수치 갱신. 사망 발생 → 생존자 수 갱신. 서든데스 발동 → 경고 표시.
- 매치 종료 → 결과 화면에 순위 표시, 로비 복귀 동선(Task 19) 연결.
- `BindWidget` 이름 불일치가 없는가 (WBP 컴파일 확인).

## 응답 원칙

- 공통 원칙.
- WBP 레이아웃은 에디터 작업 — C++ 완료와 WBP 제작 진행을 구분해 보고한다.
- 플레이스홀더로 처리한 아트 항목을 목록으로 남긴다.
