#pragma once

#include "CoreMinimal.h"
#include "ItemTypes.generated.h"

// 아이템 종류 (GDD 3장 MVP 5종).
UENUM()
enum class EItemType : uint8
{
	Balloon,  // 폭탄 개수 +1
	Potion,   // 폭발 범위 +1
	Roller,   // 이동속도 증가
	Needle,   // 갇힘 탈출 (1회 소모품, 낮은 드랍률)
	Kick,     // 폭탄 차기
};

// 맵 생성 시점에 결정되는 아이템 배치 1건.
USTRUCT()
struct FItemPlacement
{
	GENERATED_BODY()

	FIntVector Cell = FIntVector::ZeroValue; // 아이템이 숨겨진 셀 (Destructible 블록 안)
	EItemType  Type = EItemType::Balloon;
};
