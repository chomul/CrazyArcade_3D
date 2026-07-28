#pragma once

#include "CoreMinimal.h"

// 폭발 1회의 계산 결과. UExplosionSubsystem::Propagate(순수 함수 — 불변식 2)의 출력.
//
// USTRUCT 를 붙이지 않은 이유: 셀 좌표 TArray 3개뿐이라 리플렉션·리플리케이션·GC 참조가
// 전혀 필요 없다 (FVoxelGrid 와 같은 판단). 값 복사·스택 생성이 자유로운 순수 데이터.
// 컨테이너는 TArray 만 — 결정론 유지 (불변식 4 준용).
//
// TODO(Task 16): 서버 전용 ChainedBombs(셀→ABomb 해석 결과)는 ABomb 이 생기는 Task 16 에서
// 추가한다. TObjectPtr 멤버가 필요해지면 그때 UPROPERTY 요구 여부(USTRUCT 승격 vs
// TWeakObjectPtr 유지)를 함께 결정한다 — 지금 미리 넣으면 존재하지 않는 타입 참조로
// 컴파일이 깨진다.
struct FExplosionResult
{
	TArray<FIntVector> WaterCells;    // 물줄기가 채우는 칸 — 피격·아이템 소멸 판정 대상 (원점 포함)
	TArray<FIntVector> BrokenCells;   // 파괴되는 블록 칸
	TArray<FIntVector> ChainedCells;  // 물줄기에 닿은 다른 폭탄의 칸 (순수 출력 — 셀 좌표만)
};
