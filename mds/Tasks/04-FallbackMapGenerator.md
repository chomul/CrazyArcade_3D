# Task 04 — FallbackMapGenerator

> 선행: Task 01, 02, 03 · 후행: Task 06 (VoxelWorld가 사용)
> 체크리스트: `mds/Checklists/04-FallbackMapGenerator.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UFallbackMapGenerator` |
| 부모 클래스 | `UObject` + `IMapGenerator` 구현 |
| 역할 | 손으로 짠 하드코딩 레이아웃을 반환하는 폴백 맵. **1주차 코어 검증용** — 절차 생성기(Task 22)가 미완이어도 게임이 돌아가게 하는 안전장치 |

## 생성 파일

- `Source/CrazyArcade3D/MapGen/FallbackMapGenerator.h/.cpp`

## 구현 명세

```cpp
// FallbackMapGenerator.h
// Seed를 무시하고 항상 같은 하드코딩 맵을 반환한다.
// 레이아웃 요구사항 (GDD 2.1 / 4.1):
//  - Rules->MapSize (기본 21×21×4)
//  - z=0 전체 Floor
//  - 외곽 1칸 둘레 Immortal 벽
//  - 내부에 Immortal 기둥(원작 크아식 격자) + Destructible 블록 다수
//  - 2층 이상으로 이어지는 "계단식 접근로" 최소 1곳 — 점프(1블록)만으로 도달 가능해야 함
//  - 코너·변 부근 스폰 8개, 각 스폰 주변 탈출로 2방향 이상
UCLASS()
class UFallbackMapGenerator : public UObject, public IMapGenerator
{
    GENERATED_BODY()
public:
    virtual bool Generate(uint32 Seed, const UCA3DRuleSet* Rules,
                          FVoxelGrid& OutGrid,
                          TArray<FIntVector>& OutSpawns,
                          TArray<FItemPlacement>& OutItems) override;
    // OutItems는 1주차에는 비워도 된다 (아이템은 Task 23).
};
```

**구현 힌트**
- 레이아웃은 전부 정수 좌표 루프로 채운다. 결정론 계약 유지(당연히 랜덤 자체를 안 씀).
- 스폰 셀은 `Floor` 위의 `Empty` 칸이어야 하고 서로 최소 거리를 둔다.

## 검증 원칙

- 공통 원칙 + 아래.
- 임시 코드로 `Generate` 호출 후 **그리드 로그 덤프** (층별 ASCII 맵 형태 권장): 블록 종류별 개수, 스폰 8개 좌표 출력.
- 두 번 호출해서 결과가 비트 단위로 동일한지(결정론) 확인.
- 스폰 셀이 전부 `IsSolid(아래 칸) == true` && `Get(스폰 칸) == Empty` 인지 코드로 확인.

## 응답 원칙

- 공통 원칙.
- 로그 덤프 결과(층별 요약·스폰 좌표)를 보고에 포함한다.
- 계단식 접근로를 어디에 뒀는지 좌표로 명시한다 — Task 10(점프 튜닝) 때 그 자리로 가서 검증한다.
