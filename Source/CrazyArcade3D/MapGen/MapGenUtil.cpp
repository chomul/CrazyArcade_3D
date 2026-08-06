#include "MapGen/MapGenUtil.h"

#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelGrid.h"
#include "Math/RandomStream.h" // 결정론 난수의 유일한 출처 (불변식 4)

void FMapGenUtil::PlaceItemsInDestructibles(const FVoxelGrid& Grid, const UCA3DRuleSet* Rules,
                                            uint32 Seed, TArray<FItemPlacement>& OutItems)
{
	// 규칙·결정론 근거는 헤더 주석 참조 — 본체는 FallbackMapGenerator 6단계(Task 23)에서
	// 그대로 옮겨 왔다 (Task 22: 폴백·절차 생성기가 같은 본체를 공유).

	OutItems.Reset();

	if (Rules == nullptr)
	{
		return; // 호출부(생성기)가 이미 nullptr 를 걸렀지만 방어적으로 한 번 더
	}

	const int32 DropPercent = FMath::Clamp(Rules->ItemDropPercent, 0, 100);

	// 종류별 가중치 — 순서는 EItemType 선언 순서와 1:1 (인덱스가 곧 종류다).
	const int32 Weights[5] =
	{
		FMath::Max(Rules->ItemWeightBalloon, 0),
		FMath::Max(Rules->ItemWeightPotion,  0),
		FMath::Max(Rules->ItemWeightRoller,  0),
		FMath::Max(Rules->ItemWeightNeedle,  0),
		FMath::Max(Rules->ItemWeightKick,    0),
	};
	int32 WeightTotal = 0;
	for (const int32 Weight : Weights)
	{
		WeightTotal += Weight;
	}

	if (DropPercent <= 0 || WeightTotal <= 0)
	{
		return; // 드랍률 0 또는 가중치 전부 0 — 배치할 것이 없다
	}

	FRandomStream Stream(static_cast<int32>(Seed));

	for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
		{
			for (int32 X = 0; X < Grid.Size.X; ++X)
			{
				const FIntVector Cell(X, Y, Z);
				if (Grid.Get(Cell) != EBlockType::Destructible)
				{
					continue; // 숨길 곳이 아니다 — 난수도 소비하지 않는다 (순서 계약)
				}

				if (Stream.RandRange(0, 99) >= DropPercent)
				{
					continue; // 빈 블록
				}

				// 가중 추첨 — [0, WeightTotal) 을 뽑아 가중치를 순서대로 깎는다.
				int32 Roll = Stream.RandRange(0, WeightTotal - 1);
				int32 TypeIndex = 0;
				for (int32 Index = 0; Index < 5; ++Index)
				{
					if (Roll < Weights[Index])
					{
						TypeIndex = Index;
						break;
					}
					Roll -= Weights[Index];
				}

				FItemPlacement Placement;
				Placement.Cell = Cell;
				Placement.Type = static_cast<EItemType>(TypeIndex);
				OutItems.Add(Placement);
			}
		}
	}
}
