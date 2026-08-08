#include "Voxel/VoxelMovement.h"

#include "Voxel/VoxelGrid.h"

namespace VoxelMove
{
	// ⚠️ 순서 고정 — 헤더 주석 참조. +X → -X → +Y → -Y.
	// (MapValidator.cpp 의 MvHorizontal, BotController.cpp 의 BotPlanarDirs 가 각자 들고 있던
	//  같은 배열이다. 두 벌이 있으면 언젠가 한쪽만 바뀌어 검증기와 봇이 다른 길을 본다.)
	const FIntVector PlanarDirs[4] = {
		FIntVector( 1,  0, 0),
		FIntVector(-1,  0, 0),
		FIntVector( 0,  1, 0),
		FIntVector( 0, -1, 0),
	};

	bool IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell, const FMoveCaps& Caps)
	{
		if (!Grid.IsValid(Cell))
		{
			return false;
		}

		// 그 칸이 비어 있고, 바로 아래가 solid — "발판 위 공중 칸"이 플레이 공간이다.
		// z=0 은 발판이 없으므로(그 아래가 없음) 설 수 없다.
		if (Grid.Get(Cell) != EBlockType::Empty
			|| Cell.Z <= 0
			|| !Grid.IsSolid(FIntVector(Cell.X, Cell.Y, Cell.Z - 1)))
		{
			return false;
		}

		// 머리 공간 — 봇만 요구한다 (헤더 FMoveCaps 주석: 왜 검증기는 안 보는가).
		if (Caps.bRequireHeadroom && Grid.IsSolid(FIntVector(Cell.X, Cell.Y, Cell.Z + 1)))
		{
			return false;
		}

		return true;
	}

	bool FindLandingInColumn(const FVoxelGrid& Grid, const FIntVector& From, const FIntVector& Dir,
	                         FIntVector& OutCell, const FMoveCaps& Caps)
	{
		const FIntVector Column = From + Dir;

		// X/Y 만 본다 — Z 는 아래 루프가 그리드 안에서만 돈다.
		if (Column.X < 0 || Column.X >= Grid.Size.X || Column.Y < 0 || Column.Y >= Grid.Size.Y)
		{
			return false;
		}

		// **걸어 들어갈 수 있는가** — 오르지 않고 지금 높이 그대로 옆 칸에 들어서는 경우,
		// 몸은 From.Z 와 그 머리 칸(From.Z+1) 두 칸을 차지한다. 머리 칸이 막혀 있으면 못 들어간다.
		// 머리 공간을 요구하지 않는 쪽(검증기)은 몸을 한 점으로 보므로 이 제약이 없다.
		// (오르는 경우는 IsStandable 의 머리 공간 검사가 착지 칸 기준으로 이미 본다.)
		//
		// 이게 없으면 봇이 "머리 위가 막힌 기둥으로 걸어 들어가 그 밑으로 떨어지는" 경로를
		// 계획하고, 실제로는 블록에 막혀 그 자리에서 계속 벽을 민다.
		const bool bCanWalkIn = !Caps.bRequireHeadroom
			|| !Grid.IsSolid(FIntVector(Column.X, Column.Y, From.Z + 1));

		// 오르기 상한(From.Z + MaxClimbCells)에서 바닥(0)까지 **위에서 아래로**.
		// 낙하는 상한이 없으므로 아래쪽은 끝까지 훑되, 처음 만난 발판에서 멈춘다 —
		// 그게 실제 착지 칸이고 그 아래는 이 발판에 막혀 갈 수 없다.
		for (int32 Z = FMath::Min(From.Z + MaxClimbCells, Grid.Size.Z - 1); Z >= 0; --Z)
		{
			// 지금 높이 이하로 내려서는 순간부터는 "걸어 들어가기"다 — 머리 칸이 막혔으면 여기서 끝.
			if (Z <= From.Z && !bCanWalkIn)
			{
				return false;
			}

			const FIntVector Candidate(Column.X, Column.Y, Z);
			if (IsStandable(Grid, Candidate, Caps))
			{
				OutCell = Candidate;
				return true;
			}

			// **몸이 들어갈 수 없는 칸을 만나면 그 아래로는 못 내려간다.**
			// 단 오르기 후보(Z > From.Z)는 예외다 — 거기가 막혔다는 건 "못 올라간다"는 뜻일 뿐,
			// 원래 서 있던 높이로 걸어 들어가는 길은 아직 살아 있다.
			//
			// 기둥을 아래에서 위로 연속으로 채우는 맵 생성기 출력에서는 이 조기 종료가
			// 아무것도 바꾸지 않는다 (solid 아래는 다시 solid 라 어차피 발판이 안 나온다).
			// 필요해지는 것은 **폭발이 기둥 중간을 뚫어 오버행이 생긴 런타임**이고, 그때 이게
			// 없으면 봇이 벽을 통과해 그 밑으로 떨어지는 경로를 계획한다.
			if (Z <= From.Z && Grid.IsSolid(Candidate))
			{
				return false;
			}
		}
		return false;
	}

	void GatherReachableNeighbors(const FVoxelGrid& Grid, const FIntVector& From,
	                              TArray<FIntVector>& Out, const FMoveCaps& Caps)
	{
		Out.Reset();

		for (const FIntVector& Dir : PlanarDirs)
		{
			FIntVector Landing;
			if (FindLandingInColumn(Grid, From, Dir, Landing, Caps))
			{
				Out.Add(Landing);
			}
		}
	}

	int32 CountEscapeDirections(const FVoxelGrid& Grid, const FIntVector& From, const FMoveCaps& Caps)
	{
		int32 Dirs = 0;
		for (const FIntVector& Dir : PlanarDirs)
		{
			FIntVector Landing;
			if (FindLandingInColumn(Grid, From, Dir, Landing, Caps))
			{
				++Dirs;
			}
		}
		return Dirs;
	}
}
