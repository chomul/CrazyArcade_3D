#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CA3DRuleSet.generated.h"

// 매치 규칙·튜닝 값의 단일 출처. 로직 없음 — 순수 데이터.
// 인스턴스를 여러 개 만들어 룰셋 프리셋(기본전/스피드전 등)으로 쓴다.
// GameMode(서버)가 소유하되, GameState에 에셋 "포인터"를 복제해야 클라 프리뷰가 맞는다
// (UE는 에셋 참조를 경로로 복제하므로 포인터 복제 비용은 문자열 하나).
UCLASS(BlueprintType)
class CRAZYARCADE3D_API UCA3DRuleSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ── Bomb ──────────────────────────────────────────────

	// 설치 → 폭발까지 시간(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float BombFuseTime = 3.f;

	// 물줄기(폭발 판정) 잔존 시간(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float WaterLingerTime = 1.f;

	// 연쇄 폭발 1단계당 간격(초).
	UPROPERTY(EditAnywhere, Category="Bomb")
	float ChainStepDelay = 0.07f;

	// 폭탄 개수 스택 상한. (값 미확정)
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 MaxBombCountCap = 8;

	// 폭발 범위 스택 상한. (값 미확정)
	UPROPERTY(EditAnywhere, Category="Bomb")
	int32 MaxBombRangeCap = 6;

	// ── Life ──────────────────────────────────────────────

	// 물방울에 갇힌 상태 지속 시간(초). 3~5초 권장.
	UPROPERTY(EditAnywhere, Category="Life")
	float TrappedDuration = 4.f;

	// 갇힌 상태에서의 미세 이동 속도.
	UPROPERTY(EditAnywhere, Category="Life")
	float TrappedMoveSpeed = 60.f;

	// 스폰 직후 무적 시간(초). (유무 미확정)
	UPROPERTY(EditAnywhere, Category="Life")
	float SpawnInvulnTime = 0.f;

	// ── Map ───────────────────────────────────────────────

	// 바닥 블록 파괴 허용 여부. (미확정)
	UPROPERTY(EditAnywhere, Category="Map")
	bool bFloorDestructible = false;

	// 파괴 블록에 아이템이 배치될 확률(0~1).
	UPROPERTY(EditAnywhere, Category="Map")
	float ItemDropRate = 0.3f;

	// 그리드 크기 (X, Y, 층).
	UPROPERTY(EditAnywhere, Category="Map")
	FIntVector MapSize = FIntVector(21, 21, 4);

	// ── SuddenDeath ───────────────────────────────────────

	// 서든데스 발동 시각(초).
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float SuddenDeathStart = 150.f;

	// 블록 낙하 예고 시간(초).
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float DropWarningTime = 1.5f;

	// 외곽 셀 낙하 선택 가중치.
	UPROPERTY(EditAnywhere, Category="SuddenDeath")
	float OuterWeightBias = 2.f;
};
