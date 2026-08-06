#include "MapGen/ProcMapGenerator.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DRuleSet.h"
#include "MapGen/MapGenUtil.h"
#include "MapGen/MapValidator.h"
#include "Voxel/VoxelGrid.h"
#include "Math/RandomStream.h" // 결정론 난수의 유일한 출처 (불변식 4)

namespace
{
	// ⚠️ 헬퍼 이름에 Pmg 접두사 — 번역 단위 병합(unity build) 시 다른 .cpp 의 익명 네임스페이스
	// 헬퍼와 이름이 겹치면 C2084 가 난다 (MapValidator.cpp 의 Mv 접두사와 같은 규칙).

	// 스폰 배치 롤 상한 (Task 22 명세의 예시 200). 룰셋이 아니라 상수인 이유: 튜닝 값이 아니라
	// **상한 안전장치**다 — 이 롤 예산 안에서 8개 세트 구성을 여러 번 재시작할 수 있고
	// (아래 PmgPlaceSpawns 주석), 예산 소진 시에만 attempt 실패로 넘긴다.
	constexpr int32 PmgSpawnRollLimit = 200;

	// 구조물 하나의 최상층 기록 — 꼭대기 파괴 블록 스캐터(4단계)가 소비한다.
	struct PmgStructureTop
	{
		int32 X0 = 0, Y0 = 0, X1 = -1, Y1 = -1; // 최상층 사각형 (양끝 포함)
		int32 TopZ = 0;                          // 최상층 블록의 z — 스캐터는 그 위 TopZ+1
	};

