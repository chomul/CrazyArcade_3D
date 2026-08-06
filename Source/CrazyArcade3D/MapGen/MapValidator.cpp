#include "MapGen/MapValidator.h"

#include "CrazyArcade3D.h"
#include "Voxel/VoxelGrid.h"

namespace
{
	// ⚠️ 헬퍼 이름에 Mv 접두사 — 번역 단위 병합(unity build) 시 다른 .cpp 의 익명 네임스페이스
	// 헬퍼와 이름이 겹치면 C2084 가 난다.

	// **고정 순서**가 중요하다 — 이 순서가 BFS 방문 순서를 결정하고, 방문 순서가 흔들리면
	// 같은 시드가 실행마다 다른 판정을 받는다 (불변식 4).
	const FIntVector MvHorizontal[4] = {
		FIntVector( 1,  0, 0),
		FIntVector(-1,  0, 0),
		FIntVector( 0,  1, 0),
		FIntVector( 0, -1, 0),
	};

	// 캐릭터가 서 있는 칸에서 이웃 기둥으로 옮겨갈 때 도달 가능한 칸을 모은다.
	//
	// 점프 규칙(CLAUDE.md 확정): **1칸은 오르고 2칸은 못 오른다.** 아래로는 제한 없음.
	// 그래서 이웃 기둥에서 [Z+1 .. 바닥] 범위의 설 수 있는 칸을 전부 후보로 넣는다.
	void MvGatherNeighbors(const FVoxelGrid& Grid, const FIntVector& From, TArray<FIntVector>& Out)
	{
		Out.Reset();

		for (const FIntVector& Dir : MvHorizontal)
		{
			const FIntVector Column = From + Dir;
			if (Column.X < 0 || Column.X >= Grid.Size.X || Column.Y < 0 || Column.Y >= Grid.Size.Y)
			{
				continue;
			}

			// 위에서부터 훑는다: From.Z + 1 이 "1칸 오르기" 상한, 0 이 바닥.
			// 낙하는 제한이 없으므로 그 기둥의 설 수 있는 칸을 전부 받는다.
			for (int32 Z = FMath::Min(From.Z + 1, Grid.Size.Z - 1); Z >= 0; --Z)
			{
				const FIntVector Candidate(Column.X, Column.Y, Z);
				if (FMapValidator::IsStandable(Grid, Candidate))
				{
					Out.Add(Candidate);

					// **여기서 멈춘다.** 위에서 내려오다 처음 만난 발판이 실제로 착지하는 칸이고,
					// 그 아래는 이 발판에 막혀 갈 수 없다. 계속 훑으면 벽 안쪽 공간까지
					// 도달 가능으로 세어 ④(고립 구역) 검사가 무력해진다.
					break;
				}
			}
		}
	}
}

bool FMapValidator::IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell)
{
	if (!Grid.IsValid(Cell))
	{
		return false;
	}

	// 그 칸이 비어 있고, 바로 아래가 solid — "발판 위 공중 칸"이 플레이 공간이다.
	// z=0 은 발판이 없으므로(그 아래가 없음) 설 수 없다.
	return Grid.Get(Cell) == EBlockType::Empty
		&& Cell.Z > 0
		&& Grid.IsSolid(FIntVector(Cell.X, Cell.Y, Cell.Z - 1));
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
		MvGatherNeighbors(Grid, Queue[Head], Neighbors);
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
	TArray<FIntVector> Neighbors;

	for (const FIntVector& Spawn : Spawns)
	{
		if (!IsStandable(Grid, Spawn))
		{
			return false; // 설 수도 없는 자리에 스폰
		}

		// 탈출로 = **서로 다른 방향**의 수. 한 방향에서 여러 높이가 나와도 1로 센다 —
		// 폭발은 방향으로 퍼지므로 "몇 방향으로 도망칠 수 있나"가 실제 생존 조건이다.
		int32 Dirs = 0;
		for (const FIntVector& Dir : MvHorizontal)
		{
			const FIntVector Column = Spawn + Dir;
			if (Column.X < 0 || Column.X >= Grid.Size.X || Column.Y < 0 || Column.Y >= Grid.Size.Y)
			{
				continue;
			}

			for (int32 Z = FMath::Min(Spawn.Z + 1, Grid.Size.Z - 1); Z >= 0; --Z)
			{
				if (IsStandable(Grid, FIntVector(Column.X, Column.Y, Z)))
				{
					++Dirs;
					break;
				}
			}
		}

		if (Dirs < MinDirs)
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
