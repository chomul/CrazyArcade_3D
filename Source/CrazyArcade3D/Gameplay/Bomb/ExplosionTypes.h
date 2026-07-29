#pragma once

#include "CoreMinimal.h"

// 폭발 1회의 계산 결과. UExplosionSubsystem::Propagate(순수 함수 — 불변식 2)의 출력.
//
// USTRUCT 를 붙이지 않은 이유: 셀 좌표 TArray 3개뿐이라 리플렉션·리플리케이션·GC 참조가
// 전혀 필요 없다 (FVoxelGrid 와 같은 판단). 값 복사·스택 생성이 자유로운 순수 데이터.
// 컨테이너는 TArray 만 — 결정론 유지 (불변식 4 준용).
//
// 보류 결정 확정 (Task 16): ChainedBombs 멤버는 **추가하지 않는다.**
// 셀→ABomb 해석은 UExplosionSubsystem::ProcessChainStep 이 서버 레지스트리로 즉석 수행해
// 곧장 다음 단계 PendingChain 에 넣는다 — 중간 보관이 필요 없고, 이 구조체는 리플렉션·GC
// 참조 없는 순수 셀 데이터로 유지된다 (액터 포인터를 실으면 USTRUCT 승격이 강제된다).
struct FExplosionResult
{
	TArray<FIntVector> WaterCells;    // 물줄기가 채우는 칸 — 피격·아이템 소멸 판정 대상 (원점 포함)
	TArray<FIntVector> BrokenCells;   // 파괴되는 블록 칸
	TArray<FIntVector> ChainedCells;  // 물줄기에 닿은 다른 폭탄의 칸 (순수 출력 — 셀 좌표만)
};
