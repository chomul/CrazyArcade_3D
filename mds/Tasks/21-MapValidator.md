# Task 21 — MapValidator (3주차)

> 선행: Task 01, 03 · 후행: Task 22(절차 생성기의 리롤 판정)
> 체크리스트: `mds/Checklists/21-MapValidator.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `FMapValidator` |
| 부모 클래스 | 없음 — static 순수 함수 모음 struct |
| 역할 | 생성된 맵의 유효성 검증 (GDD 4.2 체크리스트 5개). 각 항목을 **독립 함수**로 분리해 개별 테스트 가능하게. 불통과 시 생성기가 리롤 |

## 생성 파일

- `Source/CrazyArcade3D/MapGen/MapValidator.h/.cpp`

## 구현 명세

```cpp
// MapValidator.h
// 전부 static 순수 함수 — 그리드를 읽기만 한다. 정수 연산·TArray만 (결정론).
struct FMapValidator
{
    // 종합 판정. 실패 시 OutReason에 어느 검사가 왜 실패했는지 기록 (리롤 로그·디버깅용).
    static bool Validate(const FVoxelGrid& Grid,
                         const TArray<FIntVector>& Spawns,
                         const TArray<FItemPlacement>& Items,
                         FString& OutReason);

    // ─── GDD 4.2의 5개 검사 — 각각 독립 함수 ───

    // 1. 모든 층의 도달 가능 영역이 점프(1블록 오르기)로 연결되는가.
    //    이동 그래프: 인접 셀(±X,±Y) + 1블록 상승 + 낙하. BFS.
    static bool AreAllLayersReachable(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns);

    // 2. 스폰 8개 상호 최소 거리 확보 (거리 기준은 맨해튼 — 정수).
    static bool HaveSpawnsMinDistance(const TArray<FIntVector>& Spawns, int32 MinManhattan);

    // 3. 각 스폰 주변 탈출로 2방향 이상 (막힌 스폰 = 시작하자마자 죽는 자리).
    static bool HaveSpawnsEscapeRoutes(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns);

    // 4. 고립된 빈 구역 없음 — 도달 가능 셀 집합이 전체 빈 셀과 일치하는가.
    static bool HasNoIsolatedRegion(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns);

    // 5. 아이템 배치 균등도 — 맵을 사분면으로 나눠 편차가 임계 이내인가.
    static bool AreItemsBalanced(const FVoxelGrid& Grid, const TArray<FItemPlacement>& Items);
};
```

**주의**
- 거리·균등도 임계값은 하드코딩하지 말고 함수 파라미터(기본값)로 노출 — 튜닝 대상.
- BFS 방문 순서도 결정론적으로 (방향 배열 고정 순서, `TSet` 대신 방문 플래그 `TArray`).

## 검증 원칙

- 공통 원칙 + 아래.
- **자동화 테스트(PIE 불필요)**: 검사별로 "통과 맵 1 + 실패 맵 1"을 손으로 만들어 5쌍 전부 검증. 특히 ①은 점프로만 닿는 2층 케이스, ④는 벽으로 밀봉된 방 케이스.
- `FallbackMapGenerator`(Task 04) 출력이 `Validate` 전체를 통과하는가 — 폴백 맵이 불통과면 폴백부터 고친다.

## 응답 원칙

- 공통 원칙.
- 5개 검사 × (통과/실패 케이스) = 10개 테스트 결과를 표로 보고한다.
- 임계값 기본치(최소 거리 등)는 제안값임을 명시하고 확정은 질문한다.
