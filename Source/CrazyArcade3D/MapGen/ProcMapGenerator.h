#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MapGen/MapGenerator.h"
#include "ProcMapGenerator.generated.h"

// 시드 기반 절차 맵 생성기 (Task 22) — 생성 → 검증(Task 21 FMapValidator) → 리롤 루프.
//
// 결정론 계약(불변식 4): 같은 Seed + Size + Rules ⇒ 비트 단위로 같은 출력.
// 내부에서 허용되는 것: FRandomStream(파생 시드), 정수 연산, TArray, 고정 순서 루프.
// 금지: float 연산, FMath::Rand/FRand, TMap/TSet 순회, 액터 이터레이션.
// 리눅스 데디 ↔ 윈도우 클라에서 시드 재현이 깨지면 진단이 매우 어렵다.
// 리롤도 결정론이다 — 파생 시드가 고정식(Seed * 7919 + attempt)이라 같은 입력이면
// 같은 순서로 같은 재시도를 한다.
//
// 레이아웃 (2026-08-06 사용자 확정 — 산·협곡까지, Z 6층):
//  - z=0 전체 Floor + 외곽 Immortal 벽 (구조물 꼭대기에서도 1칸 점프로 못 넘는 높이).
//  - 내부에 웨딩케이크식 계단 구조물(산) — 모든 층이 1칸 점프로 도달 (GDD 2.1, 검증기 ①).
//    구조물끼리 겹침 허용(자연스러운 능선), 구조물 사이 틈이 협곡 역할을 한다.
//  - 평지는 원작 크아식 기둥 격자 + 파괴 블록 스트림 롤, 구조물 꼭대기에도 같은 확률로
//    파괴 블록 스캐터 (아이템이 고지대에도 나온다).
//  - 스폰 8개는 항상 비워 두는 외곽 스폰 링(인덱스 1/Size-2) 위.
//  - 아이템은 폴백과 같은 본체(FMapGenUtil) — 두 생성기의 규칙이 갈라질 수 없다.
//
// Generate 가 false 를 반환하면(Rules 없음·크기 미달·리롤 상한 초과) 호출부(AVoxelWorld)가
// FallbackMapGenerator 로 폴백한다 (GDD 4.2 안전장치 3 — 미완이어도 게임은 돈다).
UCLASS()
class CRAZYARCADE3D_API UProcMapGenerator : public UObject, public IMapGenerator
{
	GENERATED_BODY()

public:
	virtual bool Generate(uint32 Seed, const FIntVector& Size, const UCA3DRuleSet* Rules,
	                      FVoxelGrid& OutGrid,
	                      TArray<FIntVector>& OutSpawns,
	                      TArray<FItemPlacement>& OutItems) override;

	// 진단용 — 마지막 Generate 가 몇 번째 시도에서 끝났는지 (통과 시 1 이상, 실패 시 상한).
	// 테스트가 리롤률을 집계한다 (Task 22 검증 원칙 — 리롤률이 지나치면 생성 규칙부터 보정).
	int32 LastAttemptCount = 0;

private:
	// 1회 생성 (검증 없음). 단계 본체는 .cpp 익명 네임스페이스의 Pmg* 헬퍼 —
	// 전부 FRandomStream& 을 받는 고정 순서 루프다 (단계 순서 = 난수 소비 순서 = 계약).
	// 스폰 배치가 자리를 못 찾으면 false — 이 attempt 는 실패, 리롤 루프가 파생 시드로 재시도.
	bool GenerateOnce(uint32 DerivedSeed, const FIntVector& Size, const UCA3DRuleSet* Rules,
	                  int32 SpawnMinManhattan,
	                  FVoxelGrid& OutGrid, TArray<FIntVector>& OutSpawns,
	                  TArray<FItemPlacement>& OutItems) const;
};
