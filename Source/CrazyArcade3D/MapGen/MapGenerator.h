#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Gameplay/Item/ItemTypes.h" // FItemPlacement — 데이터 타입 참조까지만 허용 (액터·로직 참조 금지)
#include "MapGenerator.generated.h"

class UCA3DRuleSet;
struct FVoxelGrid;

UINTERFACE()
class CRAZYARCADE3D_API UMapGenerator : public UInterface
{
	GENERATED_BODY()
};

// 맵 생성기 인터페이스.
// 결정론 계약: 같은 Seed + 같은 Size + 같은 Rules ⇒ 항상 같은 Grid/Spawns/Items.
// 구현체 내부에서는 FRandomStream과 정수 연산만 허용 — float, FMath::Rand(),
// TMap 순회, 액터 이터레이션은 플랫폼/실행마다 순서가 달라 결정론을 깬다.
class CRAZYARCADE3D_API IMapGenerator
{
	GENERATED_BODY()

public:
	// 성공 시 true. 실패(검증 불통과 등) 시 false — 호출부가 폴백 처리.
	// Size 는 호출부가 명시한다 (Task 22 인원별 티어) — 생성기가 Rules->MapSize 를 직접 읽지
	// 않는 이유: 크기 결정은 서버(GameMode) 소관이고, 클라는 복제된 값 하나만 믿어야
	// 중간 접속자가 다른 크기의 맵을 만드는 사고가 없다 (AVoxelWorld::GridSize 주석).
	virtual bool Generate(uint32 Seed, const FIntVector& Size, const UCA3DRuleSet* Rules,
	                      FVoxelGrid& OutGrid,
	                      TArray<FIntVector>& OutSpawns,        // 8인 스폰 셀
	                      TArray<FItemPlacement>& OutItems) = 0;
};
