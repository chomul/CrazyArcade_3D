#include "MapGen/FallbackMapGenerator.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelGrid.h"
#include "Math/RandomStream.h" // 결정론 난수의 유일한 출처 (불변식 4)

bool UFallbackMapGenerator::Generate(uint32 Seed, const UCA3DRuleSet* Rules,
                                     FVoxelGrid& OutGrid,
                                     TArray<FIntVector>& OutSpawns,
                                     TArray<FItemPlacement>& OutItems)
{
	// **지형 레이아웃**은 Seed 를 의도적으로 무시한다 — 폴백 맵은 항상 같은 하드코딩 배치다.
	// **아이템 배치(6단계)만 Seed 를 쓴다** (Task 23): 같은 맵이라도 매치마다 아이템이 달라야
	// 첫 수가 매번 같아지지 않는다. 결정론 계약("같은 Seed ⇒ 같은 결과")은 그대로 유지된다.

	OutSpawns.Reset();
	OutItems.Reset();

	if (Rules == nullptr)
	{
		UE_LOG(LogCA3D, Warning, TEXT("FallbackMapGenerator: Rules가 nullptr — 생성 실패"));
		return false;
	}

	const FIntVector Size = Rules->MapSize;

	// 방어 방침: 외곽 벽(양쪽 2) + 스폰 링(양쪽 2) + 내부 기둥/계단 영역(최소 5) = 9,
	// 층은 바닥(z=0) + 플레이 층(z=1) + 계단 2단째(z=2) = 3 이 최소 요건이다.
	// 그 미만이면 레이아웃이 성립하지 않으므로 크래시 대신 false를 반환해 호출부가 폴백 처리하게 한다.
	if (Size.X < 9 || Size.Y < 9 || Size.Z < 3)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("FallbackMapGenerator: MapSize(%d,%d,%d)가 최소 요건(9,9,3) 미만 — 생성 실패"),
			Size.X, Size.Y, Size.Z);
		return false;
	}

	OutGrid.Init(Size);

	// ── 1. z=0 전체 Floor ────────────────────────────────────────────────
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			OutGrid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}

	// ── 2. 외곽 1칸 둘레 Immortal 벽 ─────────────────────────────────────
	// 벽 높이 선택: z=1~2 두 층. 점프 높이가 1블록이므로 z=1 한 층이면 벽 위(발 높이 z=2)에
	// 올라설 수 있어 맵 경계 위가 발판이 된다 — 이를 막기 위해 두 층으로 올린다.
	// (z=1 한 층으로 줄여도 판정상 문제는 없으나 경계 위 서기가 허용됨.)
	const int32 WallTopZ = FMath::Min(2, Size.Z - 1); // 정수 Min — 불변식 4 위배 아님
	for (int32 Z = 1; Z <= WallTopZ; ++Z)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				if (X == 0 || X == Size.X - 1 || Y == 0 || Y == Size.Y - 1)
				{
					OutGrid.Set(FIntVector(X, Y, Z), EBlockType::Immortal);
				}
			}
		}
	}

	// ── 3. 내부 기둥 격자 + Destructible 블록 (플레이 층 z=1) ───────────
	// 내부 영역은 [2, Size-3]. x 또는 y == 1 / Size-2 인 "스폰 링"은 완전히 비워
	// 스폰 지점과 외곽 순환 통로를 확보한다.
	//  - 기둥: x,y 둘 다 짝수인 칸 → Immortal (원작 크아식 격자).
	//  - 파괴 블록: x,y 중 정확히 하나만 짝수인 "복도 칸" 중 (x+y)%4==1 인 칸 → Destructible.
	//    복도 칸의 절반만 채워 통로를 남기고, 홀수×홀수 교차로는 항상 비워 둔다.
	for (int32 Y = 2; Y <= Size.Y - 3; ++Y)
	{
		for (int32 X = 2; X <= Size.X - 3; ++X)
		{
			const bool bXEven = (X % 2 == 0);
			const bool bYEven = (Y % 2 == 0);

			if (bXEven && bYEven)
			{
				OutGrid.Set(FIntVector(X, Y, 1), EBlockType::Immortal);
			}
			else if (bXEven != bYEven && (X + Y) % 4 == 1)
			{
				OutGrid.Set(FIntVector(X, Y, 1), EBlockType::Destructible);
			}
		}
	}

	// ── 4. 계단식 접근로 (Task 10 점프 튜닝 검증 지점) ───────────────────
	// 기본 21×21×4 기준 정확한 좌표 (CX=10, CY=10):
	//   접근 칸  (8, 9, z=1)        : Empty     — 발 높이 z=1 로 서는 출발점
	//   1단 블록 (9, 9, z=1)        : Immortal  — 위에 서면 발 높이 z=2 (2층)
	//   2단 스택 (10, 9, z=1)+(10, 9, z=2) : Immortal — 위에 서면 발 높이 z=3 (3층)
	// 1블록 점프만으로 접근 칸 → 1단 → 2단 순서로 오를 수 있다.
	// 계단은 폭발로 사라지면 안 되므로 Immortal. 다른 MapSize에서도 중앙 부근에 같은 상대 배치.
	const int32 CX = Size.X / 2;
	const int32 CY = Size.Y / 2;
	const FIntVector StairApproach(CX - 2, CY - 1, 1);
	const FIntVector StairStep1(CX - 1, CY - 1, 1);
	const FIntVector StairStep2Low(CX, CY - 1, 1);
	const FIntVector StairStep2High(CX, CY - 1, 2);

	OutGrid.Set(StairApproach, EBlockType::Empty); // 3단계 패턴에서 Destructible이 걸렸을 수 있으므로 명시적으로 비운다
	OutGrid.Set(StairStep1, EBlockType::Immortal);
	OutGrid.Set(StairStep2Low, EBlockType::Immortal);
	OutGrid.Set(StairStep2High, EBlockType::Immortal);
	// 착지·머리 공간 방어적 확보 — 1단 위(z=2)와 접근 칸 위(z=2)는 비어 있어야 한다.
	OutGrid.Set(FIntVector(CX - 1, CY - 1, 2), EBlockType::Empty);
	OutGrid.Set(FIntVector(CX - 2, CY - 1, 2), EBlockType::Empty);

	// ── 5. 스폰 8개: 코너 4 + 변 중앙 4 ─────────────────────────────────
	// 전부 스폰 링(x 또는 y == 1 / Size-2) 위. 링은 3단계에서 완전히 비워 두었으므로
	// 각 스폰은 링 방향으로 최소 2방향의 Empty 탈출로를 자동으로 가진다.
	// 기본 21×21×4 기준: (1,1) (19,1) (1,19) (19,19) (10,1) (10,19) (1,10) (19,10), 전부 z=1.
	const int32 MaxX = Size.X - 2;
	const int32 MaxY = Size.Y - 2;
	const int32 MidX = (Size.X - 1) / 2;
	const int32 MidY = (Size.Y - 1) / 2;

	OutSpawns.Add(FIntVector(1,    1,    1)); // 코너 SW
	OutSpawns.Add(FIntVector(MaxX, 1,    1)); // 코너 SE
	OutSpawns.Add(FIntVector(1,    MaxY, 1)); // 코너 NW
	OutSpawns.Add(FIntVector(MaxX, MaxY, 1)); // 코너 NE
	OutSpawns.Add(FIntVector(MidX, 1,    1)); // 남변 중앙
	OutSpawns.Add(FIntVector(MidX, MaxY, 1)); // 북변 중앙
	OutSpawns.Add(FIntVector(1,    MidY, 1)); // 서변 중앙
	OutSpawns.Add(FIntVector(MaxX, MidY, 1)); // 동변 중앙

	// 방어: 스폰 칸은 반드시 Empty여야 한다 (구조상 이미 비어 있지만 명시적으로 보장).
	// 아래 칸(z=0)은 1단계에서 전부 Floor이므로 IsSolid가 항상 성립한다.
	for (const FIntVector& Spawn : OutSpawns)
	{
		OutGrid.Set(Spawn, EBlockType::Empty);
	}

	// ── 6. 아이템 배치 (Task 23) ─────────────────────────────────────────
	// 크아식: 아이템은 바닥에 굴러다니지 않고 **Destructible 블록 "안"에 숨어 있다**.
	// 그 블록을 부순 사람이 먼저 보는 것이 보상 구조의 핵심이라 배치 대상은 Destructible 뿐이다.
	// 노출 스폰은 서버(UExplosionSubsystem)가 파괴 시점에 한다 — 생성기는 목록만 만든다
	// (MapGen 은 Gameplay 액터를 몰라야 한다 — 폴더 의존 규칙).
	//
	// 불변식 4 (결정론): 이 블록 안에는 float 연산·FMath::Rand()·TMap 순회가 하나도 없다.
	//   · 난수는 FRandomStream(Seed) 하나에서만 뽑는다 → 서버·클라가 같은 Seed 로 같은 배치를 만든다
	//   · 확률 판정은 정수 퍼센트 비교 (Stream.RandRange(0,99) < Percent)
	//   · 순회 순서는 Z→Y→X 고정 (그리드 평탄화 순서와 동일). 순서가 바뀌면 같은 Seed 라도
	//     난수 소비 순서가 달라져 배치가 통째로 달라진다 — 여기 루프 순서는 **계약**이다.
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

	if (DropPercent > 0 && WeightTotal > 0)
	{
		FRandomStream Stream(static_cast<int32>(Seed));

		for (int32 Z = 0; Z < Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					const FIntVector Cell(X, Y, Z);
					if (OutGrid.Get(Cell) != EBlockType::Destructible)
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

	UE_LOG(LogCA3D, Log, TEXT("FallbackMapGenerator: Seed %u — 아이템 %d개 배치 (드랍률 %d%%)"),
		Seed, OutItems.Num(), DropPercent);

	return true;
}
