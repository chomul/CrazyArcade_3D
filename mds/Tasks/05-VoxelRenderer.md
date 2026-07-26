# Task 05 — VoxelRenderer (인터페이스)

> 선행: Task 01 · 후행: Task 06(멤버 보유), 07(구현)
> 체크리스트: `mds/Checklists/05-VoxelRenderer.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `IVoxelRenderer` (`UVoxelRenderer` UINTERFACE 쌍) |
| 부모 클래스 | `UInterface` |
| 역할 | 지형 렌더링 추상화. `AVoxelWorld`는 어떻게 그리는지 모른다. GDD 7.1의 "그리디 메싱 승격"이 이 인터페이스 구현체 교체 하나로 끝나게 한다 |

## 생성 파일

- `Source/CrazyArcade3D/Voxel/VoxelRenderer.h`

## 구현 명세

```cpp
// VoxelRenderer.h
UINTERFACE() class UVoxelRenderer : public UInterface { GENERATED_BODY() };

// 지형 렌더링 추상화. 구현체는 클라에서만 생성된다 (데디 서버는 nullptr).
// 논리 데이터(FVoxelGrid)와 렌더링의 완전 분리가 목적 (GDD 7.1).
class IVoxelRenderer
{
    GENERATED_BODY()
public:
    // 그리드 전체로부터 렌더 상태를 처음부터 구축한다 (맵 로드 시 1회).
    virtual void BuildFromGrid(const FVoxelGrid& Grid) = 0;

    // 셀 하나가 파괴됐다. 해당 인스턴스 제거 + 주변 6칸 재검사로
    // 새로 노출된 블록을 추가한다. 갱신 비용 최대 6.
    virtual void RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid) = 0;

    // 렌더 상태 전부 제거 (맵 재시작).
    virtual void Clear() = 0;
};
```

**주의**: `Voxel` 폴더 소속 — `Gameplay`/`Framework` 참조 금지. 파라미터는 `FVoxelGrid`와 좌표뿐이어야 한다.

## 검증 원칙

- 공통 원칙. 인터페이스 컴파일 + 의존 규칙 확인이 전부다.

## 응답 원칙

- 공통 원칙. 순수 인터페이스 Task — "컴파일 검증만 수행"임을 명시한다.
