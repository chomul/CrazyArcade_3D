// FMapValidator 자동화 테스트 (Task 21).
// -ExecCmds="Automation RunTests CrazyArcade3D.MapGen.MapValidator" 로 실행.
//
// 순수 함수라 월드도 액터도 필요 없다 — 손으로 만든 FVoxelGrid 하나면 끝난다.
// 검사 5개 각각에 **통과 맵 1 + 실패 맵 1** 을 붙인다 (Task 21 검증 원칙).
// 마지막에 FallbackMapGenerator 출력이 Validate 전체를 통과하는지 확인한다 —
// 폴백 맵이 불통과면 절차 생성기의 리롤이 영원히 돌게 되므로 여기가 방어선이다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "MapGen/MapValidator.h"
#include "MapGen/FallbackMapGenerator.h"
#include "Voxel/VoxelGrid.h"
#include "Framework/CA3DRuleSet.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapValidatorTest, "CrazyArcade3D.MapGen.MapValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 헬퍼 이름에 Mvt 접두사 — 번역 단위 병합 시 다른 테스트 .cpp 와 충돌 방지.

	// 바닥(z=0) 전체 Floor + 외곽 링 2층 벽. 폴백 맵과 같은 골격의 최소 맵.
	FVoxelGrid MvtMakeArena(int32 SizeXY, int32 SizeZ)
	{
		FVoxelGrid Grid;
		Grid.Init(FIntVector(SizeXY, SizeXY, SizeZ));

		for (int32 Y = 0; Y < SizeXY; ++Y)
		{
			for (int32 X = 0; X < SizeXY; ++X)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);

				const bool bBorder = (X == 0 || X == SizeXY - 1 || Y == 0 || Y == SizeXY - 1);
				if (bBorder)
				{
					for (int32 Z = 1; Z <= FMath::Min(2, SizeZ - 1); ++Z)
					{
						Grid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
					}
				}
			}
		}
		return Grid;
	}

	// 기둥 하나를 세워 그 위를 발판으로 만든다 (1칸 점프로 오를 수 있는 2층).
	void MvtRaise(FVoxelGrid& Grid, int32 X, int32 Y, int32 TopZ)
	{
		for (int32 Z = 1; Z <= TopZ; ++Z)
		{
			Grid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
		}
	}

	FMapValidator::FThresholds MvtDefaults()
	{
		return FMapValidator::FThresholds();
	}
}

