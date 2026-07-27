// UFallbackMapGenerator 자동화 테스트 (Task 04).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.MapGen.FallbackMapGenerator"로 실행.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "MapGen/FallbackMapGenerator.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelGrid.h"

#if WITH_AUTOMATION_TESTS

namespace CA3DFallbackMapTest
{
	// 블록 종류 → ASCII 문자. Empty='.', Floor='_', Destructible='D', Immortal='#'.
	static TCHAR BlockChar(EBlockType T)
	{
		switch (T)
		{
		case EBlockType::Floor:        return TEXT('_');
		case EBlockType::Destructible: return TEXT('D');
		case EBlockType::Immortal:     return TEXT('#');
		default:                       return TEXT('.');
		}
	}

	// 층별 ASCII 맵 + 블록 종류별 개수 덤프. 테스트 성패와 무관하게 항상 호출한다.
	static void DumpGrid(const FVoxelGrid& Grid)
	{
		for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
		{
			UE_LOG(LogCA3D, Display, TEXT("=== FallbackMap z=%d (위가 +Y) ==="), Z);
			for (int32 Y = Grid.Size.Y - 1; Y >= 0; --Y)
			{
				FString Line = FString::Printf(TEXT("y=%02d "), Y);
				for (int32 X = 0; X < Grid.Size.X; ++X)
				{
					Line.AppendChar(BlockChar(Grid.Get(FIntVector(X, Y, Z))));
				}
				UE_LOG(LogCA3D, Display, TEXT("%s"), *Line);
			}
		}

		int32 Counts[4] = { 0, 0, 0, 0 }; // Empty, Floor, Destructible, Immortal
		for (const uint8 Block : Grid.Blocks)
		{
			if (Block < 4)
			{
				++Counts[Block];
			}
		}
		UE_LOG(LogCA3D, Display, TEXT("블록 개수 — Empty:%d Floor:%d Destructible:%d Immortal:%d (총 %d)"),
			Counts[0], Counts[1], Counts[2], Counts[3], Grid.Blocks.Num());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFallbackMapGeneratorTest, "CrazyArcade3D.MapGen.FallbackMapGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FFallbackMapGeneratorTest::RunTest(const FString& Parameters)
{
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>();          // 기본 MapSize = 21×21×4
	UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();

	FVoxelGrid Grid;
	TArray<FIntVector> Spawns;
	TArray<FItemPlacement> Items;

	// --- 1. Generate 성공 ---
	const bool bGenerated = Generator->Generate(12345u, Rules, Grid, Spawns, Items);
	TestTrue(TEXT("Generate: true 반환"), bGenerated);

	// --- 8. 그리드 로그 덤프 (성패와 무관하게 항상) ---
	if (Grid.Blocks.Num() > 0)
	{
		CA3DFallbackMapTest::DumpGrid(Grid);
	}
	else
	{
		UE_LOG(LogCA3D, Display, TEXT("FallbackMap: 그리드가 비어 있어 덤프 생략 (Generate 실패)"));
	}
	for (int32 I = 0; I < Spawns.Num(); ++I)
	{
		UE_LOG(LogCA3D, Display, TEXT("스폰 %d: (%d, %d, %d)"), I, Spawns[I].X, Spawns[I].Y, Spawns[I].Z);
	}

	if (!bGenerated)
	{
		return false; // 이후 검증은 무의미
	}

	const FIntVector Size = Rules->MapSize;

	// --- 2a. z=0 전 칸 Floor ---
	bool bFloorOK = true;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			if (Grid.Get(FIntVector(X, Y, 0)) != EBlockType::Floor)
			{
				bFloorOK = false;
			}
		}
	}
	TestTrue(TEXT("z=0: 전 칸 Floor"), bFloorOK);

