#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CA3DGameMode.generated.h"

class UCA3DRuleSet;
class AVoxelWorld;
class APlayerStart;
class ACA3DPlayerState;

// 서버 전용 매치 진행자 (Task 09). 클라는 이 클래스를 모른다.
// 룰셋 소유, 시드 결정, VoxelWorld 초기화, 스폰 배정, 승패 판정(Task 18).
// (서든데스 발동은 Task 24에서 확장.)
UCLASS()
class CRAZYARCADE3D_API ACA3DGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACA3DGameMode();

	// 매치 시작: 시드 결정 → GameState에 Rules·시작 시각 세팅 → VoxelWorld 초기화 → 스폰 셀 보관.
	virtual void BeginPlay() override;

	// 참가 등록: 접속 순서로 ColorIndex 배정 + 참가 인원·AliveCount 증가 (Task 18).
	virtual void PostLogin(APlayerController* NewPlayer) override;

	// 생성기가 반환한 스폰 셀에 플레이어를 순서대로 배정한다.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	// 사망 통지 수신 (Task 18) — UStatusComponent::ServerKill 의 서버 경로에서만 불린다.
	// 즉시 순위를 매기지 않고 다음 틱에 한 번에 해소한다 (동시 사망 묶기 — .cpp 주석 참조).
	void NotifyPlayerDeath(ACA3DPlayerState* DeadState);

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
	// 쌓인 사망 통지를 한 번에 해소한다 — 공동 등수 부여 → AliveCount 갱신 → 종료 판정.
	// NotifyPlayerDeath 가 예약한 다음 틱 타이머에서만 불린다.
	void ResolvePendingDeaths();

	// 레벨에서 탐색해 캐시 (BeginPlay).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// 생성기 출력 스폰 셀 — ChoosePlayerStart 가 순서대로 소비.
	TArray<FIntVector> SpawnCells;
	int32 NextSpawnIndex = 0;

	// 스폰 셀 인덱스별로 스폰한 임시 APlayerStart 캐시 — 리스폰/재접속 때 재사용해 액터 누적 방지.
	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> SpawnStartActors;

	// ─── 승패 판정 (Task 18, 서버 전용) ───

	// 이번 프레임에 들어온 사망 통지 버퍼. 다음 틱에 통째로 해소된다.
	UPROPERTY()
	TArray<TObjectPtr<ACA3DPlayerState>> PendingDeaths;

	// 해소가 이미 예약돼 있는지. SetTimerForNextTick 은 핸들을 돌려주지 않으므로
	// 중복 예약 방지는 이 플래그가 담당한다 (한 프레임에 N명이 죽어도 해소는 1회).
	bool bDeathResolveScheduled = false;

	// 이번 매치에 입장한 총 인원 (PostLogin 누적). MinPlayersForMatchEnd 와 비교해
	// 승패 판정을 켤지 정한다 — 1인 PIE 테스트에서 시작하자마자 끝나는 것을 막는 게이트.
	int32 MatchParticipantCount = 0;

	friend class FCA3DGameModeTest;    // 자동화 테스트가 Rules·고정 시드 주입과 스폰 배정 검증을 위한 접근
	friend class FCA3DPlayerStateTest; // 자동화 테스트가 참가 인원·사망 버퍼를 직접 구성하기 위한 접근
};
