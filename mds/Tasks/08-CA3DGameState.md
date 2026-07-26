# Task 08 — CA3DGameState

> 선행: Task 02 · 후행: Task 09(GameMode가 세팅), 25/26(UI가 읽음)
> 체크리스트: `mds/Checklists/08-CA3DGameState.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DGameState` |
| 부모 클래스 | `AGameStateBase` |
| 역할 | 클라에 복제되는 매치 상태. 핵심은 **룰셋 에셋 포인터 복제** — 클라의 폭탄 타이머 표시·위험 프리뷰가 서버와 같은 값으로 계산되게 한다 |

## 생성 파일

- `Source/CrazyArcade3D/Framework/CA3DGameState.h/.cpp`

## 구현 명세

```cpp
// CA3DGameState.h
// 복제되는 매치 상태. UI(읽기 전용)와 클라 프리뷰 계산의 데이터 출처.
UCLASS()
class ACA3DGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    // 룰셋 "에셋 포인터" 복제 — UE는 에셋 참조를 경로로 복제하므로
    // 값 전체가 아니라 참조만 오간다. GameMode(서버)가 세팅.
    UPROPERTY(Replicated)
    TObjectPtr<UCA3DRuleSet> Rules;

    // 생존자 수 — HUD 표시용. 서버(GameMode)만 갱신.
    UPROPERTY(Replicated)
    int32 AliveCount = 0;

    // 매치 경과 시간 기준점 — 서든데스 카운트다운·HUD 타이머 계산용.
    UPROPERTY(Replicated)
    float MatchStartServerTime = 0.f;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};
```

**주의**
- 여기에 로직을 두지 않는다 — 갱신은 전부 GameMode(서버)가, 소비는 UI/클라가.
- 매직 넘버 금지 — 서든데스 시각 등은 `Rules`에서 읽는다.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE(Listen Server + 클라 1): 클라에서 `GameState->Rules`가 **null이 아니고** `BombFuseTime` 등이 에셋 값과 일치하는지 로그 확인. (GameMode가 세팅하므로 실검증은 Task 09 이후 가능 — 그 전엔 미검증으로 남긴다.)

## 응답 원칙

- 공통 원칙.
- Task 09 완료 전이라 복제 실검증이 불가능하면 그 사실을 명시하고 해당 체크 항목을 미검증으로 남긴다.
