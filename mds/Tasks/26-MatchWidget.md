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

## WBP_Match 계층 구조 (2026-08-04 제작 완료)

`Content/UI/WBP_Match`. `MatchWidget.cpp` 의 실제 갱신 코드 기준이다.

```
[Root] Canvas Panel                          (UserWidget 기본 루트)
│
├─ HUD_TopBar ················· Horizontal Box     ← 이름 자유
│  ├─ AliveCountText ·········· Text Block      ★
│  └─ MatchTimeText ··········· Text Block      ★   Slot Padding (24,0,0,0)
│
├─ ItemPanel ················· Horizontal Box  ★   ⚠ 반드시 Panel 계열
│  ├─ BombCountText ··········· Text Block      ★
│  ├─ BombRangeText ··········· Text Block      ★   Slot Padding (16,0,0,0)
│  ├─ MoveSpeedText ··········· Text Block      ★        〃
│  ├─ NeedleText ·············· Text Block      ★        〃
│  └─ KickText ················ Text Block      ★        〃
│
├─ SuddenDeathWarning ········ Border          ★   Visibility = Collapsed
│  └─ (Text Block "⚠ 서든데스")                     ← 이름 자유, 애니메이션은 여기에
│
└─ ResultPanel ··············· Border          ★   Visibility = Collapsed
   └─ Vertical Box                                  (Center/Center)
      └─ ResultText ·········· Text Block      ★   Justification = Center
```

★ = C++ 바인딩 이름. **대소문자까지 정확히** 일치해야 하고 "변수(Is Variable)" 가 켜져 있어야 한다.
아이템 순서는 `FormatStatLine` 과 같다 — 폴백과 위젯이 다르게 보이면 안 된다.

### 캔버스 슬롯 배치값

| 위젯 | Anchor | Alignment | Position | 기타 |
|---|---|---|---|---|
| `HUD_TopBar` | Top-Center | (0.5, 0) | (0, 24) | Size To Content ✔ |
| `ItemPanel` | Bottom-Left | (0, 1) | (24, −24) | Size To Content ✔ |
| `SuddenDeathWarning` | Top-Center | (0.5, 0) | (0, 100) | Size To Content ✔ |
| `ResultPanel` | 전체 (0,0)–(1,1) | — | Offset 전부 0 | 화면 덮기 |

### C++ 가 덮어쓰는 값 — 디자이너에서 만지지 말 것

| 위젯 | 정하는 쪽 |
|---|---|
| `ItemPanel` Visibility | **C++** — 스탯 유효 시 `HitTestInvisible`, 폰 없으면 `Collapsed` |
| `SuddenDeathWarning` Visibility | **C++** — Task 24 전까지 매 틱 `Collapsed` 강제 |
| `ResultPanel` Visibility | **C++** — `NativeConstruct` 에서 `Collapsed`, `ShowResult()` 에서 `Visible` |
| ★ Text Block 7종의 Text | **C++** — 디자이너 문구는 편집 중 미리보기용 |

폰트·색·패딩·Border 브러시·애니메이션은 전부 디자이너 소관.

### 함정

- **나머지 위젯 Visibility 는 `Hit Test Invisible`.** HUD 가 마우스를 먹으면 조작이 막힌다.
- **`ResultPanel` 은 뜨는 순간 마우스를 잡는다** (`Visible` 로 켜진다). 나중에 "로비로" 버튼이 붙을
  자리라 의도한 것 — 지금은 버튼이 없어 매치 종료 후 클릭이 안 먹는 게 정상이다.
- **`ResultText` 는 여러 줄**(`\n` 조인). Auto Wrap Text 는 끄고 Justification 만 Center.
- **아이콘으로 교체할 때** 각 스탯을 `HorizontalBox(Image + TextBlock)` 로 감싸도 되지만
  **★ 이름은 안쪽 TextBlock 에 남겨야 한다.** `ItemPanel` 도 Panel 계열이면 무엇이든 된다 (C++ 변경 0).
- **한글 폰트** — UMG 기본 폰트에 한글 글리프가 없으면 □ 로 보인다. 그때는 TTF 를 임포트해
  Font 에셋을 지정한다. C++ 는 문자열만 넘기므로 코드 변경은 없다.

### 다 만든 뒤 확인

PIE·`-game` 로그에서:

- `UMatchWidget: 미바인딩 위젯 N개 — ...` → **이 줄이 없으면 11개 전부 연결됨.** 뜨면 나열된 이름이 틀린 것
- `ACA3DHUD: 매치 위젯 표시 — WBP_Match_C` → HUD 배선 성공 (캔버스 폴백은 자동으로 꺼진다)

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 아이템 획득 → 즉시 수치 갱신. 사망 발생 → 생존자 수 갱신. 서든데스 발동 → 경고 표시.
- 매치 종료 → 결과 화면에 순위 표시, 로비 복귀 동선(Task 19) 연결.
- `BindWidget` 이름 불일치가 없는가 (WBP 컴파일 확인).

## 응답 원칙

- 공통 원칙.
- WBP 레이아웃은 에디터 작업 — C++ 완료와 WBP 제작 진행을 구분해 보고한다.
- 플레이스홀더로 처리한 아트 항목을 목록으로 남긴다.
