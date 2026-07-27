#include "Voxel/VoxelWorld.h"

#include "CrazyArcade3D.h"
#include "MapGen/FallbackMapGenerator.h"   // Voxel→MapGen 참조는 설계서 2.2가 확정 — .cpp 에서만 include
#include "Framework/CA3DRuleSet.h"         // Voxel→Framework 참조도 동일 — .cpp 에서만 include
#include "Net/UnrealNetwork.h"

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	// 지형은 모든 클라에 항상 필요 — 거리 기반 relevancy 컬링으로 액터가 사라지면
	// 파괴 Multicast를 놓쳐 그리드가 어긋난다. 항상 relevant 로 고정한다.
	bAlwaysRelevant = true;
}

void AVoxelWorld::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AVoxelWorld, Seed);
}

// ─── 좌표 변환 ─────────────────────────────────────────────
// 셀 크기를 아는 유일한 곳. 전부 액터 위치 기준.

FIntVector AVoxelWorld::WorldToCell(const FVector& W) const
{
	// FVector는 double — FloorToInt(double)는 int64를 반환하므로 FloorToInt32로 명시.
	const FVector Local = (W - GetActorLocation()) / CellSize;
	return FIntVector(
		FMath::FloorToInt32(Local.X),
		FMath::FloorToInt32(Local.Y),
		FMath::FloorToInt32(Local.Z));
}

FVector AVoxelWorld::CellToWorld(const FIntVector& C) const
{
	// 셀 중심: 각 축 (C + 0.5) * CellSize. 왕복 보장: WorldToCell(CellToWorld(C)) == C.
	return GetActorLocation() + FVector(
		(C.X + 0.5f) * CellSize,
		(C.Y + 0.5f) * CellSize,
		(C.Z + 0.5f) * CellSize);
}

FVector AVoxelWorld::CellToWorldFloor(const FIntVector& C) const
{
	// X/Y는 셀 중심, Z만 셀 바닥면.
	return GetActorLocation() + FVector(
		(C.X + 0.5f) * CellSize,
		(C.Y + 0.5f) * CellSize,
		C.Z * CellSize);
}

// ─── 서버 전용 ─────────────────────────────────────────────

void AVoxelWorld::ServerInitFromSeed(uint32 InSeed)
{
	if (!HasAuthority()) return; // 불변식 5 — 상태를 바꾸는 함수는 서버 전용

	Seed = InSeed; // 복제 프로퍼티 기록 — 클라는 OnRep_Seed로 동일 맵을 재생성한다
	InitGridFromSeed();
}

void AVoxelWorld::ServerDestroyBlocks(const TArray<FIntVector>& Cells)
{
	if (!HasAuthority()) return; // 불변식 5

	ApplyDestruction(Cells);
	MulticastOnBlocksDestroyed(Cells);
}

// ─── 리플리케이션 수신 ─────────────────────────────────────

void AVoxelWorld::OnRep_Seed()
{
	// 클라: 서버와 동일한 결정론 생성 경로로 맵 구성.
	// InitGridFromSeed 내부에서 렌더 빌드 + PendingDestroyQueue flush까지 수행한다
	// (알려진 함정: 파괴 Multicast가 이 OnRep보다 먼저 도착할 수 있다).
	InitGridFromSeed();
}

void AVoxelWorld::MulticastOnBlocksDestroyed_Implementation(const TArray<FIntVector>& Cells)
{
	// 리슨 서버/서버 로컬 실행 중복 방지 — 서버는 ServerDestroyBlocks에서
	// 이미 ApplyDestruction을 수행했으므로 여기서 또 하면 이중 파괴가 된다.
	if (HasAuthority()) return;

	if (bGridInitialized)
	{
		ApplyDestruction(Cells);
	}
	else
	{
		// 그리드 초기화 전 선도착 — 큐에 쌓아두고 OnRep_Seed 직후 flush.
		PendingDestroyQueue.Append(Cells);
	}
}

// ─── 내부 공통 경로 ───────────────────────────────────────

void AVoxelWorld::InitGridFromSeed()
{
	// 서버(ServerInitFromSeed)·클라(OnRep_Seed) 공통 경로 — 결정론 생성기이므로
	// 같은 Seed면 양쪽 그리드가 비트 단위로 동일하다.

	// Task 22에서 이 한 줄만 절차 생성기(UProceduralMapGenerator)로 바꾼다.
	UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();

	// TODO(Task 08/09): GameState에 복제된 룰셋으로 교체. 지금은 임시로 기본값 사용.
	const UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>();

	// 스폰/아이템 소비는 Task 09 GameMode 소관 — 이 Task에선 버린다.
	TArray<FIntVector> OutSpawns;
	TArray<FItemPlacement> OutItems;

	if (!Generator->Generate(Seed, Rules, Grid, OutSpawns, OutItems))
	{
		UE_LOG(LogCA3D, Error, TEXT("AVoxelWorld: Seed %u 맵 생성 실패"), Seed);
		return;
	}

	bGridInitialized = true;

	// 렌더러는 클라에서만 존재 (데디는 nullptr — null 가드).
	if (Renderer)
	{
		Renderer->BuildFromGrid(Grid);
	}

	// 선도착 파괴 큐 flush — 서버는 실제로 안 쌓이지만 공통 경로를 유지한다.
	if (PendingDestroyQueue.Num() > 0)
	{
		UE_LOG(LogCA3D, Log, TEXT("AVoxelWorld: 선도착 파괴 셀 %d개 flush"), PendingDestroyQueue.Num());
		ApplyDestruction(PendingDestroyQueue);
		PendingDestroyQueue.Empty();
	}
}

void AVoxelWorld::ApplyDestruction(const TArray<FIntVector>& Cells)
{
	// 불변식 1 — 파괴의 단일 경로. 이 함수 밖에서 파괴 목적의 Grid.Set 금지.
	for (const FIntVector& Cell : Cells)
	{
		Grid.Set(Cell, EBlockType::Empty);

		if (Renderer)
		{
			Renderer->RemoveBlock(Cell, Grid);
		}
	}
}
