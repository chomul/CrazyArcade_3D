#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MapGen/MapGenerator.h"
#include "FallbackMapGenerator.generated.h"

// Seed를 무시하고 항상 같은 하드코딩 맵을 반환하는 폴백 맵 생성기 (Task 04).
// 1주차 코어 검증용 안전장치 — 절차 생성기(Task 22)가 미완이어도 게임이 돌게 한다.
//
// 레이아웃 (GDD 2.1 / 4.1, Rules->MapSize 기본 21×21×4 기준):
//  - z=0 전체 Floor.
//  - 외곽 1칸 둘레는 Immortal 벽, z=1~2 두 층.
//  - 내부 [2, Size-3] 영역: x,y 둘 다 짝수인 칸에 Immortal 기둥(원작 크아식 격자),
//    복도 칸(x,y 중 하나만 짝수) 중 (x+y)%4==1 인 칸에 Destructible.
//    x 또는 y == 1 / Size-2 인 "스폰 링"은 완전히 비워 순환 통로를 확보한다.
//  - 계단식 접근로 1곳 (기본 21×21×4 기준 정확한 좌표 — Task 10 점프 튜닝 검증 지점):
//      접근 칸  (8, 9, z=1) Empty → 1단 블록 (9, 9, z=1) Immortal
//      → 2단 스택 (10, 9, z=1)+(10, 9, z=2) Immortal.
//    1블록 점프만으로 발 높이 z=1 → z=2 → z=3 으로 오를 수 있다.
//  - 스폰 8개: 코너 4 + 변 중앙 4, 전부 스폰 링 위 (z=1 Empty, 아래 z=0 Floor).
//
// 결정론: 랜덤·float를 일절 쓰지 않는다 — 전부 정수 좌표 루프/하드코딩 (불변식 4).
// MapSize가 최소 요건(9×9×3) 미만이거나 Rules가 nullptr이면 false 반환.
UCLASS()
class CRAZYARCADE3D_API UFallbackMapGenerator : public UObject, public IMapGenerator
{
	GENERATED_BODY()

public:
	virtual bool Generate(uint32 Seed, const UCA3DRuleSet* Rules,
	                      FVoxelGrid& OutGrid,
	                      TArray<FIntVector>& OutSpawns,
	                      TArray<FItemPlacement>& OutItems) override;
	// OutItems는 1주차에는 비워 반환한다 (아이템 배치는 Task 23).
};
