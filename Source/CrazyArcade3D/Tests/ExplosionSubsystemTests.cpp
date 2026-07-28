// UExplosionSubsystem::Propagate 자동화 테스트 (Task 15).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.ExplosionSubsystem"로 실행.
//
// Propagate 는 static 순수 함수(불변식 2)라 월드·액터 없이 손으로 만든 미니 FVoxelGrid 로
// PIE 없이 전부 검증 가능하다. 명세(Task 15) 검증 6항목 + Floor 룰 양쪽 분기를 다룬다.
// RequestDetonate 연쇄 실검증은 ABomb 이 생기는 Task 16 이후 (미검증 유지).

#include "Misc/AutomationTest.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Voxel/VoxelGrid.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExplosionSubsystemTest, "CrazyArcade3D.Gameplay.ExplosionSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FExplosionSubsystemTest::RunTest(const FString& Parameters)
{
	// 9×9×9 미니 그리드, 원점은 정중앙 — Range 3 까지 어느 방향도 그리드 밖으로 안 나간다.
	const FIntVector GridSize(9, 9, 9);
	const FIntVector Origin(4, 4, 4);
	const TArray<FIntVector> NoBombs;

	// ── ① 6방향 전파 — 빈 그리드에서 Range 만큼 물줄기 (원점 포함 1 + 6×Range) ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		const int32 Range = 2;

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, Range, false, NoBombs);

		TestEqual(TEXT("① WaterCells 수 = 1 + 6×Range"), R.WaterCells.Num(), 1 + 6 * Range);
		TestTrue(TEXT("① 원점 포함"), R.WaterCells.Contains(Origin));

		// 6방향 × 거리 1..Range 전 셀 포함 (±Z 포함 여부는 ⑤에서 별도 재확인).
		const FIntVector Dirs[6] =
		{
			FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
			FIntVector(0, 1, 0), FIntVector(0, -1, 0),
			FIntVector(0, 0, 1), FIntVector(0, 0, -1),
		};
		bool bAllPresent = true;
		for (const FIntVector& Dir : Dirs)
		{
			for (int32 Step = 1; Step <= Range; ++Step)
			{
				if (!R.WaterCells.Contains(Origin + Dir * Step))
				{
					bAllPresent = false;
				}
			}
		}
		TestTrue(TEXT("① 6방향 거리 1..Range 전 셀이 WaterCells 에 있음"), bAllPresent);
		TestEqual(TEXT("① 빈 그리드 — BrokenCells 없음"), R.BrokenCells.Num(), 0);
		TestEqual(TEXT("① 폭탄 없음 — ChainedCells 없음"), R.ChainedCells.Num(), 0);
	}

	// ── ② Immortal 차단 — 그 방향 즉시 멈춤, 셀 미포함 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(5, 4, 4), EBlockType::Immortal); // +X 거리 1

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 2, false, NoBombs);

		TestFalse(TEXT("② Immortal 셀은 WaterCells 미포함"), R.WaterCells.Contains(FIntVector(5, 4, 4)));
		TestFalse(TEXT("② Immortal 셀은 BrokenCells 미포함"), R.BrokenCells.Contains(FIntVector(5, 4, 4)));
		TestFalse(TEXT("② Immortal 너머로 전파 안 됨"), R.WaterCells.Contains(FIntVector(6, 4, 4)));
		TestTrue(TEXT("② 다른 방향(-X)은 정상 전파"), R.WaterCells.Contains(FIntVector(2, 4, 4)));
		TestEqual(TEXT("② WaterCells 수 = 1 + 5방향×2 (+X 방향 0칸)"), R.WaterCells.Num(), 1 + 5 * 2);
	}

	// ── ③ Destructible — BrokenCells 에 넣고 그 방향 멈춤 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(5, 4, 4), EBlockType::Destructible); // +X 거리 1

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 3, false, NoBombs);

		TestTrue(TEXT("③ Destructible → BrokenCells 포함"), R.BrokenCells.Contains(FIntVector(5, 4, 4)));
		TestEqual(TEXT("③ BrokenCells 는 그 1칸뿐"), R.BrokenCells.Num(), 1);
		TestFalse(TEXT("③ 부순 칸은 WaterCells 미포함"), R.WaterCells.Contains(FIntVector(5, 4, 4)));
		TestFalse(TEXT("③ 부순 칸 너머로 전파 안 됨"), R.WaterCells.Contains(FIntVector(6, 4, 4)));
		TestTrue(TEXT("③ 반대 방향(-X)은 Range 끝까지"), R.WaterCells.Contains(FIntVector(1, 4, 4)));
	}

	// ── ③-α Floor 분기 — bFloorDestructible=false: 안 부수고 멈춤 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(4, 5, 4), EBlockType::Floor); // +Y 거리 1

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 2, false, NoBombs);

		TestEqual(TEXT("③-α Floor(불허) → BrokenCells 없음"), R.BrokenCells.Num(), 0);
		TestFalse(TEXT("③-α Floor 셀은 WaterCells 미포함"), R.WaterCells.Contains(FIntVector(4, 5, 4)));
		TestFalse(TEXT("③-α Floor 너머로 전파 안 됨"), R.WaterCells.Contains(FIntVector(4, 6, 4)));
	}

	// ── ③-β Floor 분기 — bFloorDestructible=true: 부수되 역시 멈춤 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(4, 5, 4), EBlockType::Floor); // +Y 거리 1

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 2, true, NoBombs);

		TestTrue(TEXT("③-β Floor(허용) → BrokenCells 포함"), R.BrokenCells.Contains(FIntVector(4, 5, 4)));
		TestFalse(TEXT("③-β 부숴도 그 방향은 멈춤 — 너머 전파 안 됨"), R.WaterCells.Contains(FIntVector(4, 6, 4)));
	}

	// ── ④ 층간(±Z) 전파 — 위로 Destructible 파괴, 아래로 Immortal 차단 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(4, 4, 6), EBlockType::Destructible); // +Z 거리 2
		Grid.Set(FIntVector(4, 4, 3), EBlockType::Immortal);     // -Z 거리 1

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 3, false, NoBombs);

		TestTrue(TEXT("④ +Z 거리 1 은 물줄기"), R.WaterCells.Contains(FIntVector(4, 4, 5)));
		TestTrue(TEXT("④ +Z 거리 2 Destructible 파괴"), R.BrokenCells.Contains(FIntVector(4, 4, 6)));
		TestFalse(TEXT("④ +Z 부순 칸 너머 전파 안 됨"), R.WaterCells.Contains(FIntVector(4, 4, 7)));
		TestFalse(TEXT("④ -Z Immortal 차단"), R.WaterCells.Contains(FIntVector(4, 4, 3)));
		TestFalse(TEXT("④ -Z Immortal 너머 전파 안 됨"), R.WaterCells.Contains(FIntVector(4, 4, 2)));
	}

	// ── ⑤ 연쇄 셀 검출 — BombCells 위 통과 시 Chained 표시 + 전파는 계속 ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);

		// 원점(터지는 폭탄 자신)과 +Y 거리 2 에 폭탄. 원점은 연쇄 대상이 아니다 (명세 의사코드).
		TArray<FIntVector> BombCells;
		BombCells.Add(Origin);
		BombCells.Add(FIntVector(4, 6, 4));

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 3, false, BombCells);

		TestTrue(TEXT("⑤ 물줄기에 닿은 폭탄 셀 → ChainedCells"), R.ChainedCells.Contains(FIntVector(4, 6, 4)));
		TestEqual(TEXT("⑤ 원점 폭탄(자기 자신)은 연쇄 아님 — Chained 는 1칸뿐"), R.ChainedCells.Num(), 1);
		TestTrue(TEXT("⑤ 폭탄 셀 자체도 물줄기 포함"), R.WaterCells.Contains(FIntVector(4, 6, 4)));
		TestTrue(TEXT("⑤ 폭탄 셀 너머로 전파 계속"), R.WaterCells.Contains(FIntVector(4, 7, 4)));
	}

	// ── ⑥ 순수성 — 같은 입력 2회 호출 = 완전히 같은 출력 (배열 순서까지) ──
	{
		// 블록 3종 + 폭탄이 섞인 그리드로 결정론을 본다.
		FVoxelGrid Grid;
		Grid.Init(GridSize);
		Grid.Set(FIntVector(5, 4, 4), EBlockType::Destructible);
		Grid.Set(FIntVector(4, 3, 4), EBlockType::Immortal);
		Grid.Set(FIntVector(4, 4, 3), EBlockType::Floor);

		TArray<FIntVector> BombCells;
		BombCells.Add(FIntVector(4, 6, 4));

		const FExplosionResult A = UExplosionSubsystem::Propagate(Grid, Origin, 3, true, BombCells);
		const FExplosionResult B = UExplosionSubsystem::Propagate(Grid, Origin, 3, true, BombCells);

		TestTrue(TEXT("⑥ WaterCells 완전 일치 (순서 포함)"), A.WaterCells == B.WaterCells);
		TestTrue(TEXT("⑥ BrokenCells 완전 일치 (순서 포함)"), A.BrokenCells == B.BrokenCells);
		TestTrue(TEXT("⑥ ChainedCells 완전 일치 (순서 포함)"), A.ChainedCells == B.ChainedCells);

		// 두 번 호출해도 입력 그리드가 변하지 않았는가 (부작용 0의 관찰 가능한 근거).
		TestEqual(TEXT("⑥ 입력 그리드 불변: Destructible 그대로"),
			Grid.Get(FIntVector(5, 4, 4)), EBlockType::Destructible);
		TestEqual(TEXT("⑥ 입력 그리드 불변: Floor 그대로"),
			Grid.Get(FIntVector(4, 4, 3)), EBlockType::Floor);
	}

	// ── α Range 0 — 원점만 젖고 아무것도 안 부순다 (경계 안전성) ──
	{
		FVoxelGrid Grid;
		Grid.Init(GridSize);

		const FExplosionResult R = UExplosionSubsystem::Propagate(Grid, Origin, 0, false, NoBombs);

		TestEqual(TEXT("α Range 0 → WaterCells 는 원점 1칸"), R.WaterCells.Num(), 1);
		TestTrue(TEXT("α Range 0 → 원점 포함"), R.WaterCells.Contains(Origin));
		TestEqual(TEXT("α Range 0 → BrokenCells 없음"), R.BrokenCells.Num(), 0);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
