#include "Voxel/HISMVoxelRenderer.h"

#include "CrazyArcade3D.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelWorld.h" // 같은 Voxel 폴더 — CellSize 조회용 캐스트, 의존 규칙 위반 아님
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	// 6방향 이웃 오프셋 (표면 판정·재검사 공용).
	const FIntVector GNeighborDirs[6] = {
		FIntVector( 1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector( 0, 1, 0), FIntVector( 0,-1, 0),
		FIntVector( 0, 0, 1), FIntVector( 0, 0,-1),
	};
}

UHISMVoxelRenderer::UHISMVoxelRenderer()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ─── IVoxelRenderer ────────────────────────────────────────

void UHISMVoxelRenderer::BuildFromGrid(const FVoxelGrid& Grid)
{
	// ⚠️ 데디 서버 가드를 두지 않는다 (2026-08-02 수정). HISM 인스턴스가 곧 블록의 **컬리전**이라
	// 서버에서 안 만들면 바닥이 없어 캐릭터가 지형을 통과한다 — 서버가 이동 권한을 가지므로 치명적이다.
	// 불변식 5 는 "시각만 만지는 함수"에 적용되는데, 이 함수는 시각이 아니라 형상을 만든다.
	Clear();

	int32 SolidCount = 0;
	for (int32 Z = 0; Z < Grid.Size.Z; ++Z)
	{
		for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
		{
			for (int32 X = 0; X < Grid.Size.X; ++X)
			{
				const FIntVector C(X, Y, Z);
				if (Grid.Get(C) == EBlockType::Empty)
				{
					continue;
				}
				++SolidCount;

				// 표면 블록만 인스턴스 생성 — 내부 블록은 파괴로 노출될 때 RemoveBlock이 추가.
				if (IsSurface(Grid, C))
				{
					AddInstanceForCell(Grid, C);
				}
			}
		}
	}

	int32 InstanceCount = 0;
	for (const auto& Pair : CellToInstance)
	{
		InstanceCount += Pair.Value.Num();
	}

	// 체크리스트 "표면 추출 비율" 확인용 로그.
	const int32 TotalCells = Grid.Size.X * Grid.Size.Y * Grid.Size.Z;
	UE_LOG(LogCA3D, Log,
		TEXT("HISMVoxelRenderer: 총 셀 %d / 솔리드 %d / 표면 인스턴스 %d (솔리드 대비 %.1f%%, 총 셀 대비 %.1f%%)"),
		TotalCells, SolidCount, InstanceCount,
		SolidCount > 0 ? 100.f * InstanceCount / SolidCount : 0.f,
		TotalCells > 0 ? 100.f * InstanceCount / TotalCells : 0.f);
}