bool FMapValidatorTest::RunTest(const FString& Parameters)
{
	// ─── ⓪ IsStandable — 나머지 전부가 이 정의 위에 서 있다 ───
	{
		FVoxelGrid Grid = MvtMakeArena(9, 4);
		TestTrue (TEXT("⓪ 바닥 위 공중 칸은 설 수 있다"),  FMapValidator::IsStandable(Grid, FIntVector(4, 4, 1)));
		TestFalse(TEXT("⓪ 발판 없는 공중은 설 수 없다"),   FMapValidator::IsStandable(Grid, FIntVector(4, 4, 2)));
		TestFalse(TEXT("⓪ 블록 안은 설 수 없다"),          FMapValidator::IsStandable(Grid, FIntVector(0, 0, 1)));
		TestFalse(TEXT("⓪ z=0 은 발판이 없어 못 선다"),    FMapValidator::IsStandable(Grid, FIntVector(4, 4, 0)));
		TestFalse(TEXT("⓪ 범위 밖은 설 수 없다"),          FMapValidator::IsStandable(Grid, FIntVector(-1, 4, 1)));
	}

	// ─── ① 연결성: 점프로만 닿는 2층 케이스 + 끊긴 케이스 ───
	{
		// 통과: 평지 — 두 스폰이 같은 층에서 걸어서 닿는다.
		FVoxelGrid Flat = MvtMakeArena(9, 4);
		const TArray<FIntVector> FlatSpawns = { FIntVector(2, 2, 1), FIntVector(6, 6, 1) };
		TestTrue(TEXT("① 통과: 평지 두 스폰 연결"), FMapValidator::AreAllSpawnsConnected(Flat, FlatSpawns));

		// 통과: **1칸 점프로만** 닿는 2층 — 기둥 위(z=2)가 목적지.
		FVoxelGrid Step = MvtMakeArena(9, 4);
		MvtRaise(Step, 5, 5, 1); // 높이 1 기둥 → 그 위 z=2 가 발판
		const TArray<FIntVector> StepSpawns = { FIntVector(2, 2, 1), FIntVector(5, 5, 2) };
		TestTrue(TEXT("① 통과: 1칸 점프로 닿는 2층"), FMapValidator::AreAllSpawnsConnected(Step, StepSpawns));

		// 실패: **2칸**은 못 오른다 — 이걸 통과시키면 실제로는 못 가는 맵이 생산된다.
		FVoxelGrid TooHigh = MvtMakeArena(9, 4);
		MvtRaise(TooHigh, 5, 5, 2); // 높이 2 기둥 → 그 위 z=3
		const TArray<FIntVector> HighSpawns = { FIntVector(2, 2, 1), FIntVector(5, 5, 3) };
		TestFalse(TEXT("① 실패: 2칸 높이는 못 오른다"), FMapValidator::AreAllSpawnsConnected(TooHigh, HighSpawns));

		// 실패: 벽으로 완전히 갈린 두 구역.
		FVoxelGrid Split = MvtMakeArena(9, 4);
		for (int32 Y = 0; Y < 9; ++Y)
		{
			MvtRaise(Split, 4, Y, 2); // x=4 를 높이 2 벽으로 가로막는다
		}
		const TArray<FIntVector> SplitSpawns = { FIntVector(2, 2, 1), FIntVector(6, 6, 1) };
		TestFalse(TEXT("① 실패: 벽으로 갈린 두 구역"), FMapValidator::AreAllSpawnsConnected(Split, SplitSpawns));
	}

	// ─── ② 스폰 최소 거리 ───
	{
		const TArray<FIntVector> Far  = { FIntVector(2, 2, 1), FIntVector(10, 10, 1) };
		const TArray<FIntVector> Near = { FIntVector(2, 2, 1), FIntVector(3,  3,  1) };
		TestTrue (TEXT("② 통과: 맨해튼 16 ≥ 8"), FMapValidator::HaveSpawnsMinDistance(Far,  8));
		TestFalse(TEXT("② 실패: 맨해튼 2 < 8"),  FMapValidator::HaveSpawnsMinDistance(Near, 8));
		TestTrue (TEXT("② 통과: 스폰 1개는 항상 통과"),
			FMapValidator::HaveSpawnsMinDistance(TArray<FIntVector>{ FIntVector(2, 2, 1) }, 8));
	}

	// ─── ③ 탈출로 ───
	{
		FVoxelGrid Open = MvtMakeArena(9, 4);
		const TArray<FIntVector> Center = { FIntVector(4, 4, 1) };
		TestTrue(TEXT("③ 통과: 사방이 트인 스폰 (4방향)"),
			FMapValidator::HaveSpawnsEscapeRoutes(Open, Center, 2));

		// 실패: 3방향을 높이 2 벽으로 막아 탈출로 1방향만 남긴다.
		FVoxelGrid Boxed = MvtMakeArena(9, 4);
		MvtRaise(Boxed, 3, 4, 2);
		MvtRaise(Boxed, 5, 4, 2);
		MvtRaise(Boxed, 4, 3, 2);
		TestFalse(TEXT("③ 실패: 탈출로 1방향뿐"),
			FMapValidator::HaveSpawnsEscapeRoutes(Boxed, Center, 2));

		// 실패: 설 수도 없는 자리에 스폰.
		TestFalse(TEXT("③ 실패: 공중에 뜬 스폰"),
			FMapValidator::HaveSpawnsEscapeRoutes(Open, TArray<FIntVector>{ FIntVector(4, 4, 3) }, 2));
	}

	// ─── ④ 고립 구역: 밀봉된 방 ───
	{
		FVoxelGrid Open = MvtMakeArena(9, 4);
		const TArray<FIntVector> Spawns = { FIntVector(2, 2, 1) };
		TestTrue(TEXT("④ 통과: 트인 맵 (외곽 벽 꼭대기는 설계상 제외)"),
			FMapValidator::HasNoIsolatedRegion(Open, Spawns));

		// 실패: (6,6,1) 을 높이 2 벽으로 사방 밀봉 — 안쪽 칸은 서 있을 수 있는데 아무도 못 간다.
		FVoxelGrid Sealed = MvtMakeArena(9, 4);
		MvtRaise(Sealed, 5, 6, 2);
		MvtRaise(Sealed, 7, 6, 2);
		MvtRaise(Sealed, 6, 5, 2);
		MvtRaise(Sealed, 6, 7, 2);
		TestFalse(TEXT("④ 실패: 벽으로 밀봉된 방"),
			FMapValidator::HasNoIsolatedRegion(Sealed, Spawns));
	}

	// ─── ⑤ 아이템 균등도 ───
	{
		FVoxelGrid Grid = MvtMakeArena(21, 4);

		TArray<FItemPlacement> Balanced;
		for (int32 i = 0; i < 2; ++i)
		{
			Balanced.Add({ FIntVector(4,  4,  1), EItemType::Balloon });
			Balanced.Add({ FIntVector(16, 4,  1), EItemType::Potion  });
			Balanced.Add({ FIntVector(4,  16, 1), EItemType::Roller  });
			Balanced.Add({ FIntVector(16, 16, 1), EItemType::Kick    });
		}
		TestTrue(TEXT("⑤ 통과: 사분면에 2개씩"), FMapValidator::AreItemsBalanced(Grid, Balanced, 4));

		TArray<FItemPlacement> Skewed;
		for (int32 i = 0; i < 9; ++i)
		{
			Skewed.Add({ FIntVector(4, 4, 1), EItemType::Balloon }); // 한 사분면에 9개
		}
		TestFalse(TEXT("⑤ 실패: 한 사분면에 9개 (편차 9 > 4)"),
			FMapValidator::AreItemsBalanced(Grid, Skewed, 4));

		TestTrue(TEXT("⑤ 통과: 아이템 0개는 쏠릴 것이 없다"),
			FMapValidator::AreItemsBalanced(Grid, TArray<FItemPlacement>(), 4));
	}

	// ─── ⑥ Validate 종합: 실패 사유 문자열이 원인을 지목하는가 ───
	{
		FVoxelGrid Grid = MvtMakeArena(9, 4);
		FString Reason;

		TestFalse(TEXT("⑥ 빈 스폰 목록은 실패"),
			FMapValidator::Validate(Grid, TArray<FIntVector>(), TArray<FItemPlacement>(), MvtDefaults(), Reason));
		TestTrue(TEXT("⑥ 실패 사유가 비어 있지 않다"), !Reason.IsEmpty());

		FVoxelGrid Empty;
		TestFalse(TEXT("⑥ 빈 그리드는 실패"),
			FMapValidator::Validate(Empty, TArray<FIntVector>{ FIntVector(1,1,1) },
				TArray<FItemPlacement>(), MvtDefaults(), Reason));
	}

	// ─── ⑦ 폴백 맵이 Validate 를 통과하는가 (Task 21 검증 원칙) ───
	//
	// 여기가 방어선이다: 폴백 맵이 불통과면 절차 생성기의 리롤이 영원히 돌고,
	// 그 증상은 "가끔 맵 로딩이 멈춘다"로 나타나 원인 찾기가 매우 어렵다.
	{
		UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();
		const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();

		FVoxelGrid Grid;
		TArray<FIntVector> Spawns;
		TArray<FItemPlacement> Items;

		if (TestTrue(TEXT("⑦ 폴백 생성 성공"), Generator->Generate(1234u, Rules->MapSize, Rules, Grid, Spawns, Items)))
		{
			// 실측값을 로그로 남긴다 — 임계값을 추측이 아니라 데이터로 정하기 위해.
			int32 MinDist = MAX_int32;
			for (int32 i = 0; i < Spawns.Num(); ++i)
			{
				for (int32 j = i + 1; j < Spawns.Num(); ++j)
				{
					const FIntVector D = Spawns[i] - Spawns[j];
					MinDist = FMath::Min(MinDist, FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z));
				}
			}
			UE_LOG(LogCA3D, Log,
				TEXT("[검증] 폴백 맵 — 스폰 %d개 / 스폰 간 최소 맨해튼 %d / 아이템 %d개"),
				Spawns.Num(), MinDist, Items.Num());

			// 검사별로 따로 확인해야 어디가 걸리는지 알 수 있다.
			TestTrue(TEXT("⑦ 폴백: ① 스폰 연결성"), FMapValidator::AreAllSpawnsConnected(Grid, Spawns));
			TestTrue(TEXT("⑦ 폴백: ③ 스폰 탈출로 2방향"), FMapValidator::HaveSpawnsEscapeRoutes(Grid, Spawns, 2));
			TestTrue(TEXT("⑦ 폴백: ④ 고립 구역 없음"), FMapValidator::HasNoIsolatedRegion(Grid, Spawns));

			FString Reason;
			const bool bValid = FMapValidator::Validate(Grid, Spawns, Items, MvtDefaults(), Reason);
			if (!bValid)
			{
				UE_LOG(LogCA3D, Error, TEXT("[검증] 폴백 맵이 Validate 불통과 — 사유: %s"), *Reason);
			}
			TestTrue(TEXT("⑦ 폴백: Validate 종합 통과"), bValid);
		}
	}

	// ─── ⑧ 결정론: 같은 입력 ⇒ 같은 판정 (불변식 4) ───
	//
	// 판정이 흔들리면 같은 시드가 어떤 실행에서는 통과하고 어떤 실행에서는 리롤된다.
	{
		FVoxelGrid Grid = MvtMakeArena(15, 4);
		MvtRaise(Grid, 7, 7, 1);
		const TArray<FIntVector> Spawns = { FIntVector(2, 2, 1), FIntVector(12, 12, 1) };

		TArray<bool> First;
		FMapValidator::FloodFillStandable(Grid, Spawns, First);

		bool bIdentical = true;
		for (int32 Run = 0; Run < 5; ++Run)
		{
			TArray<bool> Again;
			FMapValidator::FloodFillStandable(Grid, Spawns, Again);
			if (Again != First) { bIdentical = false; break; }
		}
		TestTrue(TEXT("⑧ 결정론: 5회 반복 시 방문 집합 동일"), bIdentical);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
