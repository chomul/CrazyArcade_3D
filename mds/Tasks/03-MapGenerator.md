# Task 03 — MapGenerator (인터페이스)

> 선행: Task 01, 02 · 후행: Task 04, 06, 22
> 체크리스트: `mds/Checklists/03-MapGenerator.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `IMapGenerator` (`UMapGenerator` UINTERFACE 쌍) |
| 부모 클래스 | `UInterface` |
| 역할 | 맵 생성기 추상화. 폴백(Task 04) ↔ 절차 생성(Task 22) 교체가 `AVoxelWorld` 한 줄 수정으로 끝나게 한다 |

## 생성 파일

- `Source/CrazyArcade3D/MapGen/MapGenerator.h`
- `Source/CrazyArcade3D/Gameplay/Item/ItemTypes.h` — `EItemType`, `FItemPlacement` (생성기 출력 타입이라 여기서 함께 생성)

## 구현 명세

```cpp
// ItemTypes.h
// 아이템 종류 (GDD 3장 MVP 5종).
UENUM()
enum class EItemType : uint8
{
    Balloon,  // 폭탄 개수 +1
    Potion,   // 폭발 범위 +1
    Roller,   // 이동속도 증가
    Needle,   // 갇힘 탈출 (1회 소모품, 낮은 드랍률)
    Kick,     // 폭탄 차기
};

// 맵 생성 시점에 결정되는 아이템 배치 1건.
USTRUCT()
struct FItemPlacement
{
    GENERATED_BODY()
    FIntVector Cell;   // 아이템이 숨겨진 셀 (Destructible 블록 안)
    EItemType  Type;
};
```

```cpp
// MapGenerator.h
UINTERFACE() class UMapGenerator : public UInterface { GENERATED_BODY() };

// 맵 생성기 인터페이스.
// 결정론 계약: 같은 Seed + 같은 Rules ⇒ 항상 같은 Grid/Spawns/Items.
// 구현체 내부에서는 FRandomStream과 정수 연산만 허용 — float, FMath::Rand(),
// TMap 순회, 액터 이터레이션은 플랫폼/실행마다 순서가 달라 결정론을 깬다.
class IMapGenerator
{
    GENERATED_BODY()
public:
    // 성공 시 true. 실패(검증 불통과 등) 시 false — 호출부가 폴백 처리.
    virtual bool Generate(uint32 Seed, const UCA3DRuleSet* Rules,
                          FVoxelGrid& OutGrid,
                          TArray<FIntVector>& OutSpawns,        // 8인 스폰 셀
                          TArray<FItemPlacement>& OutItems) = 0;
};
```

**의존 규칙 메모**: `MapGen → Voxel`이 원칙이지만, 설계서가 확정한 시그니처가 `UCA3DRuleSet`(Framework)과 `FItemPlacement`(Gameplay/Item)를 받는다. **데이터 타입 참조까지만 허용**하고 액터·로직 참조는 금지한다. 헤더에서는 전방 선언, include는 `.cpp`에.

## 검증 원칙

- 공통 원칙 + 아래.
- 인터페이스와 타입 헤더가 두 타깃에서 컴파일되는가.
- `ItemTypes.h`가 `Gameplay/Item/`에 있고 다른 Gameplay 코드를 끌어오지 않는가 (enum/struct만).

## 응답 원칙

- 공통 원칙.
- 순수 인터페이스 Task이므로 PIE 검증 대상 없음 — "컴파일 검증만 수행"임을 명시한다.