	// ── 1·2단계. 바닥 + 외곽 벽 (난수 미사용) ────────────────────────────────
	void PmgBuildBaseAndWall(FVoxelGrid& Grid, int32 WallTopZ)
	{
		const FIntVector Size = Grid.Size;

		// z=0 전체 Floor — 폴백과 같은 골격 (바닥 파괴는 서든데스만, GDD 2.3).
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
			}
		}

		// 외곽 1칸 둘레 Immortal 벽, z=1..WallTopZ (= 구조물 최대 높이 + 1).
		// 높이 근거: 구조물 꼭대기 발판(발 높이 MaxH+1)에서 1칸 점프로는 발 높이 MaxH+2 까지만
		// 오르는데, 구조물은 스폰 링(인덱스 1/Size-2) 안쪽(인덱스 ≥2)에만 놓여 벽과 비인접이다.
		// 벽과 인접한 곳은 스폰 링(발 높이 1)뿐이고 거기서 벽 위(발 높이 MaxH+2 ≥ 3)는 절대
		// 못 오른다 — 맵 밖(벽 위) 탈출 경로가 구조적으로 없다.
		for (int32 Z = 1; Z <= WallTopZ; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					if (X == 0 || X == Size.X - 1 || Y == 0 || Y == Size.Y - 1)
					{
						Grid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
					}
				}
			}
		}
	}

	// ── 3단계. 구조물(산) — 웨딩케이크식 계단 고원 ───────────────────────────
	void PmgBuildStructures(FVoxelGrid& Grid, int32 StructureCount, int32 MaxHeight,
	                        FRandomStream& Stream, TArray<PmgStructureTop>& OutTops)
	{
		const FIntVector Size = Grid.Size;
		OutTops.Reset();

		// 배치 영역은 내부 [2, Size-3] — 스폰 링(인덱스 1/Size-2)과 외곽 벽은 침범 금지.
		const int32 InnerMinX = 2;
		const int32 InnerMaxX = Size.X - 3;
		const int32 InnerMinY = 2;
		const int32 InnerMaxY = Size.Y - 3;

		for (int32 Index = 0; Index < StructureCount; ++Index)
		{
			// 밑면 사각형 가로·세로 3~6칸 (내부 폭보다 크면 잘라낸다 — 최소 맵에서도 성립).
			const int32 SpanX = FMath::Min(Stream.RandRange(3, 6), InnerMaxX - InnerMinX + 1);
			const int32 SpanY = FMath::Min(Stream.RandRange(3, 6), InnerMaxY - InnerMinY + 1);
			const int32 X0 = Stream.RandRange(InnerMinX, InnerMaxX - (SpanX - 1));
			const int32 Y0 = Stream.RandRange(InnerMinY, InnerMaxY - (SpanY - 1));
			const int32 X1 = X0 + SpanX - 1;
			const int32 Y1 = Y0 + SpanY - 1;
			const int32 Height = Stream.RandRange(1, MaxHeight);

			// 웨딩케이크: 층 L(1..H)은 밑면을 (L-1)만큼 인셋한 사각형을 z=L 에 채운다.
			// 각 층이 1칸씩 물러나므로 이웃 기둥 높이 차가 항상 1 이하 — 어느 층이든 1칸 점프로
			// 오를 수 있다. GDD 2.1 요건이고 검증기 ①(연결성)·④(고립 구역)가 이 규칙으로 판정한다.
			// 겹침 허용: 계단(높이 차 1 이하) 지형끼리의 합집합 높이는 두 높이의 max 라 역시
			// 높이 차 1 이하가 유지된다 — 자연스러운 능선이 공짜로 나오고 도달성은 깨지지 않는다.
			//
			// **재질은 Destructible** (2026-08-06 사용자: 부서지는 것이 압도적으로 많아야
			// "맵 부수는 느낌"이 난다 — 목표 8:2~9:1). 산이 Immortal 이면 비율이 정반대가 되고
			// (실측 Immortal ~350 vs Destructible ~100), 산을 뚫어 길을 내는 재미도 없다.
			// 부분 파괴로 공중에 뜬 블록이 남는 것은 의도된 복셀 규칙이다 (발판만이 안전하다).
			// Immortal 은 이제 외곽 벽 + 평지 기둥 격자(폭발 차단 골격 — 원작 정체성)뿐이다.
			PmgStructureTop Top;
			for (int32 Layer = 1; Layer <= Height; ++Layer)
			{
				const int32 Inset = Layer - 1;
				const int32 LX0 = X0 + Inset;
				const int32 LX1 = X1 - Inset;
				const int32 LY0 = Y0 + Inset;
				const int32 LY1 = Y1 - Inset;
				if (LX0 > LX1 || LY0 > LY1)
				{
					break; // 인셋으로 사각형이 소진 — 좁은 밑면은 낮은 산에서 멈춘다
				}

				for (int32 Y = LY0; Y <= LY1; ++Y)
				{
					for (int32 X = LX0; X <= LX1; ++X)
					{
						Grid.Set(FIntVector(X, Y, Layer), EBlockType::Destructible);
					}
				}
				Top = PmgStructureTop{ LX0, LY0, LX1, LY1, Layer };
			}
			OutTops.Add(Top); // 실제로 쌓인 최상층 (겹침·인셋 소진 반영)
		}
	}

	// ── 4a단계. 평지 기둥 격자 + 파괴 블록 ───────────────────────────────────
	void PmgFillFlatArea(FVoxelGrid& Grid, int32 DestructiblePercent, FRandomStream& Stream)
	{
		const FIntVector Size = Grid.Size;

		// 폴백 3단계와 같은 패턴 — 내부 [2, Size-3], 스폰 링은 완전히 비워 순환 통로를 확보한다.
		// 순회는 Y→X 고정, 난수는 복도 칸에서만 소비 — 순서가 곧 계약이다 (불변식 4).
		for (int32 Y = 2; Y <= Size.Y - 3; ++Y)
		{
			for (int32 X = 2; X <= Size.X - 3; ++X)
			{
				if (Grid.Get(FIntVector(X, Y, 1)) != EBlockType::Empty)
				{
					continue; // 구조물이 차지한 칸 — 평지가 아니다 (난수도 소비하지 않는다)
				}

				const bool bXEven = (X % 2 == 0);
				const bool bYEven = (Y % 2 == 0);
				if (bXEven && bYEven)
				{
					Grid.Set(FIntVector(X, Y, 1), EBlockType::Immortal); // 원작 크아식 기둥 격자
				}
				else if (bXEven != bYEven && Stream.RandRange(0, 99) < DestructiblePercent)
				{
					// 복도 칸 — 폴백의 (x+y)%4 고정 패턴 대신 스트림 롤 (맵마다 달라진다).
					// 홀수×홀수 교차로는 항상 비워 통로를 보장한다 — 덕분에 어떤 파괴 블록도
					// 빈 이웃을 가져 그 위 발판이 반드시 도달 가능하다 (검증기 ④가 재확인).
					Grid.Set(FIntVector(X, Y, 1), EBlockType::Destructible);
				}
			}
		}
	}

	// ── 4b단계. 구조물 꼭대기 파괴 블록 스캐터 ───────────────────────────────
	void PmgScatterOnTops(FVoxelGrid& Grid, const TArray<PmgStructureTop>& Tops,
	                      int32 DestructiblePercent, FRandomStream& Stream)
	{
		// 구조물 최상층 표면에도 평지와 같은 확률로 파괴 블록 — 아이템이 고지대에도 나와야
		// 산에 오를 이유가 생긴다 (Task 22). 순회는 구조물 생성 순서 → 사각형 Y→X 고정 (불변식 4).
		TArray<FIntVector> Placed;
		for (const PmgStructureTop& Top : Tops)
		{
			Placed.Reset();
			int32 EmptyLeft = 0;

			for (int32 Y = Top.Y0; Y <= Top.Y1; ++Y)
			{
				for (int32 X = Top.X0; X <= Top.X1; ++X)
				{
					const FIntVector Cell(X, Y, Top.TopZ + 1);
					// 겹친 더 높은 구조물이 차지했거나 그리드 밖이면 대상이 아니다 (난수 미소비).
					if (!Grid.IsValid(Cell) || Grid.Get(Cell) != EBlockType::Empty
						|| !Grid.IsSolid(FIntVector(X, Y, Top.TopZ)))
					{
						continue;
					}

					if (Stream.RandRange(0, 99) < DestructiblePercent)
					{
						Grid.Set(Cell, EBlockType::Destructible);
						Placed.Add(Cell);
					}
					else
					{
						++EmptyLeft;
					}
				}
			}

			// 방어: 표면이 통째로 덮이면(특히 1×1·2×2 꼭대기에서 실제로 자주 일어난다) 그 위
			// 발판에 어느 방향에서도 못 올라가 검증기 ④(고립 구역)가 attempt 전체를 리롤시킨다.
			// 첫 배치 칸을 도로 비워 항상 진입로를 남긴다 — 고정 규칙이라 결정론은 유지된다.
			if (EmptyLeft == 0 && Placed.Num() > 0)
			{
				Grid.Set(Placed[0], EBlockType::Empty);
			}
		}
	}

	// ── 5단계. 스폰 8개 — 스폰 링 위, 최소 맨해튼 거리 ───────────────────────
	bool PmgPlaceSpawns(const FIntVector& Size, int32 MinManhattan,
	                    FRandomStream& Stream, TArray<FIntVector>& OutSpawns)
	{
		OutSpawns.Reset();

		// 스폰 링(인덱스 1/Size-2, z=1) 셀 목록 — Y→X 고정 순회로 만들어 순서가 결정론적이다.
		// 링은 생성 전 과정에서 항상 비워 두므로(폴백과 동일) 전 후보가 설 수 있는 자리이고,
		// 링 방향 이웃도 비어 있어 탈출로 2방향이 자동으로 보장된다 (검증기 ③).
		TArray<FIntVector> Ring;
		for (int32 Y = 1; Y <= Size.Y - 2; ++Y)
		{
			for (int32 X = 1; X <= Size.X - 2; ++X)
			{
				if (X == 1 || X == Size.X - 2 || Y == 1 || Y == Size.Y - 2)
				{
					Ring.Add(FIntVector(X, Y, 1));
				}
			}
		}

		// 매 스폰마다 "이미 뽑힌 스폰 전부와 최소 거리를 지키는" 후보만 걸러 그중에서 롤한다
		// (무효 후보까지 포함해 롤하면 거리 제약이 빡빡할 때 롤만 낭비한다 — 유효 집합에서
		// 뽑으면 자리가 없어질 때만 막힌다).
		//
		// **8개 세트 재시작이 핵심이다**: 8스폰 × 최소 거리(21×21 기준 8)는 링 둘레(72칸)를
		// 거의 꽉 채우는 제약이라, 탐욕 배치는 앞선 롤이 링을 잘게 쪼개면 7개쯤에서 자리가
		// 사라진다 — 실측으로 시도의 약 80%가 막혔다. 막히면 attempt 전체(지형 재생성)를
		// 버리는 대신 스폰 세트만 비우고 같은 스트림으로 재시작한다. 롤 예산(200) 안에서
		// 20회 이상 재시작할 수 있어 attempt 실패가 희귀해진다. 전부 스트림 롤이라 결정론 유지.
		int32 Rolls = 0;
		TArray<FIntVector> Valid;
		while (true)
		{
			OutSpawns.Reset();
			bool bDeadEnd = false;

			while (OutSpawns.Num() < 8)
			{
				Valid.Reset();
				for (const FIntVector& Cell : Ring) // 고정 순서 필터 (불변식 4)
				{
					bool bFarEnough = true;
					for (const FIntVector& Spawn : OutSpawns)
					{
						const FIntVector D = Cell - Spawn;
						if (FMath::Abs(D.X) + FMath::Abs(D.Y) + FMath::Abs(D.Z) < MinManhattan)
						{
							bFarEnough = false;
							break;
						}
					}
					if (bFarEnough)
					{
						Valid.Add(Cell);
					}
				}

				if (Valid.Num() == 0)
				{
					bDeadEnd = true; // 자리가 사라졌다 — 이 세트를 버리고 재시작
					break;
				}
				if (++Rolls > PmgSpawnRollLimit)
				{
					return false; // 롤 예산 소진 — 이 attempt 는 실패, 파생 시드로 지형부터 리롤
				}
				OutSpawns.Add(Valid[Stream.RandRange(0, Valid.Num() - 1)]);
			}

			if (!bDeadEnd)
			{
				return true;
			}
			// 재시작 — 매 세트가 최소 1롤(첫 스폰은 항상 유효 후보 존재)을 소비하므로
			// 롤 예산이 곧 재시작 상한이다 (무한 루프 없음).
		}
	}
}

