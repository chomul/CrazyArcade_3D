#include "Gameplay/Bomb/ExplosionFXRelay.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Bomb/WaterSegment.h"
#include "Core/PoolSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "EngineUtils.h"

AExplosionFXRelay::AExplosionFXRelay()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	// 지형 파괴 Multicast(AVoxelWorld)와 같은 이유 — 거리 컬링으로 FX Multicast 를 놓치면 안 된다.
	bAlwaysRelevant = true;
}

AVoxelWorld* AExplosionFXRelay::ResolveVoxelWorld()
{
	if (!CachedVoxelWorld)
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약
		}
	}
	return CachedVoxelWorld;
}

void AExplosionFXRelay::MulticastWaterCells_Implementation(const TArray<FIntVector>& Cells)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (리슨 호스트는 플레이어라 표시)

	AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;
	if (!VoxelWorld || !Pool)
	{
		// CellToWorld 는 그리드 초기화와 무관(액터 위치 + CellSize)이라 VoxelWorld 액터만 있으면 된다.
		UE_LOG(LogCA3D, Warning, TEXT("AExplosionFXRelay: VoxelWorld/풀 미확보 — 물줄기 FX 생략 (판정은 서버가 이미 완료)"));
		return;
	}

	// 룰셋 해석 — 잔존 시간·세그먼트 클래스의 출처. 복제 미도착이면 CDO 폴백
	// (시각 전용이라 잠시 기본값이어도 무해 — CA3DPlayerController 의 ResolveRules 와 동일 관례).
	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();
	if (const ACA3DGameState* GameState = GetWorld()->GetGameState<ACA3DGameState>())
	{
		if (GameState->Rules)
		{
			Rules = GameState->Rules;
		}
	}

	// 세그먼트 클래스: 룰셋 미지정이면 C++ 기본 — 에셋 없이도 동작해야 한다 (세그먼트가 경고 로그).
	const TSubclassOf<AWaterSegment> SegmentClass =
		Rules->WaterSegmentClass ? Rules->WaterSegmentClass : TSubclassOf<AWaterSegment>(AWaterSegment::StaticClass());

	for (const FIntVector& Cell : Cells)
	{
		AWaterSegment* Segment = Pool->Acquire<AWaterSegment>(SegmentClass, FTransform(VoxelWorld->CellToWorld(Cell)));
		if (Segment)
		{
			Segment->StartLinger(Rules->WaterLingerTime);
		}
	}
}
