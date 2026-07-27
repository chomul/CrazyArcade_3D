#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VoxelRenderer.generated.h"

struct FVoxelGrid;

UINTERFACE()
class CRAZYARCADE3D_API UVoxelRenderer : public UInterface
{
	GENERATED_BODY()
};

// 지형 렌더링 추상화. 구현체는 클라에서만 생성된다 (데디 서버는 nullptr).
// 논리 데이터(FVoxelGrid)와 렌더링의 완전 분리가 목적 (GDD 7.1).
// "그리디 메싱 승격"이 이 인터페이스 구현체 교체 하나로 끝난다 — AVoxelWorld는 어떻게 그리는지 모른다.
class CRAZYARCADE3D_API IVoxelRenderer
{
	GENERATED_BODY()

public:
	// 그리드 전체로부터 렌더 상태를 처음부터 구축한다 (맵 로드 시 1회).
	virtual void BuildFromGrid(const FVoxelGrid& Grid) = 0;

	// 셀 하나가 파괴됐다. 해당 인스턴스 제거 + 주변 6칸 재검사로
	// 새로 노출된 블록을 추가한다. 갱신 비용 최대 6.
	virtual void RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid) = 0;

	// 렌더 상태 전부 제거 (맵 재시작).
	virtual void Clear() = 0;
};
