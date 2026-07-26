# Task 06 — VoxelWorld

> 선행: Task 01, 03, 04, 05 · 후행: Task 07(렌더러 연결), 09(초기화 호출), 15(파괴 호출)
> 체크리스트: `mds/Checklists/06-VoxelWorld.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `AVoxelWorld` |
| 부모 클래스 | `AActor` |
| 역할 | 그리드의 **소유·권한·리플리케이션** 담당. 서버는 판정용 배열만 들고, 클라는 시드로 동일 맵을 재생성한다. 블록 파괴는 서버·클라 모두 `ApplyDestruction` **단일 경로**를 통과한다 (불변식 1) |

## 생성 파일

- `Source/CrazyArcade3D/Voxel/VoxelWorld.h/.cpp`

## 구현 명세

```cpp
// VoxelWorld.h
// 복셀 지형 액터. 레벨에 1개 배치. bReplicates = true.
UCLASS()
class AVoxelWorld : public AActor
{
    GENERATED_BODY()
public:
    // ─── 조회 (서버·클라 공통, O(1)) ───
    EBlockType GetBlock(const FIntVector& C) const { return Grid.Get(C); }
    bool IsSolid(const FIntVector& C) const { return Grid.IsSolid(C); }
    const FVoxelGrid& GetGrid() const { return Grid; }   // Propagate 등 읽기 전용 접근

    // ─── 좌표 변환 (셀 크기를 아는 유일한 곳) ───
    FIntVector WorldToCell(const FVector& W) const;
    FVector    CellToWorld(const FIntVector& C) const;      // 셀 중심
    FVector    CellToWorldFloor(const FIntVector& C) const; // 셀 바닥면 중심

    // ─── 서버 전용 ───
    // 생성기로 맵을 만들고 Seed를 복제 프로퍼티에 기록한다. 클라는 OnRep으로 동일 생성.
    void ServerInitFromSeed(uint32 InSeed);
    // 폭발 결과의 파괴 셀 목록을 적용 + 전 클라에 멀티캐스트.
    void ServerDestroyBlocks(const TArray<FIntVector>& Cells);

    // ⚠️ 임시값 — 확정은 Task 10 튜닝에서. 임의 변경 금지, 질문할 것.
    UPROPERTY(EditAnywhere, Category="Voxel")
    float CellSize = 100.f;

protected:
    UPROPERTY(ReplicatedUsing=OnRep_Seed)
    uint32 Seed = 0;

    // 클라: 시드 도착 → 같은 생성기로 동일 맵 생성 → 렌더 빌드 →
    //       그 전에 도착해 쌓인 파괴 이벤트(PendingDestroyQueue) flush.
    UFUNCTION() void OnRep_Seed();

    UFUNCTION(NetMulticast, Reliable)
    void MulticastOnBlocksDestroyed(const TArray<FIntVector>& Cells);

private:
    FVoxelGrid Grid;                           // 서버·클라 모두 보유
    TScriptInterface<IVoxelRenderer> Renderer; // 클라에서만 생성, 데디는 nullptr

    // ⚠️ 알려진 함정: 파괴 Multicast가 OnRep_Seed보다 먼저 도착할 수 있다.
    // 그리드 초기화 전 수신분은 여기 쌓고 OnRep_Seed 직후 flush. 처음부터 넣는다.
    TArray<FIntVector> PendingDestroyQueue;
    bool bGridInitialized = false;

    // 서버·클라 공통 경로: Grid.Set(c, Empty) + Renderer->RemoveBlock(c).
    // 이 함수 밖에서 Grid를 파괴 목적으로 수정하는 코드가 생기면 구조 위반.
    void ApplyDestruction(const TArray<FIntVector>& Cells);
};
```

**권한 흐름 (그대로 구현할 것)**

```
[서버] ServerDestroyBlocks(Cells)     // 최상단 if (!HasAuthority()) return;
   ├─▶ ApplyDestruction(Cells)
   └─▶ MulticastOnBlocksDestroyed(Cells)
          └─▶ [클라] bGridInitialized ? ApplyDestruction(Cells)
                                      : PendingDestroyQueue에 적재
```

- 이 Task 시점에는 `Renderer == nullptr` — `ApplyDestruction`은 null 가드 후 그리드만 갱신한다. Task 07에서 렌더러가 붙는다.
- 생성기는 일단 `UFallbackMapGenerator`를 직접 생성. Task 22에서 이 한 줄만 바꾼다.

## 검증 원칙

- 공통 원칙 + 아래.
- 레벨에 임시 배치 후 PIE: `ServerInitFromSeed(1234)` → 그리드 로그 덤프가 Task 04 결과와 동일한가.
- 좌표 변환 왕복: `WorldToCell(CellToWorld(C)) == C` 를 경계 셀 포함 수 개 좌표로 확인.
- `ServerDestroyBlocks` 후 해당 셀 `Get == Empty` (렌더는 아직 없음 — 로그로).

## 응답 원칙

- 공통 원칙.
- `CellSize`는 임시값(100)임을 완료 보고에 명시한다.
- 파괴 큐(flush) 로직이 들어갔는지 명시적으로 보고한다 — 이건 "자주 터지는 버그"라 설계서가 처음부터 요구한 것.
