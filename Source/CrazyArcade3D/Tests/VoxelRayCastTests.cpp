// VoxelRay::GatherSolidCells 자동화 테스트 (가림 디더 페이드).
// -ExecCmds="Automation RunTests CrazyArcade3D.Voxel.VoxelRayCast" 로 실행.
//
// 순수 함수라 월드도 액터도 필요 없다 — 손으로 만든 FVoxelGrid 하나면 끝난다
// (UExplosionSubsystem::Propagate 테스트와 같은 구조).
//
// 여기서 지키려는 것은 하나다: **직선이 지나는 칸을 하나도 빠뜨리지 않는다.**
// 대각선으로 훑다가 사이에 낀 블록을 건너뛰면 그 블록만 안 옅어져서 캐릭터가 반쯤 가린 채
// 남는다 — 눈에는 "가끔 안 된다"로 보이고 원인을 찾기 어렵다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelRayCast.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelRayCastTest, "CrazyArcade3D.Voxel.VoxelRayCast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 헬퍼 이름에 Vrc 접두사 — 번역 단위 병합 시 다른 테스트 .cpp 와 충돌 방지.
	constexpr int32 VrcMaxSteps = 256;

	FVoxelGrid VrcMakeEmptyGrid()
	{
		FVoxelGrid Grid;
		Grid.Init(FIntVector(8, 8, 4));
		return Grid;
	}

	bool VrcContains(const TArray<FIntVector>& Cells, int32 X, int32 Y, int32 Z)
	{
		return Cells.Contains(FIntVector(X, Y, Z));
	}
}

