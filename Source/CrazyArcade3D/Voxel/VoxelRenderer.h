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

	// 셀 하나의 "페이드 값"을 기록한다. 0 = 평소, 1 = 최대로 사라짐 (가림 디더 페이드).
	//
	// **지형은 왜 페이드하는지 모른다.** 카메라도 캐릭터도 모르는 채로 숫자만 받는다 —
	// 그래야 폴더 의존 규칙(Voxel ⇏ Gameplay)이 유지되고, 나중에 "적을 강조" 같은 다른
	// 이유로 같은 통로를 재사용할 수 있다. 무엇을 페이드할지는 Gameplay 가 정한다.
	//
	// 반환값 = 그 셀에 지금 렌더 대상(인스턴스)이 있었는가. false 면 호출부는 그 셀을
	// 추적 목록에서 지워야 한다 — 파괴된 블록을 계속 페이드하려 들면 헛돈다.
	// 기본 구현은 no-op: 그리디 메싱으로 교체할 때 셀 단위 페이드가 불가능해도
	// 인터페이스를 만족시킬 수 있어야 한다 (그 경우 가림 처리는 다른 방식으로 간다).
	virtual bool SetCellFade(const FIntVector& Cell, float Value) { return false; }

	// 진단용 — 그 셀의 렌더 인스턴스에 **실제로 들어가 있는** 페이드 값을 되읽는다.
	// 없으면 -1. "C++ 이 값을 넣었는가"와 "머티리얼이 그 값을 쓰는가"를 가르는 유일한 방법이다:
	// 여기서 0.8 이 나오는데 화면이 그대로면 원인은 100% 머티리얼 쪽이다.
	virtual float GetCellFade(const FIntVector& Cell) const { return -1.f; }
};
