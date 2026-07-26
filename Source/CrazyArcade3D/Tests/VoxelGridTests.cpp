// FVoxelGrid 자동화 테스트 (Task 01).
// 에디터 세션 프론트엔드(Automation 탭) 또는 -ExecCmds="Automation RunTests CrazyArcade3D.Voxel.VoxelGrid"로 실행.

#include "Misc/AutomationTest.h"
#include "Voxel/VoxelGrid.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelGridTest, "CrazyArcade3D.Voxel.VoxelGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FVoxelGridTest::RunTest(const FString& Parameters)
{
	const FIntVector GridSize(21, 21, 4);

	// --- 1. Init 후 전 칸 Empty ---
	FVoxelGrid Grid;
	Grid.Init(GridSize);

	TestEqual(TEXT("Init: 배열 크기 = X*Y*Z"), Grid.Blocks.Num(), GridSize.X * GridSize.Y * GridSize.Z);

	bool bAllEmpty = true;
	for (int32 Z = 0; Z < GridSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (int32 X = 0; X < GridSize.X; ++X)
			{
				if (Grid.Get(FIntVector(X, Y, Z)) != EBlockType::Empty)
				{
					bAllEmpty = false;
				}
			}
		}
	}
	TestTrue(TEXT("Init: 전 칸이 Empty"), bAllEmpty);

	// --- 2. Set → Get 왕복 일치 (블록 3종 각각) ---
	const FIntVector FloorCell(1, 2, 0);
	const FIntVector DestructibleCell(10, 10, 1);
	const FIntVector ImmortalCell(20, 20, 3); // 최댓값 모서리 셀

	Grid.Set(FloorCell, EBlockType::Floor);
	Grid.Set(DestructibleCell, EBlockType::Destructible);
	Grid.Set(ImmortalCell, EBlockType::Immortal);

	TestEqual(TEXT("Set→Get: Floor"), Grid.Get(FloorCell), EBlockType::Floor);
	TestEqual(TEXT("Set→Get: Destructible"), Grid.Get(DestructibleCell), EBlockType::Destructible);
	TestEqual(TEXT("Set→Get: Immortal"), Grid.Get(ImmortalCell), EBlockType::Immortal);

	// --- 3. 범위 밖 Get == Empty (음수·초과 좌표 모두) ---
	TestEqual(TEXT("범위 밖 Get: X 음수"), Grid.Get(FIntVector(-1, 0, 0)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: Y 음수"), Grid.Get(FIntVector(0, -1, 0)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: Z 음수"), Grid.Get(FIntVector(0, 0, -1)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: X 초과"), Grid.Get(FIntVector(GridSize.X, 0, 0)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: Y 초과"), Grid.Get(FIntVector(0, GridSize.Y, 0)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: Z 초과"), Grid.Get(FIntVector(0, 0, GridSize.Z)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: 전 축 음수"), Grid.Get(FIntVector(-5, -5, -5)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Get: 전 축 초과"), Grid.Get(FIntVector(100, 100, 100)), EBlockType::Empty);

	// --- 4. 범위 밖 Set 크래시 없이 무시 ---
	Grid.Set(FIntVector(-1, -1, -1), EBlockType::Immortal);
	Grid.Set(FIntVector(GridSize.X, GridSize.Y, GridSize.Z), EBlockType::Immortal);
	Grid.Set(FIntVector(0, 0, 999), EBlockType::Immortal);

	// 무시되었으므로 배열 내용이 변하지 않아야 한다 — 인접 유효 셀 재확인.
	TestEqual(TEXT("범위 밖 Set 후: (0,0,0) 그대로 Empty"), Grid.Get(FIntVector(0, 0, 0)), EBlockType::Empty);
	TestEqual(TEXT("범위 밖 Set 후: 모서리 셀 그대로 Immortal"), Grid.Get(ImmortalCell), EBlockType::Immortal);

	// --- 5. Index ↔ 좌표 왕복 무결성 (모서리 셀 포함) ---
	const FIntVector Corners[] =
	{
		FIntVector(0, 0, 0),
		FIntVector(GridSize.X - 1, 0, 0),
		FIntVector(0, GridSize.Y - 1, 0),
		FIntVector(0, 0, GridSize.Z - 1),
		FIntVector(GridSize.X - 1, GridSize.Y - 1, 0),
		FIntVector(GridSize.X - 1, 0, GridSize.Z - 1),
		FIntVector(0, GridSize.Y - 1, GridSize.Z - 1),
		FIntVector(GridSize.X - 1, GridSize.Y - 1, GridSize.Z - 1),
		FIntVector(7, 13, 2), // 임의 내부 셀
	};

	bool bIndexRoundTripOK = true;
	for (const FIntVector& C : Corners)
	{
		const int32 Idx = Grid.Index(C);

		// 인덱스가 배열 범위 안인가.
		if (Idx < 0 || Idx >= Grid.Blocks.Num())
		{
			bIndexRoundTripOK = false;
			continue;
		}

		// 인덱스 → 좌표 역변환 후 원래 좌표와 일치하는가.
		const FIntVector Decoded(
			Idx % GridSize.X,
			(Idx / GridSize.X) % GridSize.Y,
			Idx / (GridSize.X * GridSize.Y));
		if (Decoded != C)
		{
			bIndexRoundTripOK = false;
		}
	}
	TestTrue(TEXT("Index ↔ 좌표 왕복 무결성 (모서리 8곳 + 내부 1곳)"), bIndexRoundTripOK);

	// 전 셀 인덱스 유일성 — 인덱스 공식이 셀을 겹치지 않게 매핑하는가.
	TArray<uint8> Visited;
	Visited.Init(0, Grid.Blocks.Num());
	bool bIndexUnique = true;
	for (int32 Z = 0; Z < GridSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < GridSize.Y; ++Y)
		{
			for (int32 X = 0; X < GridSize.X; ++X)
			{
				const int32 Idx = Grid.Index(FIntVector(X, Y, Z));
				if (Idx < 0 || Idx >= Visited.Num() || Visited[Idx] != 0)
				{
					bIndexUnique = false;
				}
				else
				{
					Visited[Idx] = 1;
				}
			}
		}
	}
	TestTrue(TEXT("Index: 전 셀 유일 매핑"), bIndexUnique);

	// --- 6. IsSolid / BlocksExplosion 블록 종류별 판정 ---
	FVoxelGrid JudgeGrid;
	JudgeGrid.Init(FIntVector(3, 3, 1));
	const FIntVector EmptyCell(0, 0, 0);
	const FIntVector JFloor(1, 0, 0);
	const FIntVector JDestructible(2, 0, 0);
	const FIntVector JImmortal(0, 1, 0);
	JudgeGrid.Set(JFloor, EBlockType::Floor);
	JudgeGrid.Set(JDestructible, EBlockType::Destructible);
	JudgeGrid.Set(JImmortal, EBlockType::Immortal);

	TestFalse(TEXT("IsSolid: Empty = false"), JudgeGrid.IsSolid(EmptyCell));
	TestTrue(TEXT("IsSolid: Floor = true"), JudgeGrid.IsSolid(JFloor));
	TestTrue(TEXT("IsSolid: Destructible = true"), JudgeGrid.IsSolid(JDestructible));
	TestTrue(TEXT("IsSolid: Immortal = true"), JudgeGrid.IsSolid(JImmortal));
	TestFalse(TEXT("IsSolid: 범위 밖 = false"), JudgeGrid.IsSolid(FIntVector(-1, 0, 0)));

	TestFalse(TEXT("BlocksExplosion: Empty = false"), JudgeGrid.BlocksExplosion(EmptyCell));
	TestFalse(TEXT("BlocksExplosion: Floor = false"), JudgeGrid.BlocksExplosion(JFloor));
	TestFalse(TEXT("BlocksExplosion: Destructible = false"), JudgeGrid.BlocksExplosion(JDestructible));
	TestTrue(TEXT("BlocksExplosion: Immortal = true"), JudgeGrid.BlocksExplosion(JImmortal));
	TestFalse(TEXT("BlocksExplosion: 범위 밖 = false"), JudgeGrid.BlocksExplosion(FIntVector(99, 0, 0)));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
