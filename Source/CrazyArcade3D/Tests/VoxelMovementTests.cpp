// VoxelMove 자동화 테스트 (봇 층간 이동 개선).
// -ExecCmds="Automation RunTests CrazyArcade3D.Voxel.VoxelMovement" 로 실행.
//
// 이 스위트가 지키는 것은 **이동 규칙이 한 벌이라는 사실**이다. VoxelMove 는 맵 검증기
// (FMapValidator)와 봇(ABotController)이 함께 쓰는 유일한 인접 규칙이고, 여기가 흔들리면
// "검증은 통과했는데 봇은 못 가는 지형"이 다시 생산된다.
//
// 월드도 액터도 필요 없다 — 손으로 만든 FVoxelGrid 하나면 끝난다 (MapValidatorTests 와 같은 관례).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 VmT~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelMovement.h"
#include "MapGen/MapValidator.h"
#include "MapGen/FallbackMapGenerator.h"
#include "MapGen/ProcMapGenerator.h"
#include "Framework/CA3DRuleSet.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelMovementTest, "CrazyArcade3D.Voxel.VoxelMovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 층계 지형 — x 가 커질수록 기둥이 높아지고, 그 뒤가 절벽이다.
	//
	//   x:   0    1    2    3    4    5    6
	//   기둥 높이(솔리드 최상 z):
	//        0    0    1    2    4    0    0        (0 = 바닥판만)
	//
	// 발 높이(설 수 있는 칸의 z) = 기둥 높이 + 1:
	//        1    1    2    3    5    1    1
	//
	// 의도:
	//   · (1,1,1) → (2,1,2)  : **1칸 오르기** (가능)
	//   · (2,1,2) → (3,1,3)  : 1칸 오르기 (가능)
	//   · (3,1,3) → (4,1,5)  : **2칸 오르기 (불가)** — 여기서 막혀야 한다
	//   · (4,1,5) → (5,1,1)  : **4칸 낙하 (가능)** — 낙하는 제한이 없다
	//   · 낙하 후 (5,1,1) 에서 멈춘다 — 그 아래(z=0)는 바닥판이라 애초에 못 선다
	constexpr int32 VmTSizeX = 7;
	constexpr int32 VmTSizeY = 3;
	constexpr int32 VmTSizeZ = 8;

	FVoxelGrid VmTMakeTerrace()
	{
		FVoxelGrid Grid;
		Grid.Init(FIntVector(VmTSizeX, VmTSizeY, VmTSizeZ));

		const int32 PillarTop[VmTSizeX] = { 0, 0, 1, 2, 4, 0, 0 };

		for (int32 X = 0; X < VmTSizeX; ++X)
		{
			for (int32 Y = 0; Y < VmTSizeY; ++Y)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor); // 전면 바닥판

				// y = 1 만 통행로. 양옆(y=0,2)은 불멸 벽 — 판정이 x 축 하나로 단순해진다.
				if (Y != 1)
				{
					for (int32 Z = 1; Z < VmTSizeZ; ++Z)
					{
						Grid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
					}
				}
			}

			for (int32 Z = 1; Z <= PillarTop[X]; ++Z)
			{
				Grid.Set(FIntVector(X, 1, Z), EBlockType::Immortal);
			}
		}
		return Grid;
	}

	// 방향 하나로 갈 수 있는 칸을 꺼낸다 (없으면 (-1,-1,-1)).
	FIntVector VmTStep(const FVoxelGrid& Grid, const FIntVector& From, const FIntVector& Dir,
	                   const VoxelMove::FMoveCaps& Caps = VoxelMove::FMoveCaps())
	{
		FIntVector Landing;
		return VoxelMove::FindLandingInColumn(Grid, From, Dir, Landing, Caps)
			? Landing : FIntVector(-1, -1, -1);
	}

	const FIntVector VmTPlusX(1, 0, 0);
	const FIntVector VmTMinusX(-1, 0, 0);

	// 검증기의 플러드 필과 **같은 규칙으로** VoxelMove 만 써서 다시 칠한다.
	// 두 결과가 다르면 규칙이 갈라졌다는 뜻이다 — 이 스위트의 존재 이유.
	void VmTFloodFill(const FVoxelGrid& Grid, const TArray<FIntVector>& Seeds, TArray<bool>& OutVisited)
	{
		const int32 Total = Grid.Size.X * Grid.Size.Y * Grid.Size.Z;
		OutVisited.Reset();
		OutVisited.SetNumZeroed(Total);
		if (Total <= 0)
		{
			return;
		}

		TArray<FIntVector> Queue;
		for (const FIntVector& Seed : Seeds)
		{
			if (VoxelMove::IsStandable(Grid, Seed) && !OutVisited[Grid.Index(Seed)])
			{
				OutVisited[Grid.Index(Seed)] = true;
				Queue.Add(Seed);
			}
		}

		TArray<FIntVector> Neighbors;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			VoxelMove::GatherReachableNeighbors(Grid, Queue[Head], Neighbors);
			for (const FIntVector& Next : Neighbors)
			{
				if (!OutVisited[Grid.Index(Next)])
				{
					OutVisited[Grid.Index(Next)] = true;
					Queue.Add(Next);
				}
			}
		}
	}

	// 두 판정이 그리드 전 셀에서 일치하는가 (IsStandable + 플러드 필).
	// 반환은 불일치 개수 — 0 이어야 한다.
	int32 VmTCountDivergence(const FVoxelGrid& Grid, const TArray<FIntVector>& Seeds)
	{
		int32 Mismatch = 0;

		for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Grid.Size.X; ++X)
				{
					const FIntVector Cell(X, Y, Z);
					if (FMapValidator::IsStandable(Grid, Cell) != VoxelMove::IsStandable(Grid, Cell))
					{
						++Mismatch;
					}
				}
			}
		}

		TArray<bool> ValidatorVisited;
		TArray<bool> MoveVisited;
		FMapValidator::FloodFillStandable(Grid, Seeds, ValidatorVisited);
		VmTFloodFill(Grid, Seeds, MoveVisited);

		for (int32 Index = 0; Index < ValidatorVisited.Num(); ++Index)
		{
			if (ValidatorVisited[Index] != MoveVisited[Index])
			{
				++Mismatch;
			}
		}
		return Mismatch;
	}
}

