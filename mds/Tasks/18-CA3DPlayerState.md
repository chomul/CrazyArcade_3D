# Task 18 — CA3DPlayerState (2주차)

> 선행: Task 09, 12 · 후행: Task 20(봇도 사용), 26(결과 화면)
> 체크리스트: `mds/Checklists/18-CA3DPlayerState.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DPlayerState` |
| 부모 클래스 | `APlayerState` |
| 역할 | 플레이어별 복제 상태 — 색상, 생존/순위. 승패 판정(GameMode)과 결과 화면(UI)의 데이터 출처. 전적 저장은 없음(GDD 6.3) |

## 생성 파일

- `Source/CrazyArcade3D/Framework/CA3DPlayerState.h/.cpp`
- (수정) `CA3DGameMode` — 사망 통지 수신 → 생존자 수 갱신 → 승패 판정

## 구현 명세

```cpp
// CA3DPlayerState.h
// 플레이어별 매치 상태. 캐릭터가 죽어도 남는 정보는 여기에 둔다
// (StatusComponent는 폰과 함께 사라질 수 있다).
UCLASS()
class ACA3DPlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    // 캐릭터 색상 인덱스 — 1종 캐릭터 + 색 구분 (GDD 5장). GameMode가 접속 순서로 배정.
    UPROPERTY(Replicated) int32 ColorIndex = 0;

    // 탈락 순위 (0 = 아직 생존, 1 = 우승, N = N등). 매치 종료 판정·결과 화면용.
    UPROPERTY(Replicated) int32 FinalRank = 0;

    // 관전 전환 등 UI 편의용 생존 미러 (원본은 StatusComponent::LifeState).
    UPROPERTY(Replicated) bool bAlive = true;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
};
```

**GameMode 확장 (같은 Task에서 배선)**
- `StatusComponent::ServerKill` → GameMode 통지 → `bAlive=false`, `FinalRank` 부여, `GameState->AliveCount` 갱신.
- 생존 1명 → 매치 종료, 우승자 `FinalRank=1`. 최소 2명 시작(부족분은 봇 — Task 20).

## 검증 원칙

- 공통 원칙 + 아래.
- PIE(Listen + 클라 2): 한 명 사망 → 전 클라에서 `AliveCount`·`bAlive`·`FinalRank` 복제 확인.
- 마지막 1인 생존 시 매치 종료 로그·우승 순위 확인.

## 응답 원칙

- 공통 원칙.
- 승패 판정 시나리오(2인/3인, 동시 사망 포함)별 검증 여부를 구분해 보고한다. 동시 사망 순위 규칙은 임의로 정하지 말고 질문한다.