bool UProcMapGenerator::Generate(uint32 Seed, const FIntVector& Size, const UCA3DRuleSet* Rules,
                                 FVoxelGrid& OutGrid,
                                 TArray<FIntVector>& OutSpawns,
                                 TArray<FItemPlacement>& OutItems)
{
	OutSpawns.Reset();
	OutItems.Reset();
	LastAttemptCount = 0;

	if (Rules == nullptr)
	{
		UE_LOG(LogCA3D, Warning, TEXT("ProcMapGenerator: Rules가 nullptr — 생성 실패"));
		return false;
	}

	// 최소 요건은 폴백과 동일(9×9×3) — 외곽 벽(양쪽 2) + 스폰 링(양쪽 2) + 내부(최소 5).
	if (Size.X < 9 || Size.Y < 9 || Size.Z < 3)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("ProcMapGenerator: Size(%d,%d,%d)가 최소 요건(9,9,3) 미만 — 생성 실패"),
			Size.X, Size.Y, Size.Z);
		return false;
	}

	// 검증 임계값 — 하드코딩 금지, 전부 룰셋에서 (Task 21 FThresholds 주석).
	// 스폰 최소 거리만 크기 비례 스케일: 룰셋 값은 21×21 기준이라 (Size.X+Size.Y)/42 정수
	// 비례로 줄인다. 8스폰을 링에 고르게 놓으면 17×17 의 이론상 최소 맨해튼이 7이라,
	// 기준값 8을 그대로 쓰면 소형 맵이 전부 리롤되어 폴백만 나오게 된다 (17×17 → 6).
	FMapValidator::FThresholds Thresholds;
	Thresholds.SpawnMinManhattan   = FMath::Max(Rules->ValidatorSpawnMinManhattan, 0) * (Size.X + Size.Y) / 42;
	Thresholds.SpawnMinEscapeDirs  = Rules->ValidatorSpawnMinEscapeDirs;
	Thresholds.ItemQuadrantMaxDiff = Rules->ValidatorItemQuadrantMaxDiff;

	const int32 MaxAttempts = FMath::Max(Rules->ProcRerollMaxAttempts, 1);
	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		// 파생 시드는 고정식 — 리롤도 결정론이다 (같은 Seed 면 어느 머신에서든 같은 재시도 순서).
		const uint32 DerivedSeed = Seed * 7919u + static_cast<uint32>(Attempt);
		LastAttemptCount = Attempt + 1;

		if (!GenerateOnce(DerivedSeed, Size, Rules, Thresholds.SpawnMinManhattan,
		                  OutGrid, OutSpawns, OutItems))
		{
			UE_LOG(LogCA3D, Verbose,
				TEXT("ProcMapGenerator: Seed %u 시도 %d — 스폰 배치 실패, 파생 시드로 리롤"),
				Seed, Attempt + 1);
			continue;
		}

		// ⑤ 사분면 편차 기준은 **아이템 수 비례**로 그때그때 계산한다 (생성 결과에만 의존 — 결정론).
		// 룰셋 절대값(4)은 아이템 ~18개(폴백) 기준이었다. 구조물이 Destructible 로 바뀌며
		// 아이템이 60개대로 늘자 절대값 4는 확률적으로 거의 불가능해져 기준 시드가 리롤 상한
		// 16회를 전부 소진했다 (2026-08-06 실측). N/4(사분면 평균 몫 1개분)를 하한으로 허용한다.
		Thresholds.ItemQuadrantMaxDiff =
			FMath::Max(Rules->ValidatorItemQuadrantMaxDiff, OutItems.Num() / 4);

		FString Reason;
		if (FMapValidator::Validate(OutGrid, OutSpawns, OutItems, Thresholds, Reason))
		{
			// 재질 구성 집계 — 외곽 벽(경계 링)과 바닥(z=0)을 뺀 "게임 중 상호작용하는 블록"만.
			// "부서지는 것이 압도적으로 많아야 한다"(목표 8:2 이상)가 수치 요건이라
			// 감이 아니라 로그로 확인한다 (2026-08-06 사용자).
			int32 NumDestructible = 0;
			int32 NumImmortal = 0;
			for (int32 Z = 1; Z < Size.Z; ++Z)
			{
				for (int32 Y = 1; Y <= Size.Y - 2; ++Y)
				{
					for (int32 X = 1; X <= Size.X - 2; ++X)
					{
						const EBlockType Type = OutGrid.Get(FIntVector(X, Y, Z));
						NumDestructible += (Type == EBlockType::Destructible) ? 1 : 0;
						NumImmortal     += (Type == EBlockType::Immortal)     ? 1 : 0;
					}
				}
			}
			const int32 Total = NumDestructible + NumImmortal;
			UE_LOG(LogCA3D, Log,
				TEXT("ProcMapGenerator: Seed %u (%d,%d,%d) — %d번째 시도에 검증 통과 (아이템 %d개, 파괴 %d : 고정 %d = %d%% 파괴)"),
				Seed, Size.X, Size.Y, Size.Z, Attempt + 1, OutItems.Num(),
				NumDestructible, NumImmortal, (Total > 0) ? (NumDestructible * 100 / Total) : 0);
			return true;
		}

		// 실패 사유는 Verbose — 정상 동작(리롤)이지 오류가 아니다. 리롤률 튜닝 시 로그 레벨을 올려 본다.
		UE_LOG(LogCA3D, Verbose,
			TEXT("ProcMapGenerator: Seed %u 시도 %d 검증 불통과 — %s"), Seed, Attempt + 1, *Reason);
	}

	UE_LOG(LogCA3D, Warning,
		TEXT("ProcMapGenerator: Seed %u (%d,%d,%d) — 리롤 상한 %d회 초과, 생성 실패 (호출부가 폴백 처리)"),
		Seed, Size.X, Size.Y, Size.Z, MaxAttempts);
	return false;
}