bool FVoxelMovementTest::RunTest(const FString& Parameters)
{
	const FVoxelGrid Terrace = VmTMakeTerrace();

	// ─── ① IsStandable — 나머지 전부가 이 정의 위에 서 있다 ───
	{
		TestTrue (TEXT("① 바닥 위 공중 칸은 설 수 있다"), VoxelMove::IsStandable(Terrace, FIntVector(1, 1, 1)));
		TestTrue (TEXT("① 기둥 꼭대기 위도 설 수 있다"), VoxelMove::IsStandable(Terrace, FIntVector(4, 1, 5)));
		TestFalse(TEXT("① 발판 없는 공중은 못 선다"),    VoxelMove::IsStandable(Terrace, FIntVector(1, 1, 3)));
		TestFalse(TEXT("① 블록 안은 못 선다"),           VoxelMove::IsStandable(Terrace, FIntVector(4, 1, 2)));
		TestFalse(TEXT("① z=0 은 발판이 없어 못 선다"),  VoxelMove::IsStandable(Terrace, FIntVector(1, 1, 0)));
		TestFalse(TEXT("① 범위 밖은 못 선다"),           VoxelMove::IsStandable(Terrace, FIntVector(-1, 1, 1)));
	}

	// ─── ② 오르기 — 1칸은 되고 2칸은 안 된다 (점프 높이 확정값) ───
	{
		TestEqual(TEXT("② 1칸 위 발판 → 도달 가능 (1,1,1)→(2,1,2)"),
			VmTStep(Terrace, FIntVector(1, 1, 1), VmTPlusX), FIntVector(2, 1, 2));
		TestEqual(TEXT("② 1칸 위 발판 → 도달 가능 (2,1,2)→(3,1,3)"),
			VmTStep(Terrace, FIntVector(2, 1, 2), VmTPlusX), FIntVector(3, 1, 3));

		// 여기가 핵심 — (4,1,5) 는 2칸 위다. 이걸 허용하면 실제로는 못 올라가는 맵이 통과한다.
		TestEqual(TEXT("② **2칸 위 발판 → 도달 불가** (3,1,3)→(4,1,5) 없음"),
			VmTStep(Terrace, FIntVector(3, 1, 3), VmTPlusX), FIntVector(-1, -1, -1));

		TestEqual(TEXT("② 오르기 상한은 상수 하나로만 정의된다"), VoxelMove::MaxClimbCells, 1);
	}

	// ─── ③ 낙하 — 높이 제한 없음, 처음 만난 발판에서 멈춘다 ───
	{
		// 4칸 낙하: 발 높이 5 → 1. 이 한 걸음이 이번 개선의 전부다 (예전 봇은 -1칸만 알았다).
		TestEqual(TEXT("③ **4칸 아래 발판 → 도달 가능** (4,1,5)→(5,1,1)"),
			VmTStep(Terrace, FIntVector(4, 1, 5), VmTPlusX), FIntVector(5, 1, 1));

		// 3칸 낙하 (3,1,3) → (2,1,2) 는 1칸이므로, 절벽 쪽으로 확인한다: (4,1,5)→(3,1,3) 은 2칸.
		TestEqual(TEXT("③ 2칸 아래 발판 → 도달 가능 (4,1,5)→(3,1,3)"),
			VmTStep(Terrace, FIntVector(4, 1, 5), VmTMinusX), FIntVector(3, 1, 3));

		// **처음 만난 발판에서 멈춘다.** 계단 중간에 발판이 있으면 그 아래로는 못 간다 —
		// 이 규칙이 없으면 벽 안쪽 공간까지 도달 가능으로 세어 고립 구역 검사가 무력해진다.
		TestEqual(TEXT("③ 내려오다 처음 만난 발판에서 멈춘다 — 그 아래(z=1)로는 안 간다"),
			VmTStep(Terrace, FIntVector(4, 1, 5), VmTMinusX).Z, 3);

		// 낙하 중간 층을 딛고 더 내려가려면 **한 걸음이 더** 필요하다.
		TestEqual(TEXT("③ 그 아래로는 다음 걸음에서 (3,1,3)→(2,1,2)"),
			VmTStep(Terrace, FIntVector(3, 1, 3), VmTMinusX), FIntVector(2, 1, 2));
	}

	// ─── ④ 막힌 방향 / 이웃 수집 ───
	{
		TArray<FIntVector> Neighbors;
		VoxelMove::GatherReachableNeighbors(Terrace, FIntVector(1, 1, 1), Neighbors);
		TestEqual(TEXT("④ 통행로 한가운데 이웃은 2개 (±X 만, ±Y 는 벽)"), Neighbors.Num(), 2);
		TestEqual(TEXT("④ 이웃 개수 == 탈출 방향 수"),
			VoxelMove::CountEscapeDirections(Terrace, FIntVector(1, 1, 1)), 2);

		// 벽(y=0)은 어느 높이에서도 못 들어간다 — 기둥이 통째로 솔리드.
		TestEqual(TEXT("④ 솔리드 기둥 방향은 도달 불가"),
			VmTStep(Terrace, FIntVector(1, 1, 1), FIntVector(0, -1, 0)), FIntVector(-1, -1, -1));

		// 그리드 밖도 마찬가지.
		TestEqual(TEXT("④ 그리드 밖 방향은 도달 불가"),
			VmTStep(Terrace, FIntVector(0, 1, 1), VmTMinusX), FIntVector(-1, -1, -1));
	}

	// ─── ⑤ 오버행 — 벽을 통과해 그 밑으로 떨어지지 않는다 ───
	//
	// 폭발이 기둥 중간을 뚫으면 "위는 막혔고 아래는 비었다"가 런타임에 생긴다.
	// 그때 눈앞의 벽을 뚫고 아래로 내려가는 경로를 계획하면 봇이 벽에 붙어 비빈다.
	{
		FVoxelGrid Overhang;
		Overhang.Init(FIntVector(3, 1, 6));
		for (int32 X = 0; X < 3; ++X)
		{
			Overhang.Set(FIntVector(X, 0, 0), EBlockType::Floor);
		}
		// x=0 은 높이 2 기둥 → 발 높이 3. x=1 은 z=3 만 남은 오버행(그 아래 z=1,2 는 비었다).
		Overhang.Set(FIntVector(0, 0, 1), EBlockType::Immortal);
		Overhang.Set(FIntVector(0, 0, 2), EBlockType::Immortal);
		Overhang.Set(FIntVector(1, 0, 3), EBlockType::Immortal);

		// (0,0,3) 에서 +X: z=4 는 발판(오버행 위) — 1칸 오르기로 올라간다.
		TestEqual(TEXT("⑤ 오버행 위로는 1칸 오르기로 올라간다"),
			VmTStep(Overhang, FIntVector(0, 0, 3), VmTPlusX), FIntVector(1, 0, 4));

		// 오버행을 치워 보면(z=3 을 비우면) 같은 자리에서 바닥까지 떨어진다 — 대조군.
		FVoxelGrid Opened = Overhang;
		Opened.Set(FIntVector(1, 0, 3), EBlockType::Empty);
		TestEqual(TEXT("⑤ 대조군: 오버행이 없으면 바닥(z=1)까지 떨어진다"),
			VmTStep(Opened, FIntVector(0, 0, 3), VmTPlusX), FIntVector(1, 0, 1));

		// 봇처럼 머리 공간을 요구하면 오버행 **밑**(z=1, 머리 위 z=2 는 비었지만 z=3 이 막혔다)은
		// 애초에 눈앞의 z=3 블록에 막혀 못 간다 — 벽 통과 경로가 나오지 않는다.
		VoxelMove::FMoveCaps Caps;
		Caps.bRequireHeadroom = true;
		FVoxelGrid Sealed = Overhang;
		Sealed.Set(FIntVector(1, 0, 4), EBlockType::Immortal); // 오버행 위도 막아 오르기를 없앤다
		TestEqual(TEXT("⑤ 벽을 뚫고 그 밑으로 떨어지는 경로는 없다"),
			VmTStep(Sealed, FIntVector(0, 0, 3), VmTPlusX, Caps), FIntVector(-1, -1, -1));
	}

	// ─── ⑥ 머리 공간 옵션 — 봇만 요구한다 ───
	{
		// z=2 가 막힌 1칸 높이 틈: 격자 기하로는 설 수 있고, 캡슐(176 > 셀 100)로는 못 들어간다.
		FVoxelGrid Gap;
		Gap.Init(FIntVector(3, 1, 4));
		for (int32 X = 0; X < 3; ++X)
		{
			Gap.Set(FIntVector(X, 0, 0), EBlockType::Floor);
		}
		Gap.Set(FIntVector(1, 0, 2), EBlockType::Immortal); // (1,0,1) 의 머리 위를 막는다

		TestTrue (TEXT("⑥ 검증기 기본값: 1칸 틈도 설 수 있는 칸"),
			VoxelMove::IsStandable(Gap, FIntVector(1, 0, 1)));

		VoxelMove::FMoveCaps Caps;
		Caps.bRequireHeadroom = true;
		TestFalse(TEXT("⑥ 머리 공간 요구(봇): 1칸 틈에는 못 들어간다"),
			VoxelMove::IsStandable(Gap, FIntVector(1, 0, 1), Caps));
	}

	// ─── ⑦ **검증기와 완전히 같은 답인가** — 이 스위트의 본론 ───
	//
	// FMapValidator 는 이제 VoxelMove 를 호출만 한다. 여기서 두 경로의 출력을 통째로 대조해
	// "규칙이 한 벌"이라는 사실을 고정한다. 앞으로 어느 한쪽만 고치면 이 검사가 먼저 깨진다.
	{
		TestEqual(TEXT("⑦ 층계 지형: 검증기 == VoxelMove (불일치 0)"),
			VmTCountDivergence(Terrace, TArray<FIntVector>{ FIntVector(1, 1, 1) }), 0);

		// 폴백 맵 — 실제 생산 지형에서도 같은 답이어야 한다.
		UFallbackMapGenerator* Fallback = NewObject<UFallbackMapGenerator>();
		const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();

		FVoxelGrid Grid;
		TArray<FIntVector> Spawns;
		TArray<FItemPlacement> Items;
		if (TestTrue(TEXT("⑦ 폴백 생성 성공"),
			Fallback->Generate(1234u, Rules->MapSize, Rules, Grid, Spawns, Items)))
		{
			TestEqual(TEXT("⑦ 폴백 맵: 검증기 == VoxelMove (불일치 0)"),
				VmTCountDivergence(Grid, Spawns), 0);
		}

		// 절차 맵 (Z 6층 · 산·협곡) — 봇이 실제로 상대할 지형. 여러 시드를 도는 이유는
		// 한 시드의 우연에 기대지 않기 위해서다 (산 높이·꼭대기 스캐터가 시드마다 다르다).
		UProcMapGenerator* Proc = NewObject<UProcMapGenerator>();
		const uint32 ProcSeeds[4] = { 4242u, 20260806u, 777u, 31337u };

		int32 TotalBigDrops = 0;
		int32 GeneratedMaps = 0;
		for (const uint32 Seed : ProcSeeds)
		{
			FVoxelGrid ProcGrid;
			TArray<FIntVector> ProcSpawns;
			TArray<FItemPlacement> ProcItems;
			if (!Proc->Generate(Seed, Rules->MapSizeLarge, Rules, ProcGrid, ProcSpawns, ProcItems))
			{
				continue; // 리롤 상한 초과는 폴백으로 넘기는 정상 경로 — 여기서는 그냥 건너뛴다
			}
			++GeneratedMaps;

			TestEqual(*FString::Printf(TEXT("⑦ 절차 맵 시드 %u: 검증기 == VoxelMove (불일치 0)"), Seed),
				VmTCountDivergence(ProcGrid, ProcSpawns), 0);

			// **2칸 이상 낙하가 실제로 존재하는가** — 개선의 전제다.
			// 없으면 위 대조가 통과해도 "낙하 무제한"이 아무 일도 하지 않았다는 뜻이 된다.
			int32 BigDrops = 0;
			TArray<FIntVector> Neighbors;
			for (int32 Z = 0; Z < ProcGrid.Size.Z; ++Z)
			{
				for (int32 Y = 0; Y < ProcGrid.Size.Y; ++Y)
				{
					for (int32 X = 0; X < ProcGrid.Size.X; ++X)
					{
						const FIntVector Cell(X, Y, Z);
						if (!VoxelMove::IsStandable(ProcGrid, Cell))
						{
							continue;
						}
						VoxelMove::GatherReachableNeighbors(ProcGrid, Cell, Neighbors);
						for (const FIntVector& Next : Neighbors)
						{
							if (Cell.Z - Next.Z >= 2)
							{
								++BigDrops;
							}
						}
					}
				}
			}
			UE_LOG(LogCA3D, Log, TEXT("[검증] 절차 맵 시드 %u — 2칸 이상 낙하 가능한 걸음 %d개"), Seed, BigDrops);
			TotalBigDrops += BigDrops;
		}

		TestTrue(TEXT("⑦ 절차 맵이 하나 이상 생성됐다"), GeneratedMaps > 0);
		TestTrue(TEXT("⑦ 절차 맵에 2칸 이상 낙하가 존재한다 (개선이 실제로 쓰인다)"), TotalBigDrops > 0);
	}

	// ─── ⑧ 결정론 — 같은 입력이면 같은 순서로 같은 이웃 (불변식 4) ───
	{
		TArray<FIntVector> First;
		VoxelMove::GatherReachableNeighbors(Terrace, FIntVector(3, 1, 3), First);

		bool bIdentical = true;
		for (int32 Run = 0; Run < 5; ++Run)
		{
			TArray<FIntVector> Again;
			VoxelMove::GatherReachableNeighbors(Terrace, FIntVector(3, 1, 3), Again);
			if (Again != First) { bIdentical = false; break; }
		}
		TestTrue(TEXT("⑧ 5회 반복 시 이웃 목록(순서 포함) 동일"), bIdentical);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
