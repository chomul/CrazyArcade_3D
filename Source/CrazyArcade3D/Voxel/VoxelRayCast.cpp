#include "Voxel/VoxelRayCast.h"

#include "Voxel/VoxelGrid.h"

void VoxelRay::GatherSolidCells(const FVoxelGrid& Grid, const FVector& StartCell, const FVector& EndCell,
                                int32 MaxSteps, TArray<FIntVector>& OutCells)
{
	if (MaxSteps <= 0)
	{
		return;
	}

	const FVector Delta = EndCell - StartCell;
	if (Delta.IsNearlyZero())
	{
		return; // 카메라와 캐릭터가 같은 지점 — 가릴 것이 없다
	}

	// 축별 배열로 다루면 "가장 가까운 경계를 가진 축" 선택이 분기 없이 끝난다.
	const double Origin[3] = { StartCell.X, StartCell.Y, StartCell.Z };
	const double Dir[3]    = { Delta.X,     Delta.Y,     Delta.Z     };

	int32 Cell[3] = {
		FMath::FloorToInt32(StartCell.X),
		FMath::FloorToInt32(StartCell.Y),
		FMath::FloorToInt32(StartCell.Z)
	};

	int32  Step[3];
	double TMax[3];   // 다음 경계에 닿는 t (선분을 0~1 로 매개화)
	double TDelta[3]; // 한 칸 전진에 드는 t

	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::IsNearlyZero(Dir[Axis]))
		{
			// 그 축으로는 영원히 경계를 넘지 않는다 — 무한대로 두면 축 선택에서 자연히 밀린다.
			Step[Axis]   = 0;
			TMax[Axis]   = TNumericLimits<double>::Max();
			TDelta[Axis] = TNumericLimits<double>::Max();
			continue;
		}

		Step[Axis] = (Dir[Axis] > 0.0) ? 1 : -1;

		// 진행 방향 쪽 경계면. +방향이면 현재 셀의 위쪽 면, -방향이면 아래쪽 면.
		const double Boundary = (Step[Axis] > 0) ? (Cell[Axis] + 1.0) : static_cast<double>(Cell[Axis]);
		TMax[Axis]   = (Boundary - Origin[Axis]) / Dir[Axis];
		TDelta[Axis] = 1.0 / FMath::Abs(Dir[Axis]);
	}

	for (int32 StepCount = 0; StepCount < MaxSteps; ++StepCount)
	{
		const FIntVector Current(Cell[0], Cell[1], Cell[2]);
		if (Grid.IsSolid(Current))
		{
			OutCells.Add(Current);
		}

		// 가장 가까운 경계를 가진 축으로 한 칸.
		const int32 Axis = (TMax[0] < TMax[1])
			? ((TMax[0] < TMax[2]) ? 0 : 2)
			: ((TMax[1] < TMax[2]) ? 1 : 2);

		if (TMax[Axis] > 1.0)
		{
			break; // 선분 끝(캐릭터)을 지났다 — 그 너머 블록은 가림이 아니다
		}

		Cell[Axis] += Step[Axis];
		TMax[Axis] += TDelta[Axis];
	}
}