	// --- 2b. 외곽 둘레(z=1) 전부 Immortal ---
	bool bWallOK = true;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			const bool bPerimeter = (X == 0 || X == Size.X - 1 || Y == 0 || Y == Size.Y - 1);
			if (bPerimeter && Grid.Get(FIntVector(X, Y, 1)) != EBlockType::Immortal)
			{
				bWallOK = false;
			}
		}
	}
	TestTrue(TEXT("z=1: 외곽 둘레 전부 Immortal"), bWallOK);

	// --- 3. 스폰 8개, 각 스폰은 Empty이고 아래 칸이 Solid ---
	TestEqual(TEXT("스폰 개수 = 8"), Spawns.Num(), 8);
	for (const FIntVector& Spawn : Spawns)
	{
		TestEqual(FString::Printf(TEXT("스폰 (%d,%d,%d): Empty"), Spawn.X, Spawn.Y, Spawn.Z),
			Grid.Get(Spawn), EBlockType::Empty);
		TestTrue(FString::Printf(TEXT("스폰 (%d,%d,%d): 아래 칸 IsSolid"), Spawn.X, Spawn.Y, Spawn.Z),
			Grid.IsSolid(Spawn + FIntVector(0, 0, -1)));

		// 탈출로: 인접 4방향 중 Empty가 2방향 이상.
		const FIntVector Dirs[4] = {
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };
		int32 EscapeCount = 0;
		for (const FIntVector& Dir : Dirs)
		{
			const FIntVector Neighbor = Spawn + Dir;
			if (Grid.IsValid(Neighbor) && Grid.Get(Neighbor) == EBlockType::Empty)
			{
				++EscapeCount;
			}
		}
		TestTrue(FString::Printf(TEXT("스폰 (%d,%d,%d): 탈출로 2방향 이상"), Spawn.X, Spawn.Y, Spawn.Z),
			EscapeCount >= 2);
	}

	// --- 4. 스폰 간 최소 쌍 거리 (정수 맨해튼) ---
	int32 MinPairDist = MAX_int32;
	for (int32 I = 0; I < Spawns.Num(); ++I)
	{
		for (int32 J = I + 1; J < Spawns.Num(); ++J)
		{
			const FIntVector D = Spawns[I] - Spawns[J];
			const int32 Dist = FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z);
			MinPairDist = FMath::Min(MinPairDist, Dist);
		}
	}
	UE_LOG(LogCA3D, Display, TEXT("스폰 최소 쌍 거리(맨해튼): %d"), MinPairDist);
	TestTrue(TEXT("스폰 최소 쌍 거리 >= 6"), MinPairDist >= 6);

	// --- 5. 결정론: 다른 Seed로 두 번째 호출 → 비트 단위 동일 (Seed는 무시되어야 함) ---
	FVoxelGrid Grid2;
	TArray<FIntVector> Spawns2;
	TArray<FItemPlacement> Items2;
	const bool bGenerated2 = Generator->Generate(99999u, Rules, Grid2, Spawns2, Items2);
	TestTrue(TEXT("결정론: 2회차 Generate도 true"), bGenerated2);
	TestEqual(TEXT("결정론: Blocks 크기 동일"), Grid.Blocks.Num(), Grid2.Blocks.Num());
	TestTrue(TEXT("결정론: Blocks 비트 단위 동일"),
		Grid.Blocks.Num() == Grid2.Blocks.Num()
		&& FMemory::Memcmp(Grid.Blocks.GetData(), Grid2.Blocks.GetData(), Grid.Blocks.Num()) == 0);
	TestTrue(TEXT("결정론: Spawns 동일"), Spawns == Spawns2);

	// --- 6. 2층 이상(z>=2) 블록 존재 + 계단 접근로 구성 검증 ---
	int32 UpperBlockCount = 0;
	for (int32 Z = 2; Z < Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				if (Grid.Get(FIntVector(X, Y, Z)) != EBlockType::Empty)
				{
					++UpperBlockCount;
				}
			}
		}
	}
	TestTrue(TEXT("z>=2 에 블록 존재"), UpperBlockCount > 0);

	// 계단 좌표 — FallbackMapGenerator.cpp 4단계 주석과 동일 공식. 기본 21×21×4 → (8,9)/(9,9)/(10,9).
	const int32 CX = Size.X / 2;
	const int32 CY = Size.Y / 2;
	const FIntVector StairApproach(CX - 2, CY - 1, 1);
	const FIntVector StairStep1(CX - 1, CY - 1, 1);
	const FIntVector StairStep2Low(CX, CY - 1, 1);
	const FIntVector StairStep2High(CX, CY - 1, 2);
	UE_LOG(LogCA3D, Display, TEXT("계단 접근로 — 접근(%d,%d,1) 1단(%d,%d,1) 2단 스택(%d,%d,1~2)"),
		StairApproach.X, StairApproach.Y, StairStep1.X, StairStep1.Y, StairStep2Low.X, StairStep2Low.Y);

	TestEqual(TEXT("계단: 접근 칸 Empty"), Grid.Get(StairApproach), EBlockType::Empty);
	TestEqual(TEXT("계단: 접근 칸 위(z=2) Empty (머리 공간)"),
		Grid.Get(StairApproach + FIntVector(0, 0, 1)), EBlockType::Empty);
	TestEqual(TEXT("계단: 1단 블록(z=1) Immortal"), Grid.Get(StairStep1), EBlockType::Immortal);
	TestEqual(TEXT("계단: 1단 위(z=2) Empty (착지 공간)"),
		Grid.Get(StairStep1 + FIntVector(0, 0, 1)), EBlockType::Empty);
	TestEqual(TEXT("계단: 2단 스택 z=1 Immortal"), Grid.Get(StairStep2Low), EBlockType::Immortal);
	TestEqual(TEXT("계단: 2단 스택 z=2 Immortal"), Grid.Get(StairStep2High), EBlockType::Immortal);
	TestEqual(TEXT("계단: 2단 위(z=3) Empty (착지 공간)"),
		Grid.Get(StairStep2High + FIntVector(0, 0, 1)), EBlockType::Empty);

	// --- 7. OutItems 비어 있음 (아이템 배치는 Task 23) ---
	TestEqual(TEXT("OutItems 비어 있음"), Items.Num(), 0);

	// --- 추가: Destructible 존재 / nullptr Rules 방어 ---
	int32 DestructibleCount = 0;
	for (const uint8 Block : Grid.Blocks)
	{
		if (Block == static_cast<uint8>(EBlockType::Destructible))
		{
			++DestructibleCount;
		}
	}
	TestTrue(TEXT("Destructible 블록 다수 존재"), DestructibleCount > 0);

	FVoxelGrid NullGrid;
	TArray<FIntVector> NullSpawns;
	TArray<FItemPlacement> NullItems;
	TestFalse(TEXT("Rules == nullptr → false"),
		Generator->Generate(0u, nullptr, NullGrid, NullSpawns, NullItems));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
