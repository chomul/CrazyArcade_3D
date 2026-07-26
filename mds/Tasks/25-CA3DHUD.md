# Task 25 — CA3DHUD (3주차)

> 선행: Task 08, 11 · 후행: Task 26(위젯 본체)
> 체크리스트: `mds/Checklists/25-CA3DHUD.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DHUD` |
| 부모 클래스 | `AHUD` |
| 역할 | 클라 전용 — 매치 위젯(Task 26)의 생성·수명 관리. 게임 상태를 바꾸지 않고 **Framework를 읽기만** 한다 (`UI → Framework` 읽기 전용 규칙) |

## 생성 파일

- `Source/CrazyArcade3D/UI/CA3DHUD.h/.cpp`
- (에디터) GameMode `HUDClass` 지정 (BP_CA3DGameMode)

## 구현 명세

```cpp
// CA3DHUD.h
// 위젯 수명 관리자. 표시 데이터 가공은 위젯(Task 26), 데이터 원본은 GameState/PlayerState.
// 데디 서버에는 HUD가 아예 생성되지 않지만, 방어적으로 BeginPlay 최상단 가드 유지.
UCLASS()
class ACA3DHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;   // MatchWidgetClass 생성 → 뷰포트 추가

    // 매치 종료 시 결과 화면 전환 (순위 + 간단 통계 — GDD 5장).
    void ShowResult();

protected:
    // BP(에셋 지정만): WBP_Match 를 가리킨다. C++ 는 UMatchWidget 베이스만 안다.
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UMatchWidget> MatchWidgetClass;

private:
    UPROPERTY() TObjectPtr<UMatchWidget> MatchWidget;
};
```

**주의**
- HUD/위젯 어디에도 게임 로직 금지 — 서버 RPC 호출도 금지 (입력은 PlayerController 소관).
- 화면 밖 위협 인디케이터·갇힌 플레이어 표시는 **범위 제외** (GDD 5장 HUD 제외 항목). 끼워 넣지 말 것.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 매치 시작 시 위젯이 뜨는가. 매치 종료 시 결과 화면 전환.
- **데디 서버 실행 로그에 HUD/위젯 생성 흔적이 없는가** (GDD 7.4 데디 확인 항목).

## 응답 원칙

- 공통 원칙.
- 데디 서버 확인은 실제 `CrazyArcade3DServer` 실행 로그로 보고한다 (에디터 PIE만으로는 미검증).
