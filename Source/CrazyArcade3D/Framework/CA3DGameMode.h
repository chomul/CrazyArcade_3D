#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CA3DGameMode.generated.h"

class UCA3DRuleSet;
class AVoxelWorld;
class APlayerStart;

// 서버 전용 매치 진행자 (Task 09). 클라는 이 클래스를 모른다.
// 룰셋 소유, 시드 결정, VoxelWorld 초기화, 스폰 배정.
// (승패 판정·AliveCount 갱신은 Task 18, 서든데스 발동은 Task 24에서 확장.)
UCLASS()
class CRAZYARCADE3D_API ACA3DGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACA3DGameMode();

	// 매치 시작: 시드 결정 → GameState에 Rules·시작 시각 세팅 → VoxelWorld 초기화 → 스폰 셀 보관.
	virtual void BeginPlay() override;

	// 생성기가 반환한 스폰 셀에 플레이어를 순서대로 배정한다.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	// 룰셋 프리셋 — BP 서브클래스(BP_CA3DGameMode)에서 DA_Rules_Default 지정 (BP에 로직 금지).
	UPROPERTY(EditDefaultsOnly, Category="CA3D")
	TObjectPtr<UCA3DRuleSet> Rules;

	// 고정 시드 모드 — 버그 재현용 (GDD 4.2 안전장치 2). 켜면 매번 FixedSeed 로 생성한다.
	UPROPERTY(EditDefaultsOnly, Category="CA3D|Seed")
	bool bUseFixedSeed = false;

	UPROPERTY(EditDefaultsOnly, Category="CA3D|Seed", meta=(EditCondition="bUseFixedSeed", ClampMin="0"))
	int32 FixedSeed = 1;

private:
	// 레벨에서 탐색해 캐시 (BeginPlay).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// 생성기 출력 스폰 셀 — ChoosePlayerStart 가 순서대로 소비.
	TArray<FIntVector> SpawnCells;
	int32 NextSpawnIndex = 0;

	// 스폰 셀 인덱스별로 스폰한 임시 APlayerStart 캐시 — 리스폰/재접속 때 재사용해 액터 누적 방지.
	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> SpawnStartActors;

	friend class FCA3DGameModeTest; // 자동화 테스트가 Rules·고정 시드 주입과 스폰 배정 검증을 위한 접근
};
