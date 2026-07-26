# Task 20 — BotController (2주차)

> 선행: Task 10, 12, 15, 16 · 후행: 8인(부족분 봇) 매치 테스트
> 체크리스트: `mds/Checklists/20-BotController.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ABotController` |
| 부모 클래스 | `AAIController` |
| 역할 | 순수 C++ FSM 봇 (BT 미사용 — 학습 비용 회피, 구조 결정 12). 서버에서만 존재. **실제 플레이어와 같은 `ACA3DCharacter`·`UStatusComponent` 경로**로 조작한다 |

## 생성 파일

- `Source/CrazyArcade3D/AI/BotController.h/.cpp`
- (수정) `CA3DGameMode` — 시작 인원 부족분 봇 스폰

## 구현 명세

```cpp
// BotController.h
// 서버 전용 FSM 봇. 틱마다 상태 평가 → 캐릭터의 Move/DoJump/ServerPlaceBomb과
// "같은 함수"를 호출한다. 봇 전용 치트 경로(그리드 직접 수정 등) 금지.
UENUM()
enum class EBotState : uint8
{
    Wander,     // 목적지 셀 향해 이동 (계단·점프 포함)
    Attack,     // 상대 근처 → 폭탄 설치 후 이탈
    Evade,      // 현재 발밑 셀이 위험 → 안전 셀로 탈출
};

UCLASS()
class ABotController : public AAIController
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaSeconds) override;   // 서버 가드 + FSM 평가

private:
    EBotState State = EBotState::Wander;

    // 위험 판정: 현재 놓인 폭탄들에 대해 ExplosionSubsystem::Propagate(순수 함수)를
    // 호출해 WaterCells에 자기 발밑 셀이 포함되는지 확인. 프리뷰·실폭발과 같은 계산 —
    // 봇이 "안 피해지는 폭발"에 속을 수 없다.
    bool IsCellDangerous(const FIntVector& Cell) const;

    // 설치 판단: 여기 놓으면 상대가 맞는가 + 자기 탈출로가 남는가 (Propagate 재사용).
    bool ShouldPlaceBombAt(const FIntVector& Cell) const;

    // 그리드 기반 경로 — 인접 셀(±X,±Y, 1블록 점프 포함) BFS. NavMesh 미사용.
    TArray<FIntVector> FindPath(const FIntVector& From, const FIntVector& To) const;
};
```

**난이도·성격 튜닝은 이 Task 범위 아님** — 동작하는 더미 봇(돌아다니고, 위험을 피하고, 가끔 설치)이 목표 (GDD 8장 "더미 봇").

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 봇 2~4 스폰 → 스스로 움직이고 폭탄을 설치하고 자기 폭탄을 피하는가.
- 봇이 물줄기에 갇히고(`ServerTrap`) 죽는가 — 플레이어와 같은 판정 경로 증명.
- 봇 포함 매치가 끝까지 진행돼 승자가 나오는가 (Task 18 판정 연동).
- 봇 4대 동작 중 `stat unit` 서버 스파이크 없음 (BFS 비용 확인).

## 응답 원칙

- 공통 원칙.
- FSM 상태 전이 조건을 표로 요약해 보고한다.
- 봇이 못 하는 것(층간 경로 실패 케이스 등)을 아는 대로 명시한다 — 감추지 말 것.
