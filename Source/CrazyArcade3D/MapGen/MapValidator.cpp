#include "MapGen/MapValidator.h"

#include "CrazyArcade3D.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelMovement.h" // 이동 규칙(설 수 있는 칸·한 걸음)의 단일 출처 — 봇과 공유한다

// ⚠️ 이동 규칙은 **여기 있지 않다.** IsStandable·인접 판정·"1칸 오르기 / 낙하 무제한"은
// 전부 VoxelMove(Voxel/VoxelMovement.h)로 옮겼고 이 파일은 그것을 호출만 한다.
// ABotController 가 같은 함수를 쓰기 때문이다 — 규칙이 두 벌이면 "검증은 통과했는데
// 봇은 못 가는 지형"이 조용히 생산된다 (실제로 그랬다: 봇의 BFS 는 2칸 낙하를 몰랐다).
// 검증기는 순수 격자 기하(FMoveCaps 기본값)를 쓴다 — 봇의 머리 공간 제약은 얹지 않는다.

bool FMapValidator::IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell)
{
	return VoxelMove::IsStandable(Grid, Cell);
}

void FMapValidator::FloodFillStandable(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns,
                                       TArray<bool>& OutVisited)
{
	const int32 Total = Grid.Size.X * Grid.Size.Y * Grid.Size.Z;
	OutVisited.Reset();
	OutVisited.SetNumZeroed(Total);

	if (Total <= 0)
	{
		return;
	}

	// TArray 를 큐로 — TSet/TMap 순회는 순서가 보장되지 않아 결정론을 깬다 (불변식 4).
	TArray<FIntVector> Queue;
	Queue.Reserve(Total);

	for (const FIntVector& Spawn : Spawns)
	{
		if (!IsStandable(Grid, Spawn))
		{
			continue; // 스폰 자체가 설 수 없는 자리 — ③ 이 따로 잡는다
		}
		const int32 Index = Grid.Index(Spawn);
		if (!OutVisited[Index])
		{
			OutVisited[Index] = true;
			Queue.Add(Spawn);
		}
	}

	TArray<FIntVector> Neighbors;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		VoxelMove::GatherReachableNeighbors(Grid, Queue[Head], Neighbors);
		for (const FIntVector& Next : Neighbors)
		{
			const int32 Index = Grid.Index(Next);
			if (!OutVisited[Index])
			{
				OutVisited[Index] = true;
				Queue.Add(Next);
			}
		}
	}
}

// ─── ① 스폰 연결성 ───────────────────────────────────────────────────────────

bool FMapValidator::AreAllSpawnsConnected(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns)
{
	if (Spawns.Num() <= 1)
	{
		return true; // 연결할 상대가 없다
	}

	// 첫 스폰 **하나만** 시작점으로 삼는다. 전부를 시작점에 넣으면 서로 끊긴 섬이 있어도
	// 각자 자기 섬을 칠하고 전부 방문됨으로 나와 검사가 무의미해진다.
	TArray<bool> Visited;
	FloodFillStandable(Grid, TArray<FIntVector>{ Spawns[0] }, Visited);

	for (int32 i = 1; i < Spawns.Num(); ++i)
	{
		if (!Grid.IsValid(Spawns[i]) || !Visited[Grid.Index(Spawns[i])])
		{
			return false;
		}
	}
	return true;
}

// ─── ② 스폰 최소 거리 ────────────────────────────────────────────────────────

bool FMapValidator::HaveSpawnsMinDistance(const TArray<FIntVector>& Spawns, int32 MinManhattan)
{
	for (int32 i = 0; i < Spawns.Num(); ++i)
	{
		for (int32 j = i + 1; j < Spawns.Num(); ++j)
		{
			const FIntVector D = Spawns[i] - Spawns[j];
			const int32 Manhattan = FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z);
			if (Manhattan < MinManhattan)
			{
				return false;
			}
		}
	}
	return true;
}

// ─── ③ 스폰 탈출로 ───────────────────────────────────────────────────────────

bool FMapValidator::HaveSpawnsEscapeRoutes(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns,
                                           int32 MinDirs)
{
	for (const FIntVector& Spawn : Spawns)
	{
		if (!IsStandable(Grid, Spawn))
		{
			return false; // 설 수도 없는 자리에 스폰
		}

		// 탈출로 = **서로 다른 방향**의 수 (VoxelMove 가 방향당 착지 칸 하나만 돌려주므로
		// 그 개수가 곧 방향 수다). 폭발은 방향으로 퍼지므로 "몇 방향으로 도망칠 수 있나"가
		// 실제 생존 조건이다 — 한 방향에서 여러 높이가 나와도 1로 세는 이유.
		if (VoxelMove::CountEscapeDirections(Grid, Spawn) < MinDirs)
		{
			return false;
		}
	}
	return true;
}

