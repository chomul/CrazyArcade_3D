#include "Gameplay/Bomb/ExplosionSubsystem.h"

#include "Voxel/VoxelGrid.h"

FExplosionResult UExplosionSubsystem::Propagate(
	const FVoxelGrid& Grid, const FIntVector& Origin, int32 Range,
	bool bFloorDestructible, const TArray<FIntVector>& BombCells)
{
	// 순수 함수 (불변식 2): 입력만 읽고 결과만 반환한다. 멤버·전역·룰셋 접근 금지.
	// 정수 연산과 고정 순회 순서만 사용 — 같은 입력이면 배열 순서까지 같은 출력 (불변식 4 준용).

	FExplosionResult Result;

	// 최악(전 방향 Empty) 크기 = 원점 1 + 6방향 × Range. Range<=0 이어도 안전.
	Result.WaterCells.Reserve(1 + 6 * FMath::Max(Range, 0));

	// 원점 칸. 원점의 폭탄은 "지금 터지는 폭탄" 자신이므로 연쇄 판정하지 않는다 (명세 의사코드).
	Result.WaterCells.Add(Origin);

	// 방향 순서 고정 [+X,-X,+Y,-Y,+Z,-Z] — 결정론의 일부. 바꾸면 시드 재현·프리뷰 대조가 깨진다.
	static const FIntVector Directions[6] =
	{
		FIntVector( 1,  0,  0),
		FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0),
		FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1),
		FIntVector( 0,  0, -1),
	};

	for (const FIntVector& Dir : Directions)
	{
		for (int32 Step = 1; Step <= Range; ++Step)
		{
			const FIntVector Cell = Origin + Dir * Step;

			// 범위 밖은 Grid.Get 이 Empty 를 반환 — 경계 검사 없이 자연스럽게 통과한다
			// (FVoxelGrid 설계 의도). 맵 외곽은 생성기가 Immortal 벽으로 막는다.
			bool bStopThisDirection = false;
			switch (Grid.Get(Cell))
			{
			case EBlockType::Immortal:
				// 막힘 — 셀 미포함.
				bStopThisDirection = true;
				break;

			case EBlockType::Destructible:
				// 부수고 멈춤.
				Result.BrokenCells.Add(Cell);
				bStopThisDirection = true;
				break;

			case EBlockType::Floor:
				// 바닥 규칙(룰셋 bFloorDestructible)에 따라 부수되, 어느 쪽이든 멈춤.
				if (bFloorDestructible)
				{
					Result.BrokenCells.Add(Cell);
				}
				bStopThisDirection = true;
				break;

			case EBlockType::Empty:
			default:
				// 물줄기 통과. 폭탄이 놓인 칸이면 연쇄 표시 — 전파는 계속 (GDD 2.2).
				Result.WaterCells.Add(Cell);
				if (BombCells.Contains(Cell))
				{
					Result.ChainedCells.Add(Cell);
				}
				break;
			}

			if (bStopThisDirection)
			{
				break;
			}
		}
	}

	return Result;
}
