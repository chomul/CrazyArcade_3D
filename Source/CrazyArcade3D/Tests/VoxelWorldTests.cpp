// AVoxelWorld 자동화 테스트 (Task 06).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Voxel.VoxelWorld"로 실행.
//
// 월드를 직접 생성하므로 SmokeFilter(스타트업 자동 실행)가 아닌 EngineFilter를 쓴다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelGrid.h"
#include "MapGen/FallbackMapGenerator.h"
#include "Framework/CA3DRuleSet.h"

#if WITH_AUTOMATION_TESTS

namespace CA3DVoxelWorldTest
{
	// 그리드에서 Destructible 셀을 최대 MaxCount개 수집한다.
	static TArray<FIntVector> FindDestructibleCells(const FVoxelGrid& Grid, int32 MaxCount)
	{
		TArray<FIntVector> Result;
		for (int32 Z = 0; Z < Grid.Size.Z && Result.Num() < MaxCount; ++Z)
		{
			for (int32 Y = 0; Y < Grid.Size.Y && Result.Num() < MaxCount; ++Y)
			{
				for (int32 X = 0; X < Grid.Size.X && Result.Num() < MaxCount; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (Grid.Get(C) == EBlockType::Destructible)
					{
						Result.Add(C);
					}
				}
			}
		}
		return Result;
	}

