#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptInterface.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelRenderer.h" // 인터페이스 자체가 가벼워 헤더 include 허용
#include "VoxelWorld.generated.h"

class UHISMVoxelRenderer;

// 복셀 지형 액터. 레벨에 1개 배치. bReplicates = true.
//
// 리플리케이션 흐름 (불변식 1 — 서버·클라가 같은 함수를 통과한다):
//   [서버] ServerDestroyBlocks → ApplyDestruction + Multicast
//                                                      └─▶ [클라] ApplyDestruction
// 알려진 함정: 파괴 Multicast가 OnRep_Seed보다 먼저 도착할 수 있다.
// 그리드 초기화 전에 받은 파괴 셀은 PendingDestroyQueue에 쌓고 OnRep_Seed 직후 flush.
UCLASS()
class CRAZYARCADE3D_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	// ─── 조회 (서버·클라 공통, O(1)) ───
	EBlockType GetBlock(const FIntVector& C) const { return Grid.Get(C); }
	bool IsSolid(const FIntVector& C) const { return Grid.IsSolid(C); }
	const FVoxelGrid& GetGrid() const { return Grid; }   // Propagate 등 읽기 전용 접근

	// ─── 좌표 변환 (셀 크기를 아는 유일한 곳) ───
	FIntVector WorldToCell(const FVector& W) const;
	FVector    CellToWorld(const FIntVector& C) const;      // 셀 중심
	FVector    CellToWorldFloor(const FIntVector& C) const; // 셀 바닥면 중심

	// ─── 서버 전용 ───
	void ServerInitFromSeed(uint32 InSeed);
	void ServerDestroyBlocks(const TArray<FIntVector>& Cells);

	// ⚠️ 임시값 — 확정은 Task 10 튜닝에서. 임의 변경 금지, 질문할 것.
	UPROPERTY(EditAnywhere, Category="Voxel")
	float CellSize = 100.f;

	// ⚠️ 임시 (Task 09에서 제거) — GameMode가 아직 없어 서버가 BeginPlay에서 스스로 초기화한다.
	// 정식 흐름은 ACA3DGameMode가 시드를 정해 ServerInitFromSeed를 호출하는 것.
	UPROPERTY(EditAnywhere, Category="Voxel|Temp")
	bool bDebugAutoInit = true;

	UPROPERTY(EditAnywhere, Category="Voxel|Temp", meta=(EditCondition="bDebugAutoInit"))
	int32 DebugSeed = 1;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 맵 생성 시드. 클라는 OnRep으로 서버와 동일한 맵을 결정론적으로 재생성한다.
	UPROPERTY(ReplicatedUsing=OnRep_Seed)
	uint32 Seed = 0;

	UFUNCTION() void OnRep_Seed();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnBlocksDestroyed(const TArray<FIntVector>& Cells);

private:
	FVoxelGrid Grid;

	// 클라/리슨에서만 연결, 데디 서버는 nullptr (BeginPlay에서 분기).
	UPROPERTY()
	TScriptInterface<IVoxelRenderer> Renderer;

	// C++ 기본 렌더러 컴포넌트 (Task 07). 생성자 CreateDefaultSubobject로 CDO에 존재해야
	// BP_VoxelWorld 서브클래스에서 BlockMeshes 디폴트(메시 에셋)를 편집할 수 있다
	// (BeginPlay NewObject 컴포넌트는 BP 디폴트를 못 받는다).
	// 데디 서버는 BeginPlay에서 DestroyComponent — 시각 전용 (불변식 5).
	UPROPERTY(VisibleAnywhere, Category="Voxel")
	TObjectPtr<UHISMVoxelRenderer> HISMRendererComponent;

	// 그리드 초기화 전에 도착한 파괴 Multicast의 셀 보관 큐 (OnRep_Seed 직후 flush).
	TArray<FIntVector> PendingDestroyQueue;

	bool bGridInitialized = false;

	// 서버·클라 공통 그리드 생성 경로. 성공 시 bGridInitialized + 렌더 빌드 + 큐 flush.
	void InitGridFromSeed();

	// 파괴의 단일 경로 (불변식 1). 서버든 클라든 반드시 이 함수로만 들어온다.
	void ApplyDestruction(const TArray<FIntVector>& Cells);

	friend class FVoxelWorldTest; // 자동화 테스트가 OnRep_Seed·Multicast 경로(파괴 선도착 큐)를 검증하기 위한 접근
	friend class FHISMVoxelRendererTest; // BeginPlay가 돌지 않는 테스트 월드에서 Renderer 수동 배선용
};
