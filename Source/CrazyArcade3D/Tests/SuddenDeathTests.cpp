// USuddenDeathSubsystem 자동화 테스트 (Task 24).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.SuddenDeath"로 실행.
//
// 1부는 월드 없이 돈다 — 낙하 지점 선정(PickDropCell)이 그리드·난수 스트림만 받는 순수 함수라
// 300회 시뮬레이션·결정론·기둥 조건을 전부 PIE 없이 검증할 수 있다 (Propagate 와 같은 이점).
// 2부는 손으로 만든 월드에서 스케줄러 배선(시작 → 예고 → 정확히 그 셀 낙하 → 정지)을 본다.
//
// 여기서 검증하지 않는 것(= PIE 몫, 체크리스트 24): 마커가 실제로 보이는가, 예고를 보고
// 피할 수 있는가, Listen+클라에서 마커·파괴가 동일한가, 장시간 진행 시 매치가 종결되는가.
//
// ⚠️ 로컬 헬퍼·상수 이름은 전부 Sd 접두사 — UBT 가 여러 .cpp 를 한 번역 단위로 합치면
// 무명 네임스페이스가 병합돼 다른 테스트 파일의 같은 이름과 충돌한다 (mds/build.md).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/SuddenDeath/SuddenDeathSubsystem.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuddenDeathTest, "CrazyArcade3D.Gameplay.SuddenDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (BombTests·ItemPickupTests 관례).
	// ⚠️ 만료 틱에 여유를 더한다: FTimerManager 는 `InternalTime > ExpireTime` 일 때만 발화하는데,
	// 활성화 틱에서 ExpireTime = (그때의 InternalTime) + 주기 로 잡히므로 정확히 주기만큼만
	// 흘리면 두 값이 부동소수까지 같아져 그 틱에 터지지 않는다 (예고 웨이브가 안 쌓인다).
	void SdAdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds + 2.f * KINDA_SMALL_NUMBER); // 만료
	}

	// 21×21 기준 중심 체비셰프 거리. "외곽인가" 판정을 테스트 쪽에서 독립적으로 다시 계산해
	// 구현과 같은 식을 베끼지 않는다 (같은 버그를 공유하지 않기 위해).
	int32 SdChebyshevFromCenter(const FIntVector& Cell, const FIntVector& Size)
	{
		const int32 DoubleX = FMath::Abs(2 * Cell.X - (Size.X - 1));
		const int32 DoubleY = FMath::Abs(2 * Cell.Y - (Size.Y - 1));
		return FMath::Max(DoubleX, DoubleY); // 2배 좌표계 거리 (0 ~ Size-1)
	}

	// 전 기둥에 바닥이 깔린 그리드 — 모든 XY 가 후보가 되므로 가중치만의 영향을 본다.
	void SdMakeFloorGrid(FVoxelGrid& OutGrid, const FIntVector& Size)
	{
		OutGrid.Init(Size);
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				OutGrid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
			}
		}
	}

	// N회 뽑아 외곽·중앙 셀 수를 센다. 외곽 = 2배 거리가 전체 폭의 절반 이상인 셀.
	struct FSdWeightSample
	{
		int32 OuterHits = 0;
		int32 InnerHits = 0;
		int32 OuterCells = 0;
		int32 InnerCells = 0;
		int32 Failures = 0;

		// 셀 수로 정규화한 "칸 하나가 뽑힐 상대 확률" 비율. 이것이 OuterWeightBias 가 뜻하는 값이다.
		double Ratio() const
		{
			const double OuterRate = OuterCells > 0 ? static_cast<double>(OuterHits) / OuterCells : 0.0;
			const double InnerRate = InnerCells > 0 ? static_cast<double>(InnerHits) / InnerCells : 0.0;
			return InnerRate > 0.0 ? OuterRate / InnerRate : 0.0;
		}
	};

	FSdWeightSample SdSampleWeights(const FVoxelGrid& Grid, int32 Seed, int32 OuterWeightPercent, int32 Samples)
	{
		FSdWeightSample Result;

		const int32 HalfSpan = FMath::Max(Grid.Size.X - 1, Grid.Size.Y - 1) / 2;
		for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
		{
			for (int32 X = 0; X < Grid.Size.X; ++X)
			{
				(SdChebyshevFromCenter(FIntVector(X, Y, 0), Grid.Size) >= HalfSpan
					? Result.OuterCells : Result.InnerCells)++;
			}
		}

		// 고정 시드 — 이 테스트는 통계이면서도 **결정론적**이어야 한다.
		// 매 실행마다 값이 흔들리면 경계값 회귀와 우연한 편차를 구분할 수 없다.
		FRandomStream Stream(Seed);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			FIntVector Cell;
			if (!USuddenDeathSubsystem::PickDropCell(Grid, Stream, OuterWeightPercent, 32, Cell))
			{
				++Result.Failures;
				continue;
			}
			(SdChebyshevFromCenter(Cell, Grid.Size) >= HalfSpan ? Result.OuterHits : Result.InnerHits)++;
		}
		return Result;
	}
}