bool UProcMapGenerator::GenerateOnce(uint32 DerivedSeed, const FIntVector& Size, const UCA3DRuleSet* Rules,
                                     int32 SpawnMinManhattan,
                                     FVoxelGrid& OutGrid, TArray<FIntVector>& OutSpawns,
                                     TArray<FItemPlacement>& OutItems) const
{
	// 난수는 이 스트림 하나에서만 뽑는다 (불변식 4). 단계 순서(벽→구조물→평지→꼭대기→스폰)와
	// 각 단계 안의 루프 순서가 곧 난수 소비 순서다 — 여기를 바꾸면 같은 Seed 의 맵이 통째로 바뀐다.
	FRandomStream Stream(static_cast<int32>(DerivedSeed));

	OutGrid.Init(Size);
	OutSpawns.Reset();
	OutItems.Reset();

	// 구조물 최대 높이 = 룰셋 값을 층수에 맞게 클램프. Size.Z-2 인 이유: 최상층 블록(z=MaxH)
	// 위(z=MaxH+1)에 파괴 블록 스캐터가 얹히므로 거기까지는 그리드 안이어야 한다.
	const int32 MaxHeight = FMath::Clamp(Rules->ProcStructureMaxHeight, 1, FMath::Max(1, Size.Z - 2));
	const int32 WallTopZ = FMath::Min(MaxHeight + 1, Size.Z - 1);
	PmgBuildBaseAndWall(OutGrid, WallTopZ);

	// 구조물 개수 = 면적 비례 정수 스케일 (100칸당 N개 — float 없이 정수 나눗셈).
	const int32 StructureCount = Size.X * Size.Y * FMath::Max(Rules->ProcStructureCountPer100Cells, 0) / 100;
	TArray<PmgStructureTop> Tops;
	PmgBuildStructures(OutGrid, StructureCount, MaxHeight, Stream, Tops);

	const int32 DestructiblePercent = FMath::Clamp(Rules->ProcDestructiblePercent, 0, 100);
	PmgFillFlatArea(OutGrid, DestructiblePercent, Stream);
	PmgScatterOnTops(OutGrid, Tops, DestructiblePercent, Stream);

	if (!PmgPlaceSpawns(Size, SpawnMinManhattan, Stream, OutSpawns))
	{
		return false;
	}

	// 아이템 — 폴백과 같은 본체 (FMapGenUtil, Task 23 규칙). 파생 시드를 그대로 쓰는 이유:
	// 리롤될 때마다 지형과 함께 아이템도 새로 뽑혀야 검증기 ⑤(사분면 균등)의 재시도가 의미 있다.
	FMapGenUtil::PlaceItemsInDestructibles(OutGrid, Rules, DerivedSeed, OutItems);
	return true;
}