void UHISMVoxelRenderer::RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid)
{
	// ⚠️ 여기도 데디 가드를 두면 안 된다 — 서버에서만 파괴가 반영되지 않으면
	// **서버는 없는 벽에 막히고 클라는 그 자리를 지나간다** (BuildFromGrid 주석).

	// 1. 그 셀의 인스턴스 제거. 파괴 후 Grid는 이미 Empty라 타입을 그리드에서 못 얻는다 —
	//    모든 타입 맵에서 탐색한다. (셀은 한 타입에만 존재하므로 찾으면 break.)
	for (auto& TypePair : CellToInstance)
	{
		const EBlockType Type = TypePair.Key;
		TMap<FIntVector, int32>& C2I = TypePair.Value;

		const int32* IndexPtr = C2I.Find(Cell);
		if (!IndexPtr)
		{
			continue;
		}

		const int32 RemovedIndex = *IndexPtr;
		TMap<int32, FIntVector>& I2C = InstanceToCell.FindOrAdd(Type);
		UHierarchicalInstancedStaticMeshComponent* HISM = HISMs.FindRef(Type);

		if (ensure(HISM))
		{
			// ⚠️ 알려진 함정 — 스왑 보정: RemoveInstance는 마지막 인덱스의 인스턴스를
			// 제거된 자리로 당겨온다(RemoveAtSwap). 제거 전 LastIndex를 기억해 두고,
			// 제거 인덱스 ≠ LastIndex면 LastIndex에 있던 셀의 양쪽 맵 엔트리를
			// 제거 인덱스로 갱신한다. 안 하면 엉뚱한 블록이 사라진다.
			const int32 LastIndex = HISM->GetInstanceCount() - 1;
			HISM->RemoveInstance(RemovedIndex);

			C2I.Remove(Cell);
			I2C.Remove(RemovedIndex);

			if (RemovedIndex != LastIndex)
			{
				const FIntVector MovedCell = I2C.FindChecked(LastIndex);
				I2C.Remove(LastIndex);
				I2C.Add(RemovedIndex, MovedCell);
				C2I[MovedCell] = RemovedIndex;
			}
		}
		else
		{
			// HISM이 없는데 맵 엔트리만 남은 비정상 상태 — 엔트리만 정리.
			C2I.Remove(Cell);
			I2C.Remove(RemovedIndex);
		}
		break;
	}

	// 2. 주변 6칸 재검사 — 새로 노출된 표면 블록 등록.
	//    인스턴스가 없던 셀(비표면 내부 셀)의 파괴여도 반드시 수행한다.
	for (const FIntVector& Dir : GNeighborDirs)
	{
		const FIntVector N = Cell + Dir;
		if (!IsSurface(Grid, N))
		{
			continue;
		}
		const TMap<FIntVector, int32>* C2I = CellToInstance.Find(Grid.Get(N));
		if (!C2I || !C2I->Contains(N))
		{
			AddInstanceForCell(Grid, N);
		}
	}
}

void UHISMVoxelRenderer::Clear()
{
	// 인스턴스와 맵만 비운다. HISM 컴포넌트 자체는 유지 — 맵 재시작 시 재사용.
	for (const auto& Pair : HISMs)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}
	CellToInstance.Empty();
	InstanceToCell.Empty();
}

// ─── 내부 ──────────────────────────────────────────────────

bool UHISMVoxelRenderer::IsSurface(const FVoxelGrid& Grid, const FIntVector& C) const
{
	if (Grid.Get(C) == EBlockType::Empty)
	{
		return false;
	}
	for (const FIntVector& Dir : GNeighborDirs)
	{
		// 범위 밖도 Get이 Empty를 반환하므로 그리드 가장자리는 자동으로 표면.
		if (Grid.Get(C + Dir) == EBlockType::Empty)
		{
			return true;
		}
	}
	return false;
}

void UHISMVoxelRenderer::AddInstanceForCell(const FVoxelGrid& Grid, const FIntVector& C)
{
	const EBlockType Type = Grid.Get(C);
	if (Type == EBlockType::Empty)
	{
		return;
	}

	TMap<FIntVector, int32>& C2I = CellToInstance.FindOrAdd(Type);
	if (C2I.Contains(C))
	{
		return; // 이미 등록된 셀
	}

	UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Type);
	if (!HISM)
	{
		return;
	}

	// 인스턴스 트랜스폼은 컴포넌트 로컬 공간(bWorldSpace=false).
	// 위치: 셀 중심 (C + 0.5) * CellSize — AVoxelWorld::CellToWorld와 동일 규약.
	// 스케일: CellSize / 100 — 엔진 기본 큐브(100유닛, 중심 피벗) 기준.
	const float CellSize = GetCellSize();
	const FTransform InstanceXform(
		FRotator::ZeroRotator,
		FVector((C.X + 0.5f) * CellSize, (C.Y + 0.5f) * CellSize, (C.Z + 0.5f) * CellSize),
		FVector(CellSize / 100.f));

	const int32 Index = HISM->AddInstance(InstanceXform, /*bWorldSpace=*/false);
	C2I.Add(C, Index);
	InstanceToCell.FindOrAdd(Type).Add(Index, C);
}