bool FSuddenDeathTest::RunTest(const FString& Parameters)
{
	// ══════════════════════════════════════════════════════════════════════════
	// 1부. 낙하 지점 선정 (순수 함수 — 월드 불필요)
	// ══════════════════════════════════════════════════════════════════════════

	const FIntVector MapSize(21, 21, 4); // 룰셋 기본 MapSize
	const int32 Bias2x = USuddenDeathSubsystem::ToOuterWeightPercent(2.f);

	TestEqual(TEXT("① 룰셋 변환: OuterWeightBias 1.0 ⇒ 100%"),
		USuddenDeathSubsystem::ToOuterWeightPercent(1.f), 100);
	TestEqual(TEXT("① 룰셋 변환: OuterWeightBias 2.0 ⇒ 200%"), Bias2x, 200);
	TestTrue(TEXT("① 룰셋 변환: 0 이하는 1 이상으로 눌린다 (외곽이 절대 안 뽑히는 사고 방지)"),
		USuddenDeathSubsystem::ToOuterWeightPercent(0.f) >= 1);

	FVoxelGrid FloorGrid;
	SdMakeFloorGrid(FloorGrid, MapSize);

	// ── ② 외곽 가중 300회 시뮬레이션 (명세·체크리스트 요구 수치) ──
	{
		const FSdWeightSample S300 = SdSampleWeights(FloorGrid, /*Seed*/ 24, Bias2x, 300);

		UE_LOG(LogCA3D, Display,
			TEXT("서든데스 낙하 300회 (OuterWeightBias 2.0, 21×21) — 외곽 %d회/%d칸, 중앙 %d회/%d칸, 칸당 비율 외곽:중앙 = %.3f : 1 (선정 실패 %d)"),
			S300.OuterHits, S300.OuterCells, S300.InnerHits, S300.InnerCells, S300.Ratio(), S300.Failures);

		TestEqual(TEXT("② 300회 전부 선정 성공 (모든 기둥에 바닥이 있다)"), S300.Failures, 0);
		TestTrue(TEXT("② 외곽 칸이 중앙 칸보다 확실히 잘 뽑힌다 (칸당 비율 > 1.05)"), S300.Ratio() > 1.05);
		TestTrue(TEXT("② 그렇다고 중앙이 배제되지는 않는다 (중앙도 뽑힌다)"), S300.InnerHits > 0);
	}

	// ── ②-α 대량 표본으로 경향 정밀 확인 — 가중치가 실제로 선형 보간인가 ──
	// 21×21 · 중심 100 → 최외곽 200 선형 보간의 이론값은 칸당 비율 약 1.38 이다
	// (외곽 360칸 평균 가중 178.9 / 중앙 81칸 평균 가중 129.6).
	{
		const FSdWeightSample SBig = SdSampleWeights(FloorGrid, /*Seed*/ 24, Bias2x, 5000);
		UE_LOG(LogCA3D, Display,
			TEXT("서든데스 낙하 5000회 (OuterWeightBias 2.0) — 칸당 비율 외곽:중앙 = %.3f : 1 (이론 1.38)"),
			SBig.Ratio());
		TestTrue(TEXT("②-α 5000회 칸당 비율이 이론값 1.38 근처 [1.25, 1.52]"),
			SBig.Ratio() > 1.25 && SBig.Ratio() < 1.52);
	}

	// ── ②-β 룰셋 값이 실제로 소비되는가 — 균등(1.0) < 2배(2.0) < 4배(4.0) ──
	{
		const double Ratio1x = SdSampleWeights(FloorGrid, 24, USuddenDeathSubsystem::ToOuterWeightPercent(1.f), 5000).Ratio();
		const double Ratio2x = SdSampleWeights(FloorGrid, 24, Bias2x, 5000).Ratio();
		const double Ratio4x = SdSampleWeights(FloorGrid, 24, USuddenDeathSubsystem::ToOuterWeightPercent(4.f), 5000).Ratio();

		UE_LOG(LogCA3D, Display, TEXT("서든데스 외곽 가중 비교 (5000회) — 1.0배: %.3f / 2.0배: %.3f / 4.0배: %.3f"),
			Ratio1x, Ratio2x, Ratio4x);

		TestTrue(TEXT("②-β OuterWeightBias 1.0 은 사실상 균등 (칸당 비율 ≈ 1)"),
			Ratio1x > 0.9 && Ratio1x < 1.1);
		TestTrue(TEXT("②-β 가중치를 올릴수록 외곽 편중이 강해진다 (하드코딩이 아니라 룰셋을 읽는다)"),
			Ratio1x < Ratio2x && Ratio2x < Ratio4x);
	}

	// ── ③ 결정론 — 같은 시드면 낙하 순서가 완전히 같다 (선정 함수의 순수성) ──
	{
		TArray<FIntVector> SequenceA;
		TArray<FIntVector> SequenceB;
		TArray<FIntVector> SequenceC;

		FRandomStream StreamA(777);
		FRandomStream StreamB(777);   // 같은 시드
		FRandomStream StreamC(778);   // 다른 시드
		for (int32 Index = 0; Index < 50; ++Index)
		{
			FIntVector Cell;
			if (USuddenDeathSubsystem::PickDropCell(FloorGrid, StreamA, Bias2x, 32, Cell)) { SequenceA.Add(Cell); }
			if (USuddenDeathSubsystem::PickDropCell(FloorGrid, StreamB, Bias2x, 32, Cell)) { SequenceB.Add(Cell); }
			if (USuddenDeathSubsystem::PickDropCell(FloorGrid, StreamC, Bias2x, 32, Cell)) { SequenceC.Add(Cell); }
		}

		TestEqual(TEXT("③ 50회 전부 선정 성공"), SequenceA.Num(), 50);
		TestTrue(TEXT("③ 같은 시드 ⇒ 같은 낙하 순서 (배열 순서까지 완전 일치)"), SequenceA == SequenceB);
		TestTrue(TEXT("③ 다른 시드 ⇒ 다른 순서"), SequenceA != SequenceC);

		// 부작용 0 — 입력 그리드가 변하지 않았는가 (순수성의 관찰 가능한 근거).
		TestEqual(TEXT("③ 입력 그리드 불변: (0,0,0) 은 여전히 Floor"),
			FloorGrid.Get(FIntVector(0, 0, 0)), EBlockType::Floor);
	}

	// ── ④ 원점 = 그 기둥에서 가장 높은 solid 블록의 "위 칸" ──
	{
		FVoxelGrid TowerGrid;
		TowerGrid.Init(FIntVector(3, 3, 5));
		// (1,1) 기둥만 세운다: z=0 Floor, z=1 Destructible, z=2 Immortal → 원점은 z=3 이어야 한다.
		TowerGrid.Set(FIntVector(1, 1, 0), EBlockType::Floor);
		TowerGrid.Set(FIntVector(1, 1, 1), EBlockType::Destructible);
		TowerGrid.Set(FIntVector(1, 1, 2), EBlockType::Immortal);

		FRandomStream Stream(5);
		FIntVector Cell;
		const bool bPicked = USuddenDeathSubsystem::PickDropCell(TowerGrid, Stream, Bias2x, 64, Cell);

		TestTrue(TEXT("④ 후보 기둥이 하나뿐이어도 선정된다"), bPicked);
		TestEqual(TEXT("④ 원점은 가장 높은 solid(z=2) 위 칸인 (1,1,3)"), Cell, FIntVector(1, 1, 3));
	}

	// ── ⑤ 이미 뚫린(전부 Empty) 기둥은 선택되지 않는다 ──
	{
		FVoxelGrid HoleGrid;
		HoleGrid.Init(FIntVector(9, 9, 4));
		// 두 기둥만 살려 둔다 — 나머지는 전부 구멍.
		HoleGrid.Set(FIntVector(0, 0, 0), EBlockType::Floor);
		HoleGrid.Set(FIntVector(8, 8, 0), EBlockType::Floor);

		FRandomStream Stream(31);
		bool bAllOnSolidColumn = true;
		int32 PickedCount = 0;
		for (int32 Index = 0; Index < 200; ++Index)
		{
			FIntVector Cell;
			if (!USuddenDeathSubsystem::PickDropCell(HoleGrid, Stream, Bias2x, 64, Cell))
			{
				continue;
			}
			++PickedCount;
			if (!(Cell == FIntVector(0, 0, 1) || Cell == FIntVector(8, 8, 1)))
			{
				bAllOnSolidColumn = false;
			}
		}

		TestTrue(TEXT("⑤ 살아 있는 기둥이 있으면 대부분 선정에 성공한다"), PickedCount > 150);
		TestTrue(TEXT("⑤ 선정된 셀은 전부 solid 기둥 위 — 뚫린 기둥은 재추첨된다"), bAllOnSolidColumn);

		// 전부 뚫린 맵 — 재추첨 상한을 넘겨 false. 호출자가 이 웨이브를 건너뛴다.
		FVoxelGrid EmptyGrid;
		EmptyGrid.Init(FIntVector(9, 9, 4));
		FRandomStream EmptyStream(31);
		FIntVector Unused;
		TestFalse(TEXT("⑤ 전부 뚫린 맵 → 선정 실패(false) — 무한 루프 없이 빠져나온다"),
			USuddenDeathSubsystem::PickDropCell(EmptyGrid, EmptyStream, Bias2x, 32, Unused));
	}

	// ── ⑥ 경계: 초기화되지 않은 그리드 · 최소 크기 맵 ──
	{
		FRandomStream Stream(1);
		FIntVector Cell;

		FVoxelGrid ZeroGrid; // Init 미호출 — Size 는 기본값이지만 Blocks 가 비어 있다
		ZeroGrid.Init(FIntVector(0, 0, 0));
		TestFalse(TEXT("⑥ 크기 0 그리드 → false (크래시 없이)"),
			USuddenDeathSubsystem::PickDropCell(ZeroGrid, Stream, Bias2x, 8, Cell));

		// 1×1×1 — 가중치 보간의 분모(MaxSpan)가 0 이 되는 경계. 0 나눗셈이 나면 안 된다.
		FVoxelGrid TinyGrid;
		TinyGrid.Init(FIntVector(1, 1, 1));
		TinyGrid.Set(FIntVector(0, 0, 0), EBlockType::Floor);
		TestTrue(TEXT("⑥ 1×1×1 맵도 선정된다 (MaxSpan 0 나눗셈 방어)"),
			USuddenDeathSubsystem::PickDropCell(TinyGrid, Stream, Bias2x, 8, Cell));
		TestEqual(TEXT("⑥ 1×1×1 원점은 (0,0,1)"), Cell, FIntVector(0, 0, 1));

		// 재추첨 상한 0·음수도 최소 1회는 시도한다 (FMath::Max 방어).
		FRandomStream Stream2(2);
		TestTrue(TEXT("⑥ MaxAttempts 0 이어도 최소 1회 시도한다"),
			USuddenDeathSubsystem::PickDropCell(TinyGrid, Stream2, Bias2x, 0, Cell));
	}

	// ── ⑦ 바닥 파괴 분기 — 서든데스만 z=0 을 뚫는다 (기존 폭탄 동작 회귀 방지) ──
	// Propagate 는 순수 함수이고 bFloorDestructible 을 **인자로** 받으므로, 서든데스는
	// 그 자리에 bSuddenDeathDestroysFloor 를 넘기기만 한다 (Propagate 수정 불필요).
	{
		FVoxelGrid Grid;
		Grid.Init(FIntVector(5, 5, 3));
		Grid.Set(FIntVector(2, 2, 0), EBlockType::Floor);

		const FIntVector Origin(2, 2, 1); // 바닥 바로 위 = 낙하 원점 형태
		const TArray<FIntVector> NoBombs;

		const FExplosionResult Bomb = UExplosionSubsystem::Propagate(Grid, Origin, 2, /*bFloorDestructible*/ false, NoBombs);
		TestFalse(TEXT("⑦ 폭탄(bFloorDestructible=false) — 바닥은 부서지지 않는다"),
			Bomb.BrokenCells.Contains(FIntVector(2, 2, 0)));

		const FExplosionResult Drop = UExplosionSubsystem::Propagate(Grid, Origin, 2, /*bSuddenDeathDestroysFloor*/ true, NoBombs);
		TestTrue(TEXT("⑦ 서든데스 낙하(bSuddenDeathDestroysFloor=true) — 바닥이 BrokenCells 에 들어간다"),
			Drop.BrokenCells.Contains(FIntVector(2, 2, 0)));
		TestFalse(TEXT("⑦ 바닥을 부숴도 그 방향 전파는 멈춘다 (그리드 밖으로 새지 않음)"),
			Drop.WaterCells.Contains(FIntVector(2, 2, -1)));
	}

	// ══════════════════════════════════════════════════════════════════════════
	// 2부. 스케줄러 배선 (월드 필요 — 시작 → 예고 → 그 셀 낙하 → 정지)
	// ══════════════════════════════════════════════════════════════════════════

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	// NetMulticast RPC(MulticastWarnDrop·MulticastWaterCells)가 ProcessEvent 에서 버려지지 않게
	// 액터 초기화만 켠다 (BombTests 주석과 동일 — World->BeginPlay() 는 부르지 않는다).
	World->InitializeActorsForPlay(FURL());

	// ── 룰셋 주입: 테스트용 짧은 주기 + 낙하 3발 ──
	// GameState 를 세워 복제 포인터 경로(SuddenDeathResolveRules)를 그대로 태운다 —
	// CDO 폴백만 쓰면 "룰셋을 실제로 읽는가" 가 검증되지 않는다.
	UCA3DRuleSet* TestRules = NewObject<UCA3DRuleSet>();
	TestRules->DropInterval = 0.5f;
	TestRules->DropWarningTime = 1.f;
	TestRules->DropsPerWave = 3;
	TestRules->DropExplosionRange = 2;
	TestRules->bSuddenDeathDestroysFloor = true;

	ACA3DGameState* GameState = World->SpawnActor<ACA3DGameState>();
	if (!TestNotNull(TEXT("ACA3DGameState 스폰"), GameState))
	{
		World->DestroyWorld(false);
		return false;
	}
	GameState->Rules = TestRules;
	World->SetGameState(GameState);

	// ── 손그리드 9×9×4 — z=0 전부 Floor (모든 기둥이 후보) ──
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	VoxelWorld->Grid.Init(FIntVector(9, 9, 4));
	for (int32 Y = 0; Y < 9; ++Y)
	{
		for (int32 X = 0; X < 9; ++X)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	VoxelWorld->bGridInitialized = true;

	USuddenDeathSubsystem* SuddenDeath = World->GetSubsystem<USuddenDeathSubsystem>();
	if (!TestNotNull(TEXT("USuddenDeathSubsystem 존재"), SuddenDeath))
	{
		World->DestroyWorld(false);
		return false;
	}

	// ── ⑧ 시작 — 낙하 타이머가 돈다 ──
	TestFalse(TEXT("⑧ 시작 전에는 구동 중이 아니다"), SuddenDeath->IsRunning());
	SuddenDeath->ServerStart();
	TestTrue(TEXT("⑧ ServerStart 후 구동 중"), SuddenDeath->IsRunning());
	TestTrue(TEXT("⑧ 낙하 주기 타이머 가동"), World->GetTimerManager().IsTimerActive(SuddenDeath->DropTimer));

	SuddenDeath->ServerStart(); // 중복 호출
	TestEqual(TEXT("⑧ 중복 ServerStart 는 무시된다 (예고 웨이브가 늘지 않는다)"),
		SuddenDeath->PendingWaves.Num(), 0);

	// ── ⑨ 예고 — DropInterval 경과 시 웨이브가 예고 상태로 쌓인다 ──
	SdAdvanceTimers(World, TestRules->DropInterval);

	if (!TestEqual(TEXT("⑨ 웨이브 1개가 예고 중"), SuddenDeath->PendingWaves.Num(), 1))
	{
		SuddenDeath->ServerStop();
		World->DestroyWorld(false);
		return false;
	}
	// 개수를 정확히 3 으로 못 박지 않는 이유: 서브시스템의 스트림은 서버 로컬 난수로 시드되므로
	// (클라 재현이 불필요해 의도적으로 그렇게 뒀다) 같은 셀이 두 번 뽑혀 AddUnique 로 합쳐질 수
	// 있다. "룰셋 기본값 1 이 아니라 룰셋에 넣은 3 을 읽는다" 는 사실은 이 범위로 충분히 잡힌다.
	TestTrue(TEXT("⑨ 웨이브 낙하 개수가 룰셋 DropsPerWave(3)를 따른다 (기본값 1 이 아니다)"),
		SuddenDeath->PendingWaves[0].Cells.Num() > 1
		&& SuddenDeath->PendingWaves[0].Cells.Num() <= TestRules->DropsPerWave);

	// 예고 셀을 **지금** 복사해 둔다 — 낙하 후 "정확히 이 셀이 부서졌는가" 를 대조하기 위한 것.
	// 마커≠낙하 셀이면 체크리스트 24 의 필수 요건이 깨진다.
	const TArray<FIntVector> WarnedCells = SuddenDeath->PendingWaves[0].Cells;
	bool bWarnedCellsIntact = true;
	for (const FIntVector& Cell : WarnedCells)
	{
		// 예고 시점에는 아직 아무것도 부서지지 않았어야 한다 (원점 아래가 그대로 solid).
		if (!VoxelWorld->GetGrid().IsSolid(Cell - FIntVector(0, 0, 1)))
		{
			bWarnedCellsIntact = false;
		}
	}
	TestTrue(TEXT("⑨ 예고만으로는 지형이 변하지 않는다 (파괴는 만료 후)"), bWarnedCellsIntact);

	// ── ⑩ 낙하 — 예고했던 **그 셀**이 부서진다 ──
	SdAdvanceTimers(World, TestRules->DropWarningTime);

	bool bAllWarnedCellsDestroyed = true;
	for (const FIntVector& Cell : WarnedCells)
	{
		// 원점 아래 칸(= 예고 대상 기둥의 꼭대기 블록)이 파괴돼 Empty 가 됐어야 한다.
		if (VoxelWorld->GetGrid().Get(Cell - FIntVector(0, 0, 1)) != EBlockType::Empty)
		{
			bAllWarnedCellsDestroyed = false;
		}
	}
	TestTrue(TEXT("⑩ 예고했던 셀이 정확히 파괴된다 (예고 셀 = 낙하 셀)"), bAllWarnedCellsDestroyed);
	TestTrue(TEXT("⑩ 바닥(z=0)이 뚫린다 — bSuddenDeathDestroysFloor 가 적용됐다"),
		VoxelWorld->GetGrid().Get(WarnedCells[0] - FIntVector(0, 0, 1)) == EBlockType::Empty);

	// ── ⑪ 정지 — 예고 중이던 웨이브까지 취소되고 더는 떨어지지 않는다 ──
	SdAdvanceTimers(World, TestRules->DropInterval); // 새 웨이브를 하나 만들어 둔다
	const int32 PendingBeforeStop = SuddenDeath->PendingWaves.Num();
	TestTrue(TEXT("⑪ 정지 직전 예고 중인 웨이브가 존재"), PendingBeforeStop > 0);

	SuddenDeath->ServerStop();
	TestFalse(TEXT("⑪ ServerStop 후 구동 중 아님"), SuddenDeath->IsRunning());
	TestEqual(TEXT("⑪ 예고 중이던 웨이브가 전부 취소됨"), SuddenDeath->PendingWaves.Num(), 0);
	TestFalse(TEXT("⑪ 낙하 주기 타이머 해제"), World->GetTimerManager().IsTimerActive(SuddenDeath->DropTimer));

	// 정지 후 시간이 흘러도 지형이 더 변하지 않는가 — "종료 후에도 블록이 떨어진다" 회귀 방지.
	int32 SolidCountAfterStop = 0;
	for (int32 Y = 0; Y < 9; ++Y)
	{
		for (int32 X = 0; X < 9; ++X)
		{
			if (VoxelWorld->GetGrid().IsSolid(FIntVector(X, Y, 0))) { ++SolidCountAfterStop; }
		}
	}
	SdAdvanceTimers(World, TestRules->DropInterval + TestRules->DropWarningTime);
	int32 SolidCountLater = 0;
	for (int32 Y = 0; Y < 9; ++Y)
	{
		for (int32 X = 0; X < 9; ++X)
		{
			if (VoxelWorld->GetGrid().IsSolid(FIntVector(X, Y, 0))) { ++SolidCountLater; }
		}
	}
	TestEqual(TEXT("⑪ 정지 후에는 시간이 흘러도 지형이 변하지 않는다"), SolidCountLater, SolidCountAfterStop);

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
