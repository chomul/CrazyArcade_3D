#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Item/ItemTypes.h" // FItemPlacement — 데이터 타입 참조까지만 (MapGenerator.h 관례)

struct FVoxelGrid;

// 생성된 맵이 "플레이 가능한가"를 판정하는 순수 함수 모음 (Task 21, GDD 4.2).
//
// **전부 static · 그리드를 읽기만 한다.** 액터도 월드도 룰셋도 모른다 — 그래서
// 절차 생성기의 리롤 루프, 자동화 테스트, 나중에 만들 에디터 맵 검사 툴이 같은 함수를 공유한다
// (UExplosionSubsystem::Propagate 를 순수 함수로 둔 것과 같은 이유).
//
// ⚠️ **정수 연산과 고정 순서 루프만.** 이 판정은 리롤 여부를 가르므로 결과가 흔들리면
// 같은 시드가 어떤 실행에서는 통과하고 어떤 실행에서는 리롤된다 — 그 순간 불변식 4
// (시드 재현)가 깨진다. `TSet` 순회·`FMath::Rand`·float 비교 금지, 방문 표시는 `TArray<bool>`.
struct CRAZYARCADE3D_API FMapValidator
{
	// ─── 임계값 (하드코딩 금지 — 호출부가 룰셋에서 넘긴다) ───────────────────
	//
	// 기본값은 21×21 맵 기준 제안치다. 절차 생성기는 룰셋 값을 넘기고,
	// 테스트는 명시값을 넘긴다 — 기본값에 의존하는 호출부를 만들지 않는다.
	struct FThresholds
	{
		int32 SpawnMinManhattan = 8;    // 스폰 간 최소 맨해튼 거리
		int32 SpawnMinEscapeDirs = 2;   // 스폰 주변 탈출로 최소 방향 수
		int32 ItemQuadrantMaxDiff = 4;  // 사분면 간 아이템 개수 최대 편차
	};

	// 종합 판정. 실패 시 OutReason 에 **어느 검사가 왜** 실패했는지 남긴다 —
	// 리롤률이 높을 때 생성 규칙의 어느 부분을 고쳐야 하는지가 이 문자열에 달려 있다.
	static bool Validate(const FVoxelGrid& Grid,
	                     const TArray<FIntVector>& Spawns,
	                     const TArray<FItemPlacement>& Items,
	                     const FThresholds& Thresholds,
	                     FString& OutReason);

	// ─── GDD 4.2 의 5개 검사 — 각각 독립 함수 (개별 테스트 가능) ─────────────

	// ① 스폰에서 출발해 **점프 규칙으로** 모든 스폰에 닿는가.
	//
	// 이동 그래프가 이 게임의 규칙 그 자체다:
	//   · 같은 층 인접 4방향 (±X, ±Y)
	//   · **1칸 오르기** — 2칸은 못 오른다 (CLAUDE.md 확정값). 이걸 2로 두면
	//     검증은 통과하는데 실제로는 못 올라가는 맵이 나온다
	//   · 낙하는 높이 제한 없음 (아래로는 얼마든지 떨어진다)
	static bool AreAllSpawnsConnected(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns);

	// ② 스폰 상호 최소 거리 (맨해튼 — 정수 연산 유지).
	static bool HaveSpawnsMinDistance(const TArray<FIntVector>& Spawns, int32 MinManhattan);

	// ③ 각 스폰 주변 탈출로가 MinDirs 방향 이상.
	// 막힌 스폰은 시작하자마자 첫 폭탄에 죽는 자리다 — 리롤 사유로 충분하다.
	static bool HaveSpawnsEscapeRoutes(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns,
	                                   int32 MinDirs);

	// ④ 고립된 빈 구역 없음 — 스폰에서 닿는 "설 수 있는 칸" 집합이
	// 맵 전체의 설 수 있는 칸과 일치하는가. 벽으로 밀봉된 방이 있으면 그 안의 아이템은
	// 아무도 못 먹고, 서든데스 낙하만 애꿎게 소비된다.
	static bool HasNoIsolatedRegion(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns);

	// ⑤ 아이템이 사분면에 고르게 흩어졌는가 (한쪽에 몰리면 그 자리가 곧 승패다).
	static bool AreItemsBalanced(const FVoxelGrid& Grid, const TArray<FItemPlacement>& Items,
	                             int32 MaxQuadrantDiff);

	// ─── 공용 헬퍼 (①·④ 가 공유, 테스트도 직접 쓴다) ────────────────────────

	// 캐릭터가 "설 수 있는 칸"인가 — 그 칸이 비었고 **바로 아래가 solid** 여야 한다.
	// 발판 위 공중 칸이 곧 플레이 공간이다 (GDD 2.1 "발판만이 안전하다").
	static bool IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell);

	// 스폰들에서 출발해 이동 규칙으로 닿는 모든 설 수 있는 칸을 표시한다.
	// OutVisited 는 Grid 와 같은 크기의 평탄화 배열 (인덱스 규약도 FVoxelGrid::Index 와 동일).
	// 방향 배열이 고정 순서라 방문 순서까지 결정론적이다.
	static void FloodFillStandable(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns,
	                               TArray<bool>& OutVisited);
};
