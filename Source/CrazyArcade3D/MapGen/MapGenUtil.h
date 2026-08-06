#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Item/ItemTypes.h" // FItemPlacement — 데이터 타입 참조까지만 허용 (MapGenerator.h 관례)

struct FVoxelGrid;
class UCA3DRuleSet;

// 맵 생성기 공용 유틸 (Task 22).
//
// 아이템 배치 본체를 폴백·절차 생성기가 **같은 함수**로 공유한다 — 코드를 복붙하면
// 두 생성기의 아이템 규칙(드랍률·가중치·순회 순서)이 조용히 갈라지고, 그 차이는
// "폴백 맵에서만 아이템 분포가 이상하다" 같은 진단 최악의 버그로 나타난다.
// FMapValidator 와 같은 이유로 전부 static — 그리드를 읽기만 하는 순수 함수다.
struct CRAZYARCADE3D_API FMapGenUtil
{
	// Destructible 블록 안에 아이템을 숨긴다 (Task 23 규칙). OutItems 는 Reset 후 채운다.
	//
	// 크아식: 아이템은 바닥에 굴러다니지 않고 **Destructible 블록 "안"에 숨어 있다**.
	// 그 블록을 부순 사람이 먼저 보는 것이 보상 구조의 핵심이라 배치 대상은 Destructible 뿐이다.
	// 노출 스폰은 서버(UExplosionSubsystem)가 파괴 시점에 한다 — 생성기는 목록만 만든다
	// (MapGen 은 Gameplay 액터를 몰라야 한다 — 폴더 의존 규칙).
	//
	// 불변식 4 (결정론): 이 함수 안에는 float 연산·FMath::Rand()·TMap 순회가 하나도 없다.
	//   · 난수는 FRandomStream(Seed) 하나에서만 뽑는다 → 서버·클라가 같은 Seed 로 같은 배치를 만든다
	//   · 확률 판정은 정수 퍼센트 비교 (Stream.RandRange(0,99) < Percent)
	//   · 순회 순서는 Z→Y→X 고정 (그리드 평탄화 순서와 동일). 순서가 바뀌면 같은 Seed 라도
	//     난수 소비 순서가 달라져 배치가 통째로 달라진다 — 여기 루프 순서는 **계약**이다.
	static void PlaceItemsInDestructibles(const FVoxelGrid& Grid, const UCA3DRuleSet* Rules,
	                                      uint32 Seed, TArray<FItemPlacement>& OutItems);
};
