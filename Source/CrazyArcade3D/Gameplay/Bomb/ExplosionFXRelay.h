#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "ExplosionFXRelay.generated.h"

class AVoxelWorld;

// 물줄기 셀 Multicast 의 소유 액터 (Task 16) — 서버 ExplosionSubsystem 이 lazy 스폰.
//
// Multicast 는 액터에만 가능한데 서브시스템은 액터가 아니다. 소유 액터 후보 검토:
//   ABomb        — 폭발 직후 Destroy 되므로 같은 프레임의 Reliable RPC 전송이 보장되지 않음. 탈락.
//   AVoxelWorld  — 폴더 의존 규칙상 Voxel 은 아무것도 몰라야 한다("게임 규칙을 몰라야 함").
//                  물줄기 FX·풀·룰셋을 알게 되는 순간 구조가 무너짐. 탈락.
//   전용 릴레이  — Gameplay/Bomb 안에 상주하는 가벼운 AInfo. 채택.
// bAlwaysRelevant: 지형 파괴와 같은 이유 — 컬링으로 Multicast 를 놓치면 FX 가 어긋난다.
UCLASS()
class CRAZYARCADE3D_API AExplosionFXRelay : public AInfo
{
	GENERATED_BODY()

public:
	AExplosionFXRelay();

	// 서버 → 전 클라: 이번 연쇄 단계의 물줄기 셀 목록. 클라는 풀(Task 14)에서
	// AWaterSegment 를 획득해 배치하고, WaterLingerTime(룰셋) 후 세그먼트가 스스로 반납한다.
	// 판정(ServerTrap·파괴)은 서버가 이미 끝냈다 — 이 경로는 순수 시각.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWaterCells(const TArray<FIntVector>& Cells);

private:
	// 셀 → 월드 변환용 (CellSize 의 유일한 출처). lazy 탐색·캐시.
	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	AVoxelWorld* ResolveVoxelWorld();
};