bool FVoxelRayCastTest::RunTest(const FString& Parameters)
{
	// ─── ① 빈 그리드: 아무것도 안 나온다 ───
	{
		const FVoxelGrid Grid = VrcMakeEmptyGrid();
		TArray<FIntVector> Hits;
		VoxelRay::GatherSolidCells(Grid, FVector(0.5, 0.5, 0.5), FVector(7.5, 7.5, 3.5), VrcMaxSteps, Hits);
		TestEqual(TEXT("① 빈 그리드: 걸리는 칸 0"), Hits.Num(), 0);
	}

	// ─── ② 축 정렬 직선: 사이에 낀 블록을 전부 집는다 ───
	{
		FVoxelGrid Grid = VrcMakeEmptyGrid();
		Grid.Set(FIntVector(2, 0, 0), EBlockType::Destructible);
		Grid.Set(FIntVector(4, 0, 0), EBlockType::Destructible);
		Grid.Set(FIntVector(6, 0, 0), EBlockType::Destructible); // 도착점 너머

		TArray<FIntVector> Hits;
		// x=0.5 → x=5.5 (셀 5). 6번 칸은 선분 밖이므로 들어오면 안 된다.
		VoxelRay::GatherSolidCells(Grid, FVector(0.5, 0.5, 0.5), FVector(5.5, 0.5, 0.5), VrcMaxSteps, Hits);

		TestTrue(TEXT("② 축 정렬: (2,0,0) 포함"), VrcContains(Hits, 2, 0, 0));
		TestTrue(TEXT("② 축 정렬: (4,0,0) 포함"), VrcContains(Hits, 4, 0, 0));
		TestFalse(TEXT("② 축 정렬: 선분 너머 (6,0,0) 미포함 — 캐릭터 뒤 블록은 가림이 아니다"),
			VrcContains(Hits, 6, 0, 0));
		TestEqual(TEXT("② 축 정렬: 정확히 2칸"), Hits.Num(), 2);
	}

	// ─── ③ 대각선: 칸을 건너뛰지 않는다 (이 함수의 존재 이유) ───
	{
		FVoxelGrid Grid = VrcMakeEmptyGrid();
		// (0,0) → (3,3) 대각선 통로를 벽으로 채운다. 순진한 구현은 (1,1),(2,2) 만 집고
		// 그 사이의 (1,0)/(0,1) 같은 칸을 건너뛴다.
		for (int32 X = 0; X < 4; ++X)
		{
			for (int32 Y = 0; Y < 4; ++Y)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Destructible);
			}
		}

		TArray<FIntVector> Hits;
		VoxelRay::GatherSolidCells(Grid, FVector(0.5, 0.5, 0.5), FVector(3.5, 3.5, 0.5), VrcMaxSteps, Hits);

		// 완전 대각선은 매 걸음 한 축씩 번갈아 넘으므로 (0,0)→(3,3) 사이에 최소 7칸을 지난다.
		TestTrue(TEXT("③ 대각선: 최소 7칸 (건너뛰기 없음)"), Hits.Num() >= 7);
		TestTrue(TEXT("③ 대각선: 시작 칸 포함"), VrcContains(Hits, 0, 0, 0));
		TestTrue(TEXT("③ 대각선: 끝 칸 포함"), VrcContains(Hits, 3, 3, 0));

		// 인접성 검사 — 연속한 두 칸은 정확히 한 축으로 1만 달라야 한다.
		bool bAllAdjacent = true;
		for (int32 i = 1; i < Hits.Num(); ++i)
		{
			const FIntVector D = Hits[i] - Hits[i - 1];
			const int32 Manhattan = FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z);
			if (Manhattan != 1)
			{
				bAllAdjacent = false;
				break;
			}
		}
		TestTrue(TEXT("③ 대각선: 연속한 칸이 전부 한 칸씩만 이동 (DDA 무결성)"), bAllAdjacent);
	}

	// ─── ④ 그리드 밖에서 출발 (카메라는 보통 맵 밖 하늘에 있다) ───
	{
		FVoxelGrid Grid = VrcMakeEmptyGrid();
		Grid.Set(FIntVector(3, 3, 2), EBlockType::Immortal);

		TArray<FIntVector> Hits;
		// z = 10 (그리드 밖, Size.Z = 4) 에서 수직 하강.
		VoxelRay::GatherSolidCells(Grid, FVector(3.5, 3.5, 10.0), FVector(3.5, 3.5, 0.5), VrcMaxSteps, Hits);

		TestTrue(TEXT("④ 격자 밖 출발: 범위 밖 구간에서 죽지 않고 (3,3,2) 를 집는다"),
			VrcContains(Hits, 3, 3, 2));
		TestEqual(TEXT("④ 격자 밖 출발: solid 는 그 한 칸뿐"), Hits.Num(), 1);
	}

	// ─── ⑤ 퇴화 입력: 크래시도 무한 루프도 없다 ───
	{
		const FVoxelGrid Grid = VrcMakeEmptyGrid();
		TArray<FIntVector> Hits;

		VoxelRay::GatherSolidCells(Grid, FVector(2.5, 2.5, 1.5), FVector(2.5, 2.5, 1.5), VrcMaxSteps, Hits);
		TestEqual(TEXT("⑤ 시작 == 끝: 빈 결과"), Hits.Num(), 0);

		VoxelRay::GatherSolidCells(Grid, FVector(0.5, 0.5, 0.5), FVector(7.5, 7.5, 3.5), 0, Hits);
		TestEqual(TEXT("⑤ MaxSteps 0: 빈 결과"), Hits.Num(), 0);
	}

	// ─── ⑥ 역방향도 같은 칸 집합 (카메라가 어느 쪽에 있든 동일해야 한다) ───
	{
		FVoxelGrid Grid = VrcMakeEmptyGrid();
		Grid.Set(FIntVector(2, 1, 0), EBlockType::Destructible);
		Grid.Set(FIntVector(4, 2, 1), EBlockType::Destructible);

		const FVector A(0.5, 0.5, 0.5);
		const FVector B(6.5, 3.5, 2.5);

		TArray<FIntVector> Forward, Backward;
		VoxelRay::GatherSolidCells(Grid, A, B, VrcMaxSteps, Forward);
		VoxelRay::GatherSolidCells(Grid, B, A, VrcMaxSteps, Backward);

		TestEqual(TEXT("⑥ 역방향: 집힌 칸 수 동일"), Forward.Num(), Backward.Num());

		bool bSameSet = Forward.Num() == Backward.Num();
		for (const FIntVector& Cell : Forward)
		{
			if (!Backward.Contains(Cell)) { bSameSet = false; break; }
		}
		TestTrue(TEXT("⑥ 역방향: 같은 칸 집합"), bSameSet);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
