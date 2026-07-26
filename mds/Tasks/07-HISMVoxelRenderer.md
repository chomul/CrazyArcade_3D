# Task 07 — HISMVoxelRenderer

> 선행: Task 05, 06 · 후행: Task 10(캐릭터가 이 컬리전 위를 걷는다)
> 체크리스트: `mds/Checklists/07-HISMVoxelRenderer.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UHISMVoxelRenderer` |
| 부모 클래스 | `UActorComponent` + `IVoxelRenderer` 구현 |
| 역할 | HISM(계층형 인스턴스드 스태틱 메시) 기반 지형 렌더링 + 컬리전. **표면 추출** — 6면 중 하나라도 Empty에 접한 블록만 인스턴스화 (전체의 20~40%) |

## 생성 파일

- `Source/CrazyArcade3D/Voxel/HISMVoxelRenderer.h/.cpp`
- (에디터) `Content/Blueprints/BP_VoxelWorld` — 블록 타입별 메시·머티리얼 지정
- (에디터) `Content/Maps/L_Arena` — BP_VoxelWorld 배치

## 구현 명세

```cpp
// HISMVoxelRenderer.h
// AVoxelWorld에 부착되는 렌더 컴포넌트. 클라 전용 —
// 생성·빌드 함수 최상단에서 if (IsRunningDedicatedServer()) return;
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class UHISMVoxelRenderer : public UActorComponent, public IVoxelRenderer
{
    GENERATED_BODY()
public:
    virtual void BuildFromGrid(const FVoxelGrid& Grid) override;
    virtual void RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid) override;
    virtual void Clear() override;

protected:
    // 블록 타입별 메시 — BP 서브클래스에서 에셋만 지정 (BP에 로직 금지).
    UPROPERTY(EditDefaultsOnly, Category="Voxel")
    TMap<EBlockType, TObjectPtr<UStaticMesh>> BlockMeshes;

private:
    // 블록 타입별 HISM 컴포넌트. BuildFromGrid에서 동적 생성.
    UPROPERTY() TMap<EBlockType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> HISMs;

    // ⚠️ 알려진 함정: RemoveInstance는 "마지막 인덱스"를 제거 자리로 당겨온다.
    // 셀→인스턴스 인덱스 맵과 인덱스→셀 역맵을 반드시 함께 갱신할 것.
    // 안 하면 엉뚱한 블록이 사라진다.
    TMap<FIntVector, int32> CellToInstance;   // 타입별로 분리 관리해도 됨
    TMap<int32, FIntVector> InstanceToCell;

    // 6면 중 하나라도 Empty(범위 밖 포함)에 접하면 표면 블록.
    bool IsSurface(const FVoxelGrid& Grid, const FIntVector& C) const;
    void AddInstanceForCell(const FVoxelGrid& Grid, const FIntVector& C);
};
```

**RemoveBlock 순서 (정확히 이 순서로)**
1. 해당 셀 인스턴스 제거 → **스왑된 마지막 인스턴스의 맵 엔트리 갱신**
2. 주변 6칸 재검사: 이제 표면이 된 블록(`IsSurface == true` && 미등록) 인스턴스 추가

**연결 작업**
1. `AVoxelWorld`가 클라에서 이 컴포넌트를 생성해 `Renderer`에 연결 (`IsRunningDedicatedServer()`면 생략).
2. BP_VoxelWorld에서 블록 3종 메시 지정 (임시 큐브 + 색 구분 머티리얼 인스턴스면 충분. 동적 머티리얼 생성 금지 — GDD 7.4).
3. `L_Arena` 맵 생성, BP_VoxelWorld 배치. `Config/DefaultEngine.ini`의 `GameDefaultMap` 주석은 **맵이 실제로 열리는 걸 확인한 후** 해제.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: **맵이 눈에 보인다.** 층·기둥·계단이 Task 04 레이아웃과 일치.
- 로그로 `총 블록 수 vs 인스턴스 수` 출력 — 표면 비율 20~40% 범위인가.
- 임시 콘솔 명령 등으로 블록 1개 파괴 → 그 블록만 사라지고 **주변 6칸의 새 표면이 노출**되는가. 엉뚱한 블록이 사라지면 인덱스 맵 버그.
- `stat unit` 켠 상태에서 빌드 시 히치 확인 (⭐ 1주차 내내 유지).

## 응답 원칙

- 공통 원칙.
- 표면 추출 비율 실측값을 보고한다.
- 에디터 연결 작업(BP·맵) 중 미완이 있으면 항목별로 남긴다.
