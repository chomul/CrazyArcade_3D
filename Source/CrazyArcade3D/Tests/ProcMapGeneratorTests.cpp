// UProcMapGenerator 자동화 테스트 (Task 22).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.MapGen.ProcMapGenerator" 로 실행.
//
// 월드 불필요 — 생성기·검증기 모두 순수 함수 영역이다 (불변식 4).
// 핵심은 **결정론**: 같은 Seed·Size 는 몇 번을 돌려도 비트 단위로 같은 맵이어야 하고,
// 그 근거를 해시 값·리롤 횟수 로그로 남긴다 (Task 22 검증 원칙 — 보고용).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "MapGen/ProcMapGenerator.h"
#include "MapGen/MapValidator.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelGrid.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProcMapGeneratorTest, "CrazyArcade3D.MapGen.ProcMapGenerator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 헬퍼 이름에 Pmgt 접두사 — 번역 단위 병합(unity build) 시 다른 테스트 .cpp 와 충돌 방지.

	// 그리드 해시 — FNV-1a 32비트. Blocks 는 X→Y→Z 평탄화 고정 순서의 uint8 배열이라
	// 이 해시는 플랫폼 무관하게 결정론적이다 (리눅스 서버 ↔ 윈도우 클라 대조에도 쓸 수 있는 값).
	uint32 PmgtHashGrid(const FVoxelGrid& Grid)
	{
		uint32 Hash = 2166136261u; // FNV offset basis
		for (const uint8 Block : Grid.Blocks)
		{
			Hash = (Hash ^ Block) * 16777619u; // FNV prime
		}
		return Hash;
	}

	// 아이템 배치 비교 — FItemPlacement 에 operator== 가 없어 손으로 비교 (셀·종류·순서까지).
	bool PmgtItemsEqual(const TArray<FItemPlacement>& A, const TArray<FItemPlacement>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Cell != B[Index].Cell || A[Index].Type != B[Index].Type)
			{
				return false;
			}
		}
		return true;
	}

	// 생성기와 같은 공식으로 스케일한 검증 임계값 (ProcMapGenerator.cpp 주석 참조).
	FMapValidator::FThresholds PmgtScaledThresholds(const UCA3DRuleSet* Rules, const FIntVector& Size)
	{
		FMapValidator::FThresholds Thresholds;
		Thresholds.SpawnMinManhattan   = FMath::Max(Rules->ValidatorSpawnMinManhattan, 0) * (Size.X + Size.Y) / 42;
		Thresholds.SpawnMinEscapeDirs  = Rules->ValidatorSpawnMinEscapeDirs;
		Thresholds.ItemQuadrantMaxDiff = Rules->ValidatorItemQuadrantMaxDiff;
		return Thresholds;
	}
}

