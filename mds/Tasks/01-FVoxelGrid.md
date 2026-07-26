# Task 01 — FVoxelGrid

> 선행: 없음 (프로젝트 스캐폴딩 완료 상태) · 후행: Task 03, 04, 06
> 체크리스트: `mds/Checklists/01-FVoxelGrid.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `FVoxelGrid` |
| 부모 클래스 | 없음 — **UObject 아님**, 순수 값 타입 struct |
| 역할 | 복셀 지형의 논리 데이터. 서버·클라 양쪽이 동일하게 보유하는 1바이트/칸 3D 배열. GC·리플리케이션 오버헤드 0 |

## 생성 파일

- `Source/CrazyArcade3D/Voxel/VoxelTypes.h` — `EBlockType`, 그리드 상수
- `Source/CrazyArcade3D/Voxel/VoxelGrid.h/.cpp`

## 구현 명세

```cpp
// VoxelTypes.h
// 블록 1칸의 종류. uint8 1바이트로 그리드에 저장된다.
UENUM()
enum class EBlockType : uint8
{
    Empty        = 0,  // 빈 칸 — 이동·폭발 통과
    Floor        = 1,  // 바닥 (파괴 여부는 룰셋 bFloorDestructible)
    Destructible = 2,  // 파괴 가능 블록 — 폭발에 부서지고 전파를 멈춤
    Immortal     = 3,  // 불멸 블록 — 폭발을 막음
};
```

```cpp
// VoxelGrid.h
// 복셀 지형의 순수 데이터. 서버/클라 양쪽에서 동일하게 사용한다.
// UObject가 아니므로 값 복사·스택 생성이 자유롭다. 21×21×4 = 1,764바이트.
struct FVoxelGrid
{
    FIntVector    Size = FIntVector(21, 21, 4); // 셀 개수 (X, Y, 층)
    TArray<uint8> Blocks;                       // Size.X*Y*Z 평탄화 배열, 1바이트/칸

    // 배열을 Size에 맞게 할당하고 전부 Empty로 초기화한다.
    void Init(FIntVector InSize);

    // 좌표가 그리드 범위 안인가.
    FORCEINLINE bool IsValid(const FIntVector& C) const;

    // 3D 좌표 → 평탄화 인덱스. X 우선, 그 다음 Y, Z.
    FORCEINLINE int32 Index(const FIntVector& C) const
    { return C.X + C.Y * Size.X + C.Z * Size.X * Size.Y; }

    // 범위 밖은 Empty 반환 — 폭발 전파 루프의 경계 검사 부담을 없앤다.
    FORCEINLINE EBlockType Get(const FIntVector& C) const;
    // 범위 밖이면 무시.
    FORCEINLINE void Set(const FIntVector& C, EBlockType T);

    // 그 칸 위에 서 있을 수 있는가 (Empty가 아니면 true).
    bool IsSolid(const FIntVector& C) const;
    // 폭발 전파를 막는가 (Immortal, 또는 룰에 따라 Destructible/Floor에서 멈춤 판정은 Propagate 쪽).
    bool BlocksExplosion(const FIntVector& C) const;
};
```

**주의**
- 좌표 변환(`FIntVector` ↔ `FVector`)은 **여기 두지 않는다** — 셀 크기를 알아야 하므로 `AVoxelWorld`(Task 06) 소관.
- `Voxel` 폴더는 아무것도 참조하지 않는다. 게임 규칙(`Gameplay`, `Framework`) include 금지.

## 검증 원칙

- 공통 원칙(`00-INDEX.md`) + 아래.
- 임시 검증 코드(자동화 테스트 또는 모듈 시작 시 1회 로그)로: `Init` 후 전 칸 `Empty`, `Set`→`Get` 왕복 일치, **범위 밖 `Get`이 `Empty` 반환**, `Index` 왕복 무결성.
- 로그는 `LogCA3D` 카테고리.

## 응답 원칙

- 공통 원칙(`00-INDEX.md`).
- 임시 검증 코드를 남겼다면 위치를 보고한다 (이후 Task에서 제거/이관 판단).
