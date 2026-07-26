#pragma once

#include "CoreMinimal.h"
#include "VoxelTypes.generated.h"

// 블록 1칸의 종류. uint8 1바이트로 그리드에 저장된다.
UENUM()
enum class EBlockType : uint8
{
	Empty        = 0,  // 빈 칸 — 이동·폭발 통과
	Floor        = 1,  // 바닥 (파괴 여부는 룰셋 bFloorDestructible)
	Destructible = 2,  // 파괴 가능 블록 — 폭발에 부서지고 전파를 멈춤
	Immortal     = 3,  // 불멸 블록 — 폭발을 막음
};
