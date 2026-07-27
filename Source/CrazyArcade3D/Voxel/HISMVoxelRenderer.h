#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Voxel/VoxelRenderer.h"
#include "Voxel/VoxelTypes.h"
#include "HISMVoxelRenderer.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
struct FVoxelGrid;

// AVoxelWorld에 부착되는 렌더 컴포넌트. 클라 전용 —
// 생성·빌드 함수 최상단에서 if (IsRunningDedicatedServer()) return; (불변식 5).
//
// 표면 추출: 6방향 이웃 중 하나라도 Empty인 블록만 인스턴스를 만든다.
// 내부에 완전히 묻힌 블록은 그리지 않다가, 파괴로 노출되는 순간 RemoveBlock의
// 주변 6칸 재검사에서 추가된다 (갱신 비용 최대 6).
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class CRAZYARCADE3D_API UHISMVoxelRenderer : public UActorComponent, public IVoxelRenderer
{
	GENERATED_BODY()

public:
	UHISMVoxelRenderer();

	virtual void BuildFromGrid(const FVoxelGrid& Grid) override;
	virtual void RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid) override;
	virtual void Clear() override;

protected:
	// 블록 타입별 메시 — BP 서브클래스에서 에셋만 지정 (BP에 로직 금지).
	// 머티리얼은 메시/BP가 가진 그대로 사용 — 동적 머티리얼 생성 금지 (GDD 7.4).
	UPROPERTY(EditDefaultsOnly, Category="Voxel")
	TMap<EBlockType, TObjectPtr<UStaticMesh>> BlockMeshes;

private:
	// 블록 타입별 HISM 컴포넌트. BuildFromGrid에서 최초 필요 시 동적 생성.
	UPROPERTY()
	TMap<EBlockType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> HISMs;

	// 셀→인스턴스 / 인스턴스→셀 맵 — 인스턴스 인덱스는 HISM 컴포넌트별 공간이므로 "타입별"로 분리 관리한다.
	TMap<EBlockType, TMap<FIntVector, int32>> CellToInstance;
	TMap<EBlockType, TMap<int32, FIntVector>> InstanceToCell;

	// 셀이 Empty가 아니고, 6방향 이웃 중 하나라도 Empty(범위 밖 포함)면 표면.
	bool IsSurface(const FVoxelGrid& Grid, const FIntVector& C) const;

	// 셀의 타입에 맞는 HISM에 인스턴스 추가 + 양방향 맵 등록. 이미 등록된 셀이면 무시.
	void AddInstanceForCell(const FVoxelGrid& Grid, const FIntVector& C);

	// 타입별 HISM 조회, 없으면 동적 생성 (메시 미지정이면 경고 로그 — 생성 시 1회).
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(EBlockType Type);

	// 소유 AVoxelWorld의 CellSize. 캐스트 실패 시 100 폴백.
	float GetCellSize() const;

	friend class FHISMVoxelRendererTest; // 자동화 테스트가 인덱스 맵 무결성을 검증하기 위한 접근
};