	// 그리드의 Destructible 개수.
	static int32 CountDestructible(const FVoxelGrid& Grid)
	{
		int32 Count = 0;
		for (const uint8 Block : Grid.Blocks)
		{
			if (Block == static_cast<uint8>(EBlockType::Destructible))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelWorldTest, "CrazyArcade3D.Voxel.VoxelWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVoxelWorldTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → SpawnActor 결과는 항상 HasAuthority()==true.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	// ─── 1. ServerInitFromSeed = FallbackMapGenerator 직접 실행과 비트 단위 동일 ───
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld);
	TestTrue(TEXT("넷드라이버 없음 → HasAuthority"), VoxelWorld->HasAuthority());

	VoxelWorld->ServerInitFromSeed(1234u);

	// 기준 그리드: Task 04 생성기를 기본 룰셋으로 직접 실행.
	FVoxelGrid RefGrid;
	{
		UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>();
		UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();
		TArray<FIntVector> RefSpawns;
		TArray<FItemPlacement> RefItems;
		TestTrue(TEXT("기준 Generate 성공"), Generator->Generate(1234u, Rules, RefGrid, RefSpawns, RefItems));
	}

	const FVoxelGrid& Grid = VoxelWorld->GetGrid();
	TestEqual(TEXT("그리드 Blocks 크기 동일"), Grid.Blocks.Num(), RefGrid.Blocks.Num());
	TestTrue(TEXT("그리드 비트 단위 동일 (Task 04 결과와 일치)"),
		Grid.Blocks.Num() == RefGrid.Blocks.Num()
		&& FMemory::Memcmp(Grid.Blocks.GetData(), RefGrid.Blocks.GetData(), Grid.Blocks.Num()) == 0);

	// ─── 2. 좌표 변환 왕복 (원점 액터 + 비원점 액터) ───
	AVoxelWorld* OffsetWorld = World->SpawnActor<AVoxelWorld>(
		AVoxelWorld::StaticClass(), FVector(500.f, -300.f, 250.f), FRotator::ZeroRotator);
	TestNotNull(TEXT("비원점 AVoxelWorld 스폰"), OffsetWorld);

	const FIntVector TestCells[] = {
		FIntVector(0, 0, 0), FIntVector(20, 20, 3), FIntVector(10, 9, 1),
		FIntVector(0, 20, 0), FIntVector(20, 0, 3), FIntVector(1, 1, 1) };

	AVoxelWorld* Worlds[] = { VoxelWorld, OffsetWorld };
	for (AVoxelWorld* W : Worlds)
	{
		const TCHAR* Label = (W == VoxelWorld) ? TEXT("원점") : TEXT("(500,-300,250)");
		for (const FIntVector& C : TestCells)
		{
			const FVector Center = W->CellToWorld(C);
			const FIntVector RoundTrip = W->WorldToCell(Center);
			TestTrue(FString::Printf(TEXT("%s 액터: WorldToCell(CellToWorld(%d,%d,%d)) 왕복 — 결과 (%d,%d,%d)"),
				Label, C.X, C.Y, C.Z, RoundTrip.X, RoundTrip.Y, RoundTrip.Z), RoundTrip == C);

			const FVector Floor = W->CellToWorldFloor(C);
			TestTrue(FString::Printf(TEXT("%s 액터: (%d,%d,%d) Floor.Z = Center.Z - CellSize/2"),
				Label, C.X, C.Y, C.Z),
				FMath::IsNearlyEqual(Floor.Z, Center.Z - W->CellSize * 0.5, KINDA_SMALL_NUMBER));
			TestTrue(FString::Printf(TEXT("%s 액터: (%d,%d,%d) Floor.X/Y = Center.X/Y (셀 중심 유지)"),
				Label, C.X, C.Y, C.Z),
				FMath::IsNearlyEqual(Floor.X, Center.X, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(Floor.Y, Center.Y, KINDA_SMALL_NUMBER));
		}
	}

	// ─── 3+5. ServerDestroyBlocks → 해당 셀 Empty + Destructible 개수 변화 로그 ───
	const TArray<FIntVector> DestroyCells = CA3DVoxelWorldTest::FindDestructibleCells(Grid, 3);
	TestTrue(TEXT("파괴 대상 Destructible 셀 확보"), DestroyCells.Num() > 0);

	const int32 DestructibleBefore = CA3DVoxelWorldTest::CountDestructible(Grid);
	VoxelWorld->ServerDestroyBlocks(DestroyCells);
	const int32 DestructibleAfter = CA3DVoxelWorldTest::CountDestructible(Grid);

	UE_LOG(LogCA3D, Display, TEXT("파괴 전 Destructible: %d → 파괴 후: %d (요청 %d개)"),
		DestructibleBefore, DestructibleAfter, DestroyCells.Num());

	for (const FIntVector& C : DestroyCells)
	{
		UE_LOG(LogCA3D, Display, TEXT("파괴 셀 (%d, %d, %d)"), C.X, C.Y, C.Z);
		TestEqual(FString::Printf(TEXT("파괴 셀 (%d,%d,%d) == Empty"), C.X, C.Y, C.Z),
			VoxelWorld->GetBlock(C), EBlockType::Empty);
	}
	TestEqual(TEXT("Destructible 개수가 파괴 수만큼 감소"),
		DestructibleAfter, DestructibleBefore - DestroyCells.Num());

	// ─── 4. 파괴 선도착 큐 시뮬레이션 (friend 접근) ───
	// 한계: 이 테스트 월드의 액터는 HasAuthority()==true 라
	// MulticastOnBlocksDestroyed_Implementation 을 직접 호출하면 서버 중복 방지 가드에
	// 걸려 클라 분기(큐 적재)를 타지 못한다. 따라서 friend 로 PendingDestroyQueue 에
	// 셀을 직접 넣고 OnRep_Seed() 를 호출해 "그리드 생성 → 큐 flush" 경로를 검증한다.
	AVoxelWorld* LateInitWorld = World->SpawnActor<AVoxelWorld>();
	TestNotNull(TEXT("선도착 검증용 AVoxelWorld 스폰"), LateInitWorld);

	const TArray<FIntVector> EarlyCells = CA3DVoxelWorldTest::FindDestructibleCells(RefGrid, 2);
	TestTrue(TEXT("선도착 시뮬레이션용 셀 확보"), EarlyCells.Num() > 0);

	// 그리드 초기화 전: 파괴 셀이 큐에 먼저 도착한 상황을 재현.
	LateInitWorld->PendingDestroyQueue.Append(EarlyCells);
	TestFalse(TEXT("초기화 전 bGridInitialized == false"), LateInitWorld->bGridInitialized);

	// Seed 복제 도착 → OnRep_Seed: 그리드 생성 + 큐 flush.
	LateInitWorld->Seed = 1234u;
	LateInitWorld->OnRep_Seed();

	TestTrue(TEXT("OnRep_Seed 후 bGridInitialized == true"), LateInitWorld->bGridInitialized);
	TestEqual(TEXT("OnRep_Seed 후 PendingDestroyQueue 비어 있음"),
		LateInitWorld->PendingDestroyQueue.Num(), 0);
	for (const FIntVector& C : EarlyCells)
	{
		TestEqual(FString::Printf(TEXT("선도착 셀 (%d,%d,%d): flush 후 Empty"), C.X, C.Y, C.Z),
			LateInitWorld->GetBlock(C), EBlockType::Empty);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
