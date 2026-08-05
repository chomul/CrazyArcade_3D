// UHISMVoxelRenderer 자동화 테스트 (Task 07).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Voxel.HISMVoxelRenderer"로 실행.
//
// 메시 에셋을 지정하지 않은 상태로 돌린다 — 인스턴스 카운트/인덱스 맵 로직만 검증.
// 표면 판정은 렌더러 구현과 독립으로 다시 구현해 교차 검증한다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/HISMVoxelRenderer.h"
#include "Voxel/VoxelRayCast.h" // 가림 페이드 배선 검증 (2026-08-06)
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#if WITH_AUTOMATION_TESTS

namespace CA3DHISMRendererTest
{
	const FIntVector Dirs[6] = {
		FIntVector( 1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector( 0, 1, 0), FIntVector( 0,-1, 0),
		FIntVector( 0, 0, 1), FIntVector( 0, 0,-1),
	};

	// 렌더러와 독립 구현한 표면 판정 — Empty가 아니고 6방향 중 하나라도 Empty(범위 밖 포함)면 표면.
	static bool IsSurfaceRef(const FVoxelGrid& Grid, const FIntVector& C)
	{
		if (Grid.Get(C) == EBlockType::Empty)
		{
			return false;
		}
		for (const FIntVector& D : Dirs)
		{
			if (Grid.Get(C + D) == EBlockType::Empty)
			{
				return true;
			}
		}
		return false;
	}

	// 타입별 기대 표면 셀 집합 — "처음부터 다시 BuildFromGrid 했을 때"의 기대값.
	static TMap<EBlockType, TSet<FIntVector>> ComputeExpectedSurface(const FVoxelGrid& Grid)
	{
		TMap<EBlockType, TSet<FIntVector>> Out;
		for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Grid.Size.X; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (IsSurfaceRef(Grid, C))
					{
						Out.FindOrAdd(Grid.Get(C)).Add(C);
					}
				}
			}
		}
		return Out;
	}

	static int32 CountSolid(const FVoxelGrid& Grid)
	{
		int32 Count = 0;
		for (const uint8 Block : Grid.Blocks)
		{
			if (Block != static_cast<uint8>(EBlockType::Empty))
			{
				++Count;
			}
		}
		return Count;
	}

	// 서로 다른 Destructible 셀을 스캔 순서(결정적)로 최대 MaxCount개 수집.
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

	// 첫 번째 "표면인" Destructible 셀. 없으면 (-1,-1,-1).
	static FIntVector FindFirstSurfaceDestructible(const FVoxelGrid& Grid)
	{
		for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
		{
			for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
			{
				for (int32 X = 0; X < Grid.Size.X; ++X)
				{
					const FIntVector C(X, Y, Z);
					if (Grid.Get(C) == EBlockType::Destructible && IsSurfaceRef(Grid, C))
					{
						return C;
					}
				}
			}
		}
		return FIntVector(-1, -1, -1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHISMVoxelRendererTest, "CrazyArcade3D.Voxel.HISMVoxelRenderer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHISMVoxelRendererTest::RunTest(const FString& Parameters)
{
	using namespace CA3DHISMRendererTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}

	UHISMVoxelRenderer* Renderer = VoxelWorld->FindComponentByClass<UHISMVoxelRenderer>();
	if (!TestNotNull(TEXT("생성자 CreateDefaultSubobject 렌더러 컴포넌트 존재"), Renderer))
	{
		World->DestroyWorld(false);
		return false;
	}

	// 이 테스트 월드에는 GameMode가 없어 액터 BeginPlay가 돌지 않는다 —
	// BeginPlay의 배선(Renderer = HISMRendererComponent)을 friend 접근으로 수동 수행.
	// ServerInitFromSeed 전에 배선해야 InitGridFromSeed의 BuildFromGrid와
	// ApplyDestruction의 RemoveBlock 경로가 실제로 렌더러를 통과한다.
	VoxelWorld->Renderer = Renderer;

	VoxelWorld->ServerInitFromSeed(1234u);
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();
	TestTrue(TEXT("그리드 초기화됨"), Grid.Blocks.Num() > 0);

	// ─── 검사 람다 (friend 함수 본문 안 — 렌더러 private 접근 가능) ───

	// b. 정/역맵 왕복 무결성 + HISM 인스턴스 카운트와 맵 크기 일치.
	auto CheckIntegrity = [&](const FString& Phase)
	{
		for (const auto& TypePair : Renderer->CellToInstance)
		{
			const int32 TypeNum = static_cast<int32>(TypePair.Key);
			const TMap<FIntVector, int32>& C2I = TypePair.Value;
			const TMap<int32, FIntVector>* I2C = Renderer->InstanceToCell.Find(TypePair.Key);
			UHierarchicalInstancedStaticMeshComponent* HISM = Renderer->HISMs.FindRef(TypePair.Key);

			TestNotNull(*FString::Printf(TEXT("%s: 타입 %d 역맵 존재"), *Phase, TypeNum), I2C);
			TestNotNull(*FString::Printf(TEXT("%s: 타입 %d HISM 존재"), *Phase, TypeNum), HISM);
			if (!I2C || !HISM)
			{
				continue;
			}

			TestEqual(*FString::Printf(TEXT("%s: 타입 %d 정/역맵 크기 일치"), *Phase, TypeNum),
				C2I.Num(), I2C->Num());
			TestEqual(*FString::Printf(TEXT("%s: 타입 %d HISM 인스턴스 수 == 맵 크기"), *Phase, TypeNum),
				HISM->GetInstanceCount(), C2I.Num());

			int32 Mismatches = 0;
			for (const auto& Pair : C2I) // 셀→인덱스→셀 왕복 + 인덱스 범위
			{
				const FIntVector* Back = I2C->Find(Pair.Value);
				if (!Back || *Back != Pair.Key
					|| Pair.Value < 0 || Pair.Value >= HISM->GetInstanceCount())
				{
					++Mismatches;
				}
			}
			for (const auto& Pair : *I2C) // 인덱스→셀→인덱스 왕복
			{
				const int32* Fwd = C2I.Find(Pair.Value);
				if (!Fwd || *Fwd != Pair.Key)
				{
					++Mismatches;
				}
			}
			TestEqual(*FString::Printf(TEXT("%s: 타입 %d 왕복 불일치 0건"), *Phase, TypeNum),
				Mismatches, 0);
		}
	};

	// 렌더러의 타입별 셀 집합 == 현재 그리드로 다시 BuildFromGrid 했을 때의 기대 표면 집합.
	auto CheckMatchesExpected = [&](const FString& Phase)
	{
		const TMap<EBlockType, TSet<FIntVector>> Expected = ComputeExpectedSurface(VoxelWorld->GetGrid());

		int32 Missing = 0; // 기대에는 있는데 렌더러에 없음
		for (const auto& TypePair : Expected)
		{
			const TMap<FIntVector, int32>* C2I = Renderer->CellToInstance.Find(TypePair.Key);
			for (const FIntVector& C : TypePair.Value)
			{
				if (!C2I || !C2I->Contains(C))
				{
					++Missing;
				}
			}
		}
		int32 Extra = 0; // 렌더러에는 있는데 기대에 없음
		for (const auto& TypePair : Renderer->CellToInstance)
		{
			const TSet<FIntVector>* Exp = Expected.Find(TypePair.Key);
			for (const auto& Pair : TypePair.Value)
			{
				if (!Exp || !Exp->Contains(Pair.Key))
				{
					++Extra;
				}
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: 기대 표면인데 렌더러에 없는 셀 0개"), *Phase), Missing, 0);
		TestEqual(*FString::Printf(TEXT("%s: 렌더러에 있는데 기대 표면이 아닌 셀 0개"), *Phase), Extra, 0);
	};

	// ─── a. 초기 빌드: 인스턴스 총수/타입별 개수 == 독립 표면 판정 결과 ───
	{
		const TMap<EBlockType, TSet<FIntVector>> Expected = ComputeExpectedSurface(Grid);
		int32 ExpectedTotal = 0;
		for (const auto& Pair : Expected)
		{
			ExpectedTotal += Pair.Value.Num();
		}
		int32 ActualTotal = 0;
		for (const auto& Pair : Renderer->CellToInstance)
		{
			ActualTotal += Pair.Value.Num();
		}
		TestTrue(TEXT("표면 블록이 존재"), ExpectedTotal > 0);
		TestEqual(TEXT("인스턴스 총수 == 독립 표면 판정 개수"), ActualTotal, ExpectedTotal);

		for (const auto& Pair : Expected)
		{
			const TMap<FIntVector, int32>* C2I = Renderer->CellToInstance.Find(Pair.Key);
			TestEqual(*FString::Printf(TEXT("타입 %d 인스턴스 개수 일치"), static_cast<int32>(Pair.Key)),
				C2I ? C2I->Num() : 0, Pair.Value.Num());
		}

		// c. 표면 비율 로그 (체크리스트 "표면 추출 비율" 확인용).
		const int32 TotalCells = Grid.Size.X * Grid.Size.Y * Grid.Size.Z;
		const int32 Solid = CountSolid(Grid);
		UE_LOG(LogCA3D, Display,
			TEXT("표면 비율: 인스턴스 %d / 솔리드 %d (솔리드 대비 %.1f%%) / 총 셀 %d (총 셀 대비 %.1f%%)"),
			ActualTotal, Solid,
			Solid > 0 ? 100.f * ActualTotal / Solid : 0.f,
			TotalCells,
			TotalCells > 0 ? 100.f * ActualTotal / TotalCells : 0.f);
	}
	CheckIntegrity(TEXT("초기 빌드"));
	CheckMatchesExpected(TEXT("초기 빌드"));

	// ─── 단일 파괴: 표면 Destructible 셀 1개 → 엔트리 소멸 + 스왑 보정 + 이웃 노출 ───
	{
		const FIntVector Target = FindFirstSurfaceDestructible(Grid);
		TestTrue(TEXT("표면 Destructible 셀 확보"), Target.X >= 0);

		VoxelWorld->ServerDestroyBlocks({ Target });

		// 파괴 셀 엔트리가 모든 타입 맵에서 소멸했는가.
		int32 Residual = 0;
		for (const auto& TypePair : Renderer->CellToInstance)
		{
			if (TypePair.Value.Contains(Target))
			{
				++Residual;
			}
		}
		TestEqual(TEXT("파괴 셀 엔트리 소멸"), Residual, 0);

		// 새로 노출된 이웃(현재 그리드 기준 표면)이 등록됐는가.
		for (const FIntVector& D : Dirs)
		{
			const FIntVector N = Target + D;
			if (!IsSurfaceRef(Grid, N))
			{
				continue;
			}
			const TMap<FIntVector, int32>* C2I = Renderer->CellToInstance.Find(Grid.Get(N));
			TestTrue(*FString::Printf(TEXT("노출 이웃 (%d,%d,%d) 등록"), N.X, N.Y, N.Z),
				C2I && C2I->Contains(N));
		}

		// 스왑된 셀의 맵 갱신 포함 전체 무결성 재검사.
		CheckIntegrity(TEXT("단일 파괴 후"));
		CheckMatchesExpected(TEXT("단일 파괴 후"));
	}

	// ─── 연속 파괴 20회+ : 무결성 + "다시 BuildFromGrid 한 기대 집합"과 완전 일치 ───
	{
		const TArray<FIntVector> Targets = FindDestructibleCells(Grid, 24);
		TestTrue(TEXT("연속 파괴 대상 20개 이상 확보"), Targets.Num() >= 20);

		for (const FIntVector& C : Targets)
		{
			VoxelWorld->ServerDestroyBlocks({ C }); // 1개씩 — RemoveInstance 스왑을 매번 유발
		}

		CheckIntegrity(TEXT("연속 파괴 후"));
		CheckMatchesExpected(TEXT("연속 파괴 후")); // 렌더·그리드 불일치 없음 증명
	}

	// ─── 가림 디더 페이드 배선 (2026-08-06) ───
	//
	// UOcclusionFadeComponent 가 의존하는 통로 전부를 여기서 한 번에 확인한다:
	//   WorldToCellFloat → VoxelRay::GatherSolidCells → SetCellFade → 인스턴스 커스텀 데이터.
	// 카메라(PlayerCameraManager)만 빠지는데, 그건 위치 하나를 읽어 오는 것뿐이다.
	{
		// 파괴 테스트로 지형이 뚫렸으므로 원본 지형으로 되돌린 뒤 검사한다.
		VoxelWorld->ServerInitFromSeed(1234u);
		const FVoxelGrid& FadeGrid = VoxelWorld->GetGrid();

		// ⓐ 실수 셀 좌표 왕복 — CellToWorld(C) 를 넣으면 셀 중심 C + 0.5 가 나와야 한다.
		{
			const FIntVector Probe(3, 4, 1);
			const FVector Back = VoxelWorld->WorldToCellFloat(VoxelWorld->CellToWorld(Probe));
			TestTrue(TEXT("ⓐ WorldToCellFloat: 셀 중심 왕복 (소수부 0.5 보존)"),
				Back.Equals(FVector(Probe.X + 0.5, Probe.Y + 0.5, Probe.Z + 0.5), 1e-4));
		}

		// ⓑ 인스턴스별 커스텀 데이터 슬롯이 실제로 잡혀 있는가 (머티리얼이 읽을 자리).
		for (const auto& Pair : Renderer->HISMs)
		{
			if (Pair.Value)
			{
				TestEqual(TEXT("ⓑ HISM NumCustomDataFloats == 1"), Pair.Value->NumCustomDataFloats, 1);
			}
		}

		// ⓒ 살아 있는 셀에 페이드를 쓰면 그 인스턴스의 커스텀 데이터에 그대로 들어간다.
		FIntVector LiveCell = FIntVector::ZeroValue;
		EBlockType LiveType = EBlockType::Empty;
		int32 LiveIndex = INDEX_NONE;
		for (const auto& TypePair : Renderer->CellToInstance)
		{
			for (const TPair<FIntVector, int32>& C2I : TypePair.Value)
			{
				LiveCell = C2I.Key; LiveType = TypePair.Key; LiveIndex = C2I.Value;
				break;
			}
			if (LiveIndex != INDEX_NONE) break;
		}

		if (TestTrue(TEXT("ⓒ 페이드 대상 인스턴스 확보"), LiveIndex != INDEX_NONE))
		{
			TestTrue(TEXT("ⓒ 살아 있는 셀: SetCellFade 성공"), VoxelWorld->SetCellFade(LiveCell, 0.75f));

			UHierarchicalInstancedStaticMeshComponent* HISM = Renderer->HISMs.FindRef(LiveType);
			if (TestNotNull(TEXT("ⓒ HISM 존재"), HISM))
			{
				const int32 DataIndex = LiveIndex * HISM->NumCustomDataFloats;
				if (TestTrue(TEXT("ⓒ 커스텀 데이터 인덱스 유효"),
					HISM->PerInstanceSMCustomData.IsValidIndex(DataIndex)))
				{
					TestEqual(TEXT("ⓒ 쓴 값이 인스턴스 커스텀 데이터 0번에 들어갔다"),
						HISM->PerInstanceSMCustomData[DataIndex], 0.75f);
				}
			}

			// ⓓ 그 블록이 파괴되면 false — 컴포넌트가 추적을 끊는 신호다.
			//    (없으면 사라진 블록을 영원히 갱신하려 든다)
			if (FadeGrid.Get(LiveCell) == EBlockType::Destructible)
			{
				VoxelWorld->ServerDestroyBlocks({ LiveCell });
				TestFalse(TEXT("ⓓ 파괴된 셀: SetCellFade 가 false (추적 해제 신호)"),
					VoxelWorld->SetCellFade(LiveCell, 0.75f));
			}
		}

		// ⓔ 애초에 인스턴스가 없는 셀(빈 공중)도 false.
		TestFalse(TEXT("ⓔ 빈 셀: SetCellFade 가 false"),
			VoxelWorld->SetCellFade(FIntVector(1, 1, 3), 1.f));

		// ⓕ 전체 사슬 — 맵 밖 상공의 "카메라"에서 바닥 위 캐릭터로 훑으면 외곽 벽이 잡힌다.
		//    폴백 맵의 외곽 링은 z=1~2 Immortal 이므로 낮은 각도로 들어오면 반드시 걸린다.
		{
			const FVector CameraWorld = VoxelWorld->CellToWorld(FIntVector(-4, 10, 3));
			const FVector PawnWorld   = VoxelWorld->CellToWorld(FIntVector(5, 10, 1));

			TArray<FIntVector> Occluders;
			VoxelRay::GatherSolidCells(FadeGrid,
				VoxelWorld->WorldToCellFloat(CameraWorld),
				VoxelWorld->WorldToCellFloat(PawnWorld), 256, Occluders);

			TestTrue(TEXT("ⓕ 전체 사슬: 맵 밖 카메라 → 캐릭터 사이에 가림 블록이 잡힌다"),
				Occluders.Num() > 0);

			bool bAllSolid = true;
			for (const FIntVector& Cell : Occluders)
			{
				if (!FadeGrid.IsSolid(Cell)) { bAllSolid = false; break; }
			}
			TestTrue(TEXT("ⓕ 전체 사슬: 잡힌 칸이 전부 실제 solid"), bAllSolid);

			// 잡힌 칸 전부에 페이드를 걸어 본다 — 표면 블록이면 성공해야 한다
			// (내부에 묻힌 블록은 인스턴스가 없어 false 가 정상이므로 개수만 세지 않는다).
			int32 Applied = 0;
			for (const FIntVector& Cell : Occluders)
			{
				if (VoxelWorld->SetCellFade(Cell, 1.f)) { ++Applied; }
			}
			TestTrue(TEXT("ⓕ 전체 사슬: 잡힌 칸 중 최소 하나에 페이드가 실제로 적용됐다"), Applied > 0);
		}
	}

	// ─── Clear: 인스턴스 0 + 맵 비움 (HISM 컴포넌트는 유지) ───
	{
		Renderer->Clear();
		for (const auto& Pair : Renderer->HISMs)
		{
			TestEqual(*FString::Printf(TEXT("Clear 후 타입 %d 인스턴스 0"), static_cast<int32>(Pair.Key)),
				Pair.Value ? Pair.Value->GetInstanceCount() : 0, 0);
		}
		TestEqual(TEXT("Clear 후 CellToInstance 비움"), Renderer->CellToInstance.Num(), 0);
		TestEqual(TEXT("Clear 후 InstanceToCell 비움"), Renderer->InstanceToCell.Num(), 0);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