// ─── ④ 고립 구역 ─────────────────────────────────────────────────────────────

bool FMapValidator::HasNoIsolatedRegion(const FVoxelGrid& Grid, const TArray<FIntVector>& Spawns)
{
	TArray<bool> Visited;
	FloodFillStandable(Grid, Spawns, Visited);

	// 맵 전체의 "설 수 있는 칸"이 전부 방문됐는가. 하나라도 안 닿으면 밀봉된 방이 있다는 뜻.
	// 고정 순서 3중 루프 — 인덱스 순회라 결정론적이다.
	//
	// ⚠️ **최외곽 링은 제외한다.** 경계 벽 꼭대기는 "설 수 있는 칸"이면서 도달 불가인데,
	// 그건 버그가 아니라 **설계**다 — 벽을 2층으로 올린 이유가 바로 1칸 점프로 벽 위에
	// 못 올라가게 하려는 것이었다 (FallbackMapGenerator 2단계 주석). 이걸 빼지 않으면
	// 정상적인 맵이 전부 ④ 에서 떨어져 리롤이 무한히 돈다.
	// ④ 가 잡으려는 것은 **플레이 영역 안의 밀봉된 방**이다.
	for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
	{
		for (int32 Y = 1; Y < Grid.Size.Y - 1; ++Y)
		{
			for (int32 X = 1; X < Grid.Size.X - 1; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				if (IsStandable(Grid, Cell) && !Visited[Grid.Index(Cell)])
				{
					return false;
				}
			}
		}
	}
	return true;
}

// ─── ⑤ 아이템 균등도 ─────────────────────────────────────────────────────────

bool FMapValidator::AreItemsBalanced(const FVoxelGrid& Grid, const TArray<FItemPlacement>& Items,
                                     int32 MaxQuadrantDiff)
{
	if (Items.Num() == 0)
	{
		return true; // 아이템이 없으면 쏠릴 것도 없다 (드랍률 0 설정 등)
	}

	// 사분면 개수 — 정수 나눗셈으로 경계를 잡는다 (float 금지).
	const int32 MidX = Grid.Size.X / 2;
	const int32 MidY = Grid.Size.Y / 2;

	int32 Counts[4] = { 0, 0, 0, 0 };
	for (const FItemPlacement& Item : Items)
	{
		const int32 QuadIndex = (Item.Cell.X >= MidX ? 1 : 0) + (Item.Cell.Y >= MidY ? 2 : 0);
		++Counts[QuadIndex];
	}

	int32 Min = Counts[0];
	int32 Max = Counts[0];
	for (int32 i = 1; i < 4; ++i)
	{
		Min = FMath::Min(Min, Counts[i]);
		Max = FMath::Max(Max, Counts[i]);
	}

	return (Max - Min) <= MaxQuadrantDiff;
}

// ─── 종합 ────────────────────────────────────────────────────────────────────

bool FMapValidator::Validate(const FVoxelGrid& Grid,
                             const TArray<FIntVector>& Spawns,
                             const TArray<FItemPlacement>& Items,
                             const FThresholds& Thresholds,
                             FString& OutReason)
{
	if (Grid.Blocks.Num() == 0)
	{
		OutReason = TEXT("그리드가 비어 있음");
		return false;
	}
	if (Spawns.Num() == 0)
	{
		OutReason = TEXT("스폰이 0개");
		return false;
	}

	// 순서에 의미가 있다: 싼 검사부터. ①·④ 는 BFS 라 가장 비싸므로 뒤에 둔다.
	if (!HaveSpawnsMinDistance(Spawns, Thresholds.SpawnMinManhattan))
	{
		OutReason = FString::Printf(TEXT("② 스폰 최소 거리 미달 (기준 %d)"), Thresholds.SpawnMinManhattan);
		return false;
	}
	if (!HaveSpawnsEscapeRoutes(Grid, Spawns, Thresholds.SpawnMinEscapeDirs))
	{
		OutReason = FString::Printf(TEXT("③ 스폰 탈출로 부족 (기준 %d방향)"), Thresholds.SpawnMinEscapeDirs);
		return false;
	}
	if (!AreItemsBalanced(Grid, Items, Thresholds.ItemQuadrantMaxDiff))
	{
		OutReason = FString::Printf(TEXT("⑤ 아이템 사분면 편차 초과 (기준 %d)"), Thresholds.ItemQuadrantMaxDiff);
		return false;
	}
	if (!AreAllSpawnsConnected(Grid, Spawns))
	{
		OutReason = TEXT("① 점프로 닿지 않는 스폰 있음");
		return false;
	}
	if (!HasNoIsolatedRegion(Grid, Spawns))
	{
		OutReason = TEXT("④ 고립된 빈 구역 있음");
		return false;
	}

	OutReason.Reset();
	return true;
}
