#pragma once

#include "CoreMinimal.h"

struct FVoxelGrid;

// 그리드를 관통하는 직선이 지나는 셀을 모으는 순수 함수 (Task: 가림 디더 페이드).
//
// **월드 좌표도 CellSize 도 모른다** — 입력이 이미 "실수 셀 좌표"다. 변환은 CellSize 의 유일한
// 출처인 AVoxelWorld(WorldToCellFloat) 소관이고, 여기는 격자 위의 기하만 다룬다.
// 그래서 Voxel 이 Gameplay 를 참조하지 않은 채로 남고(폴더 의존 규칙), PIE 없이 테스트된다
// — UExplosionSubsystem::Propagate 를 순수 함수로 둔 것과 같은 이유(불변식 2의 정신).
namespace VoxelRay
{
	// StartCell → EndCell 직선이 통과하는 **solid 셀**을 통과 순서대로 OutCells 에 append 한다.
	// 첫 solid 에서 멈추지 않는다 — 카메라와 캐릭터 **사이의 블록 전부**를 페이드해야 하므로.
	//
	// 알고리즘은 Amanatides & Woo 3D DDA: 세 축의 "다음 셀 경계까지 거리" 중 가장 가까운 축으로
	// 한 칸씩 전진한다. 셀을 건너뛰지 않는 것이 핵심 — 대각선으로 훑으면 사이에 낀 블록이
	// 빠지고, 그 블록만 안 지워져서 캐릭터가 반쯤 가려진다.
	//
	// 그리드 밖도 그대로 전진한다 (카메라는 보통 맵 밖 하늘에 있다). FVoxelGrid::Get 이 범위 밖을
	// Empty 로 돌려주므로 경계 검사가 따로 필요 없다.
	//
	// MaxSteps 는 폭주 방지용 상한 — 한 축당 셀 수 × 3 정도면 충분하다.
	void GatherSolidCells(const FVoxelGrid& Grid, const FVector& StartCell, const FVector& EndCell,
	                      int32 MaxSteps, TArray<FIntVector>& OutCells);
}