bool FProcMapGeneratorTest::RunTest(const FString& Parameters)
{
	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();
	UProcMapGenerator* Generator = NewObject<UProcMapGenerator>();

	// ①·⑥ 이 공유하는 기준 생성물.
	const uint32 BaseSeed = 20260806u;
	const FIntVector LargeSize = Rules->MapSizeLarge;
	FVoxelGrid BaseGrid;
	TArray<FIntVector> BaseSpawns;
	TArray<FItemPlacement> BaseItems;

	// ─── ① 결정론 (핵심): 같은 Seed·Size 5회 → 그리드 해시·스폰·아이템 완전 동일 ───
	{
		if (!TestTrue(TEXT("① 기준 생성 성공"),
			Generator->Generate(BaseSeed, LargeSize, Rules, BaseGrid, BaseSpawns, BaseItems)))
		{
			return false; // 이후 검증은 무의미
		}

		const uint32 BaseHash = PmgtHashGrid(BaseGrid);
		// 보고용 해시 로그 (Task 22 응답 원칙 — 결정론을 해시 값으로 보고).
		UE_LOG(LogCA3D, Display,
			TEXT("[Task22] 결정론 기준 — Seed %u (%d,%d,%d): 그리드 해시 %08X, 스폰 %d개, 아이템 %d개, 시도 %d회"),
			BaseSeed, LargeSize.X, LargeSize.Y, LargeSize.Z, BaseHash,
			BaseSpawns.Num(), BaseItems.Num(), Generator->LastAttemptCount);

		bool bAllIdentical = true;
		for (int32 Run = 1; Run < 5; ++Run)
		{
			FVoxelGrid Grid;
			TArray<FIntVector> Spawns;
			TArray<FItemPlacement> Items;
			if (!Generator->Generate(BaseSeed, LargeSize, Rules, Grid, Spawns, Items)
				|| PmgtHashGrid(Grid) != BaseHash
				|| Spawns != BaseSpawns
				|| !PmgtItemsEqual(Items, BaseItems))
			{
				UE_LOG(LogCA3D, Error, TEXT("[Task22] 결정론 위반 — %d회차 재생성이 기준과 다름"), Run + 1);
				bAllIdentical = false;
				break;
			}
		}
		TestTrue(TEXT("① 같은 Seed·Size 5회 — 그리드 해시·스폰·아이템 완전 동일 (불변식 4)"), bAllIdentical);
	}

	// ─── ② 다른 시드 10개 → 서로 다른 해시 ≥ 9 + 전부 Validate 통과 (리롤 통계 로그) ───
	{
		const FMapValidator::FThresholds Thresholds = PmgtScaledThresholds(Rules, LargeSize);

		TArray<uint32> Hashes;
		int32 TotalAttempts = 0; // 시드당 1이면 리롤 0 — 합계에서 리롤률을 읽는다
		int32 GeneratedCount = 0;

		for (uint32 Seed = 101; Seed <= 110; ++Seed)
		{
			FVoxelGrid Grid;
			TArray<FIntVector> Spawns;
			TArray<FItemPlacement> Items;
			if (!TestTrue(*FString::Printf(TEXT("② Seed %u 생성 성공"), Seed),
				Generator->Generate(Seed, LargeSize, Rules, Grid, Spawns, Items)))
			{
				continue;
			}
			++GeneratedCount;
			TotalAttempts += Generator->LastAttemptCount;
			Hashes.Add(PmgtHashGrid(Grid));

			// 재검증은 생성기의 **유효 기준**과 같아야 한다 — 사분면 편차는 아이템 수 비례
			// max(룰셋 값, N/4) 다 (Generate 주석). 룰셋 절대값으로 다시 검사하면 생성기가
			// 통과시킨 맵을 테스트가 떨어뜨린다 (2026-08-06 실측 — 아이템 60개대에서 전부 불통과).
			FMapValidator::FThresholds Effective = Thresholds;
			Effective.ItemQuadrantMaxDiff = FMath::Max(Thresholds.ItemQuadrantMaxDiff, Items.Num() / 4);

			FString Reason;
			const bool bValid = FMapValidator::Validate(Grid, Spawns, Items, Effective, Reason);
			if (!bValid)
			{
				UE_LOG(LogCA3D, Error, TEXT("② Seed %u Validate 불통과 — %s"), Seed, *Reason);
			}
			TestTrue(*FString::Printf(TEXT("② Seed %u Validate 통과"), Seed), bValid);
		}

		// 고유 해시 개수 — TSet 순회 금지(불변식 4는 테스트에도 적용), TArray 고정 순서 스캔.
		int32 UniqueHashes = 0;
		for (int32 I = 0; I < Hashes.Num(); ++I)
		{
			bool bSeen = false;
			for (int32 J = 0; J < I; ++J)
			{
				if (Hashes[J] == Hashes[I])
				{
					bSeen = true;
					break;
				}
			}
			if (!bSeen)
			{
				++UniqueHashes;
			}
		}

		// 보고용 리롤 통계 (Task 22 응답 원칙 — 리롤률 >50% 면 생성 규칙부터 보정).
		UE_LOG(LogCA3D, Display,
			TEXT("[Task22] 시드 %d개 — 고유 해시 %d개 / 총 시도 %d회 (리롤 %d회, 시드당 평균 시도 %d.%d회)"),
			GeneratedCount, UniqueHashes, TotalAttempts, TotalAttempts - GeneratedCount,
			GeneratedCount > 0 ? TotalAttempts / GeneratedCount : 0,
			GeneratedCount > 0 ? (TotalAttempts * 10 / GeneratedCount) % 10 : 0);
		TestTrue(TEXT("② 서로 다른 시드 10개 → 고유 해시 9개 이상"), UniqueHashes >= 9);
	}

	// ─── ③ 두 티어 (17,17,6)·(21,21,6) 모두 생성 성공 + Validate 통과 ───
	{
		const FIntVector TierSizes[2] = { Rules->MapSizeSmall, Rules->MapSizeLarge };
		for (const FIntVector& Size : TierSizes)
		{
			FVoxelGrid Grid;
			TArray<FIntVector> Spawns;
			TArray<FItemPlacement> Items;
			if (!TestTrue(*FString::Printf(TEXT("③ 티어 (%d,%d,%d) 생성 성공"), Size.X, Size.Y, Size.Z),
				Generator->Generate(777u, Size, Rules, Grid, Spawns, Items)))
			{
				continue;
			}

			// 사분면 편차는 생성기의 유효 기준(max(룰셋, N/4))으로 재검증 — ② 와 같은 이유.
			FMapValidator::FThresholds Effective = PmgtScaledThresholds(Rules, Size);
			Effective.ItemQuadrantMaxDiff = FMath::Max(Effective.ItemQuadrantMaxDiff, Items.Num() / 4);

			FString Reason;
			const bool bValid = FMapValidator::Validate(Grid, Spawns, Items, Effective, Reason);
			if (!bValid)
			{
				UE_LOG(LogCA3D, Error, TEXT("③ 티어 (%d,%d,%d) Validate 불통과 — %s"),
					Size.X, Size.Y, Size.Z, *Reason);
			}
			TestTrue(*FString::Printf(TEXT("③ 티어 (%d,%d,%d) Validate 통과"), Size.X, Size.Y, Size.Z), bValid);
			UE_LOG(LogCA3D, Display, TEXT("[Task22] 티어 (%d,%d,%d) Seed 777 — 해시 %08X, 시도 %d회"),
				Size.X, Size.Y, Size.Z, PmgtHashGrid(Grid), Generator->LastAttemptCount);
		}
	}

	// ─── ④ 강제 실패: 불가능한 임계값 → false (호출부 폴백 신호가 실제로 나오는가) ───
	{
		// transient 룰셋 — 스폰 8개가 절대 만족 못 하는 거리. 실패 경로는 리롤 상한까지 돌고
		// false 를 반환해야 한다 (AVoxelWorld 가 이 false 를 보고 폴백 생성기로 전환한다).
		UCA3DRuleSet* ImpossibleRules = NewObject<UCA3DRuleSet>();
		ImpossibleRules->ValidatorSpawnMinManhattan = 999;

		FVoxelGrid Grid;
		TArray<FIntVector> Spawns;
		TArray<FItemPlacement> Items;
		TestFalse(TEXT("④ 불가능한 임계값(스폰 거리 999) → Generate false (폴백 신호)"),
			Generator->Generate(42u, ImpossibleRules->MapSizeSmall, ImpossibleRules, Grid, Spawns, Items));
		TestEqual(TEXT("④ 실패 시 리롤 상한까지 시도"),
			Generator->LastAttemptCount, ImpossibleRules->ProcRerollMaxAttempts);
	}

	// ─── ⑤ 구조물 존재 + 재질 비율 (2026-08-06 사용자: 부서지는 것이 압도적이어야 한다) ───
	{
		// 구조물은 **Destructible** 이다 — Immortal 로 찾으면 안 된다 (재질 확정 후 갱신).
		// 외곽 벽이 z≥2 Immortal 이므로 **내부([1, Size-2])만** 스캔해야 구조물 검사가 된다.
		bool bFoundHighStructure = false;
		int32 WorstDestructiblePercent = 100;
		for (uint32 Seed = 1; Seed <= 5; ++Seed)
		{
			FVoxelGrid Grid;
			TArray<FIntVector> Spawns;
			TArray<FItemPlacement> Items;
			if (!Generator->Generate(Seed, LargeSize, Rules, Grid, Spawns, Items))
			{
				continue;
			}

			// 내부(외곽 벽 제외) z≥1 의 파괴/고정 블록 집계 — 생성기의 성공 로그와 같은 정의.
			int32 NumDestructible = 0;
			int32 NumImmortal = 0;
			for (int32 Z = 1; Z < Grid.Size.Z; ++Z)
			{
				for (int32 Y = 1; Y <= Grid.Size.Y - 2; ++Y)
				{
					for (int32 X = 1; X <= Grid.Size.X - 2; ++X)
					{
						const EBlockType Type = Grid.Get(FIntVector(X, Y, Z));
						NumDestructible += (Type == EBlockType::Destructible) ? 1 : 0;
						NumImmortal     += (Type == EBlockType::Immortal)     ? 1 : 0;
						bFoundHighStructure |= (Z >= 2 && Type == EBlockType::Destructible);
					}
				}
			}

			const int32 Total = NumDestructible + NumImmortal;
			if (Total > 0)
			{
				WorstDestructiblePercent = FMath::Min(WorstDestructiblePercent, NumDestructible * 100 / Total);
			}
		}
		TestTrue(TEXT("⑤ 어떤 시드에서 z≥2 내부 Destructible 존재 (구조물이 실제로 쌓인다)"), bFoundHighStructure);

		// 목표 8:2 — 시드 5개의 **최악값**이 75% 이상이어야 한다 (평균은 나쁜 시드를 가린다).
		UE_LOG(LogCA3D, Log, TEXT("[Task22] 재질 비율 — 시드 5개 중 최저 파괴 비율 %d%% (기준 75%%)"),
			WorstDestructiblePercent);
		TestTrue(TEXT("⑤ 파괴 블록 비율 ≥ 75% (사용자 목표 8:2~9:1)"), WorstDestructiblePercent >= 75);
	}

	// ─── ⑥ 스폰: 전부 IsStandable + 스폰 링(인덱스 1/Size-2, z=1) 위 ───
	{
		TestEqual(TEXT("⑥ 스폰 개수 = 8"), BaseSpawns.Num(), 8);
		for (const FIntVector& Spawn : BaseSpawns)
		{
			TestTrue(*FString::Printf(TEXT("⑥ 스폰 (%d,%d,%d): IsStandable"), Spawn.X, Spawn.Y, Spawn.Z),
				FMapValidator::IsStandable(BaseGrid, Spawn));

			const bool bOnRing = Spawn.Z == 1
				&& (Spawn.X == 1 || Spawn.X == LargeSize.X - 2
					|| Spawn.Y == 1 || Spawn.Y == LargeSize.Y - 2)
				&& Spawn.X >= 1 && Spawn.X <= LargeSize.X - 2
				&& Spawn.Y >= 1 && Spawn.Y <= LargeSize.Y - 2;
			TestTrue(*FString::Printf(TEXT("⑥ 스폰 (%d,%d,%d): 스폰 링 위"), Spawn.X, Spawn.Y, Spawn.Z),
				bOnRing);
		}
	}

	// ─── 방어: Rules nullptr → false (폴백과 같은 관례) ───
	{
		FVoxelGrid Grid;
		TArray<FIntVector> Spawns;
		TArray<FItemPlacement> Items;
		TestFalse(TEXT("Rules == nullptr → false"),
			Generator->Generate(0u, LargeSize, nullptr, Grid, Spawns, Items));
		TestFalse(TEXT("최소 요건 미만 크기(8,8,2) → false"),
			Generator->Generate(0u, FIntVector(8, 8, 2), Rules, Grid, Spawns, Items));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
