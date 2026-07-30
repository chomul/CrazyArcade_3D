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

	// 그리드가 생성됐는가 — 클라의 프리뷰 계산(ABomb)이 OnRep_Seed 선후 관계를 확인하는 용도.
	bool IsGridInitialized() const { return bGridInitialized; }

	// 그리드 변경(파괴 적용) 알림 — 서버·클라 각자 ApplyDestruction 직후 로컬 브로드캐스트.
	// 소비처 예: ABomb 위험 프리뷰 갱신 (퓨즈 중 지형이 바뀌면 표시 범위가 실폭발과 어긋나므로 재계산).
	// Voxel 은 구독자가 누구인지 모른다 — "게임 규칙을 몰라야 함" 의존 규칙 유지.
	DECLARE_MULTICAST_DELEGATE(FOnGridChanged);
	FOnGridChanged OnGridChanged;

	// ─── 좌표 변환 (셀 크기를 아는 유일한 곳) ───
	FIntVector WorldToCell(const FVector& W) const;
	FVector    CellToWorld(const FIntVector& C) const;      // 셀 중심
	FVector    CellToWorldFloor(const FIntVector& C) const; // 셀 바닥면 중심

	// ─── 서버 전용 ───
	void ServerInitFromSeed(uint32 InSeed);
	void ServerDestroyBlocks(const TArray<FIntVector>& Cells);

	// 생성기가 반환한 스폰 셀 — 서버 GameMode(ChoosePlayerStart)가 소비한다 (Task 09).
	// 결정론 생성이라 클라에도 같은 값이 만들어지지만 소비처는 서버뿐이다.
	const TArray<FIntVector>& GetSpawnCells() const { return SpawnCells; }

	// ⚠️ 임시값 — 확정은 Task 10 튜닝에서. 임의 변경 금지, 질문할 것.
	UPROPERTY(EditAnywhere, Category="Voxel")
	float CellSize = 100.f;

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

	// 생성기 출력 스폰 셀 보관 (GetSpawnCells). 아이템 배치(OutItems)는 아직 소비처가
	// 없어(Task 23) 보관하지 않는다 — 소비처가 생기면 같은 방식으로 보관한다.
	TArray<FIntVector> SpawnCells;

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

	// 파괴 적용 순번 (인스턴스 로컬). 🏁 게이트용 자동 해시 로그의 대조 키 — 서버·클라가
	// 같은 파괴를 각자 적용하므로 같은 #N 끼리 해시가 같아야 한다 (설계서 5장 11번).
	int32 DestructionApplyCount = 0;

	// 서버·클라 공통 그리드 생성 경로. 성공 시 bGridInitialized + 렌더 빌드 + 큐 flush.
	void InitGridFromSeed();

	// 파괴의 단일 경로 (불변식 1). 서버든 클라든 반드시 이 함수로만 들어온다.
	void ApplyDestruction(const TArray<FIntVector>& Cells);

	friend class FVoxelWorldTest; // 자동화 테스트가 OnRep_Seed·Multicast 경로(파괴 선도착 큐)를 검증하기 위한 접근
	friend class FBombTest; // 자동화 테스트가 손그리드 구성(결정론 시나리오)을 위한 접근 (Task 16)
	friend class FPredictedBombVisualTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 17)
	friend class FHISMVoxelRendererTest; // BeginPlay가 돌지 않는 테스트 월드에서 Renderer 수동 배선용
	friend class FDeathHandlingTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 27)
};
