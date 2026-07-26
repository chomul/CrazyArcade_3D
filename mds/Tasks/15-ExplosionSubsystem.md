# Task 15 — ExplosionSubsystem

> 선행: Task 01, 02, 06, 12 · 후행: Task 16(폭탄이 호출), 20(봇 조회), 24(서든데스)
> 체크리스트: `mds/Checklists/15-ExplosionSubsystem.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UExplosionSubsystem` |
| 부모 클래스 | `UWorldSubsystem` |
| 역할 | 폭발 전파 계산(**static 순수 함수** `Propagate` — 불변식 2) + 서버의 연쇄 폭발 프레임 분산 스케줄링(불변식 아님, GDD 7.3) |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Bomb/ExplosionTypes.h` — `FExplosionResult`
- `Source/CrazyArcade3D/Gameplay/Bomb/ExplosionSubsystem.h/.cpp`

## 구현 명세

```cpp
// ExplosionTypes.h
// 폭발 1회의 계산 결과. Propagate(순수 함수)의 출력.
struct FExplosionResult
{
    TArray<FIntVector> WaterCells;    // 물줄기가 채우는 칸 — 피격·아이템 소멸 판정 대상
    TArray<FIntVector> BrokenCells;   // 파괴되는 블록 칸
    TArray<FIntVector> ChainedCells;  // 물줄기에 닿은 다른 폭탄의 칸 (순수 출력)
    UPROPERTY() TArray<TObjectPtr<ABomb>> ChainedBombs; // 서버 전용 — 서브시스템이 셀→폭탄 해석 후 채움
};
```

```cpp
// ExplosionSubsystem.h
UCLASS()
class UExplosionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // ─── 서버 전용 ───
    // 폭탄 하나가 터졌다 → 연쇄 큐에 투입. 실제 처리는 ProcessChainStep이 단계별로.
    void RequestDetonate(ABomb* Bomb);

    // ─── 순수 함수 (불변식 2) ───
    // FVoxelGrid만 읽고 FExplosionResult를 반환한다. 부작용 0.
    // 서버(실폭발)·클라(위험 프리뷰 데칼)·봇 AI(설치 판단)가 전부 이 함수를 쓴다 —
    // 그래서 표시와 실제가 구조적으로 어긋날 수 없다.
    // BombCells: 현재 폭탄이 놓인 셀 목록 (정렬된 TArray — 결정론 유지). 연쇄 판정용.
    static FExplosionResult Propagate(
        const FVoxelGrid& Grid, const FIntVector& Origin, int32 Range,
        bool bFloorDestructible, const TArray<FIntVector>& BombCells);

private:
    // 연쇄 프레임 분산 (GDD 7.3): 단계당 ChainStepDelay(0.05~0.1s)씩 나눠 처리 —
    // 서버 스파이크 제거 + "촤르륵" 연출 동시 획득.
    UPROPERTY() TArray<TObjectPtr<ABomb>> PendingChain;
    FTimerHandle ChainTimer;
    void ProcessChainStep();
};
```

**Propagate — 6방향 전파 규칙 (GDD 2.2, 그대로 구현)**

```
Water += Origin                                  # 원점 칸
for dir in [+X,-X,+Y,-Y,+Z,-Z]:
    for step in 1..Range:
        cell = Origin + dir*step
        switch Grid.Get(cell):
            Immortal:      break                 # 막힘
            Destructible:  Broken += cell; break # 부수고 멈춤
            Floor:         if bFloorDestructible: Broken += cell
                           break
            Empty:         Water += cell
                           if cell in BombCells: Chained += cell   # 연쇄 — 전파는 계속
```

**ProcessChainStep (서버)** — 단계마다:
1. `PendingChain`에서 이번 단계 폭탄들을 꺼내 각각 `Propagate` 호출
2. `VoxelWorld->ServerDestroyBlocks(BrokenCells)` (Task 06 경로)
3. 물줄기 셀 목록 Multicast → 클라가 풀(Task 14)에서 FX 획득, `WaterLingerTime` 후 반납
4. `WaterCells` 안의 캐릭터(발밑 셀 기준, Task 10 `GetFootCell`) → `StatusComponent::ServerTrap`
5. `WaterCells` 안의 아이템 소멸 (Task 23 이후)
6. `ChainedCells`→`ChainedBombs` 해석, 다음 단계 `PendingChain`으로 (중복 폭발 방지 플래그 필수)

**설계서와의 차이 (보고됨)**: 순수성 유지를 위해 `Propagate` 인자에 `bFloorDestructible`(룰셋 값)과 `BombCells`를 명시적으로 받는다 — 원 시그니처는 `(Grid, Origin, Range)`였으나 그 형태로는 바닥 규칙·연쇄 판정이 부작용 없이 불가능하다.

## 검증 원칙

- 공통 원칙 + 아래.
- `Propagate`는 **유닛 테스트 가능해야 한다**: 손으로 만든 미니 그리드로 ① 6방향 전파 ② Immortal 차단 ③ Destructible 부수고 멈춤 ④ **층간(±Z) 전파** ⑤ 연쇄 셀 검출 ⑥ 같은 입력=같은 출력. 이 검증은 PIE 없이 가능 — 자동화 테스트로.
- 이 Task 시점엔 `ABomb`이 없으므로 `RequestDetonate` 연쇄 실검증은 Task 16으로 미룬다 (미검증 표기).

## 응답 원칙

- 공통 원칙.
- `Propagate` 유닛 테스트 6항목의 결과를 개별 보고한다.
- 시그니처 확장(위 "설계서와의 차이")을 완료 보고에 명시한다.