bool UHISMVoxelRenderer::SetCellFade(const FIntVector& Cell, float Value)
{
	// 어느 타입의 HISM 에 있는지 모르므로 등록된 타입을 훑는다 (타입 수는 한 자릿수).
	for (const TPair<EBlockType, TMap<FIntVector, int32>>& TypePair : CellToInstance)
	{
		const int32* Index = TypePair.Value.Find(Cell);
		if (!Index)
		{
			continue;
		}

		const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* HISM = HISMs.Find(TypePair.Key);
		if (!HISM || !*HISM)
		{
			continue;
		}

		// bMarkRenderStateDirty = false 가 중요하다: true 면 프리미티브 프록시를 통째로 다시
		// 만든다. 커스텀 데이터 변경은 엔진이 인스턴스 데이터 델타로 따로 반영하므로
		// (FPrimitiveInstanceDataManager::CustomDataChanged) 프록시 재생성이 필요 없다.
		// 페이드는 매 프레임 갱신되므로 여기서 프록시를 재생성하면 그대로 프레임을 먹는다.
		return (*HISM)->SetCustomDataValue(*Index, /*CustomDataIndex=*/0, Value,
			/*bMarkRenderStateDirty=*/false);
	}

	return false; // 그 셀에 인스턴스가 없다 (파괴됐거나 내부에 묻힌 블록) — 호출부가 추적을 끊는다
}

float UHISMVoxelRenderer::GetCellFade(const FIntVector& Cell) const
{
	for (const TPair<EBlockType, TMap<FIntVector, int32>>& TypePair : CellToInstance)
	{
		const int32* Index = TypePair.Value.Find(Cell);
		if (!Index)
		{
			continue;
		}

		const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* HISM = HISMs.Find(TypePair.Key);
		if (!HISM || !*HISM || (*HISM)->NumCustomDataFloats <= 0)
		{
			continue;
		}

		const int32 DataIndex = *Index * (*HISM)->NumCustomDataFloats;
		if ((*HISM)->PerInstanceSMCustomData.IsValidIndex(DataIndex))
		{
			return (*HISM)->PerInstanceSMCustomData[DataIndex];
		}
	}

	return -1.f;
}

UHierarchicalInstancedStaticMeshComponent* UHISMVoxelRenderer::GetOrCreateHISM(EBlockType Type)
{
	if (const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = HISMs.Find(Type))
	{
		return Found->Get();
	}

	AActor* Owner = GetOwner();
	if (!ensure(Owner))
	{
		return nullptr;
	}

	UHierarchicalInstancedStaticMeshComponent* HISM =
		NewObject<UHierarchicalInstancedStaticMeshComponent>(Owner);
	if (USceneComponent* Root = Owner->GetRootComponent())
	{
		HISM->SetupAttachment(Root);
	}

	// 가림 페이드용 인스턴스별 float 1개 (SetCellFade). **RegisterComponent 이전에** 정해야
	// 인스턴스 데이터 버퍼가 처음부터 그 크기로 잡힌다.
	// 머티리얼이 안 읽으면 값이 놀 뿐 렌더링에 영향이 없다 — 에셋 작업 전에도 안전.
	HISM->NumCustomDataFloats = 1;

	HISM->RegisterComponent();

	const TObjectPtr<UStaticMesh>* MeshPtr = BlockMeshes.Find(Type);
	if (MeshPtr && *MeshPtr)
	{
		// 동적 머티리얼 생성 금지 (GDD 7.4) — 머티리얼은 메시/BP가 가진 그대로.
		HISM->SetStaticMesh(MeshPtr->Get());
	}
	else
	{
		// 컴포넌트는 타입별 1회 생성이므로 이 경고도 타입별 1회.
		// 메시 미지정(테스트/에셋 미지정) 상태에서도 인스턴스 카운트 로직은 동작해야 한다.
		UE_LOG(LogCA3D, Warning,
			TEXT("HISMVoxelRenderer: EBlockType %d 메시 미지정 — 인스턴스는 등록되지만 렌더링되지 않는다"),
			static_cast<int32>(Type));
	}

	HISMs.Add(Type, HISM);
	return HISM;
}

float UHISMVoxelRenderer::GetCellSize() const
{
	// 셀 크기를 아는 곳은 AVoxelWorld뿐 (같은 Voxel 폴더 — 의존 규칙 위반 아님).
	if (const AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(GetOwner()))
	{
		return VoxelWorld->CellSize;
	}
	return 100.f; // 캐스트 실패 폴백
}
