#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/TimerHandle.h"
#include "CA3DGameMode.generated.h"

class UCA3DRuleSet;
class AVoxelWorld;
class APlayerStart;
class ACA3DPlayerState;
class AController;

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

	// 액터 BeginPlay 일괄 실행(Super) **직후** 보류된 폰 스폰을 해소한다.
	// BeginPlay 안이 아니라 여기여야 하는 이유는 .cpp 주석 참조.
	virtual void StartPlay() override;

	// 참가 등록: 접속 순서로 ColorIndex 배정 + 참가 인원·AliveCount 증가 (Task 18).
	virtual void PostLogin(APlayerController* NewPlayer) override;

	// 중도 이탈 (2026-08-10 사용자 확정): **나간 사람은 그 자리에서 사망 처리해 순위를 부여하고,
	// 결과 화면에 "탈주" 로 표시한다.** 참가 인원에서 빼지 않는다 — 그 사람은 매치에 참가했고
	// 자리를 차지했다. 실제 처리는 HandleParticipantLeft 가 한다 (.cpp 주석).
	virtual void Logout(AController* Exiting) override;

	// 폰 스폰 시점의 게이트 — 지형이 판가름 나기 전에는 스폰하지 않고 보류한다 (아래 .cpp 주석).
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	// 생성기가 반환한 스폰 셀에 플레이어를 순서대로 배정한다.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	// 사망 통지 수신 (Task 18) — UStatusComponent::ServerKill 의 서버 경로에서만 불린다.
	// 즉시 순위를 매기지 않고 다음 틱에 한 번에 해소한다 (동시 사망 묶기 — .cpp 주석 참조).
	void NotifyPlayerDeath(ACA3DPlayerState* DeadState);

protected:
	// ─── 스폰 위치의 단일 출처를 지키는 두 오버라이드 ───
	// 엔진은 "로그인 때 고른 자리(StartSpot)를 기억했다가 나중에 그대로 쓴다" 는 전제로 만들어져
	// 있다. 이 게임은 로그인 시점에 맵이 없을 수 있어 그 전제가 성립하지 않는다 (.cpp 주석).

	// 로그인 단계에서 StartSpot 을 미리 정하지 않는다.
	virtual bool UpdatePlayerStartSpot(AController* Player, const FString& Portal, FString& OutErrorMessage) override;

	// 굳어 있는 StartSpot 을 재사용하지 않는다 — 스폰 위치는 **항상** ChoosePlayerStart 가 정한다.
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;

	// 룰셋 프리셋 — BP 서브클래스(BP_CA3DGameMode)에서 DA_Rules_Default 지정 (BP에 로직 금지).
	UPROPERTY(EditDefaultsOnly, Category="CA3D")
	TObjectPtr<UCA3DRuleSet> Rules;

	// 고정 시드 모드 — 버그 재현용 (GDD 4.2 안전장치 2). 켜면 매번 FixedSeed 로 생성한다.
	UPROPERTY(EditDefaultsOnly, Category="CA3D|Seed")
	bool bUseFixedSeed = false;

	UPROPERTY(EditDefaultsOnly, Category="CA3D|Seed", meta=(EditCondition="bUseFixedSeed", ClampMin="0"))
	int32 FixedSeed = 1;

private:
	// 참가 등록의 **단일 경로** (Task 20) — 사람(PostLogin)과 봇(SpawnFillBots)이 둘 다 여기를
	// 통과한다. ColorIndex 배정·MatchParticipantCount·AliveCount 가 두 벌로 갈라지면
	// "봇이 낀 매치에서만 승패 판정이 어긋나는" 진단 최악의 버그가 된다.
	void RegisterParticipant(AController* NewController);

	// 이탈 처리의 **단일 경로** — Logout 이 Super 를 부르기 전에 여기를 통과한다.
	// 등록(RegisterParticipant)의 반대편이지만 **대칭이 아니다**: 참가 인원은 되돌리지 않고
	// 생존 수만 되돌린다. 그 비대칭의 근거는 .cpp 주석에 있다.
	void HandleParticipantLeft(AController* Exiting);

	// 룰셋(bFillWithBots·BotFillTargetPlayers) 또는 콘솔 변수 `ca3d.BotFill` 이 요구하는
	// 인원까지 ABotController 를 스폰해 채운다. BeginPlay 가 BotFillDelaySeconds 후로 예약.
	void SpawnFillBots();

	// 봇 충원 목표 인원의 **단일 공식** (Task 22) — 콘솔 변수 `ca3d.BotFill`(≥0)이 룰셋
	// (bFillWithBots·BotFillTargetPlayers)을 덮어쓴다. SpawnFillBots(실제 충원)와
	// BeginPlay(맵 크기 티어 판정)가 둘 다 여기를 통과한다 — 공식이 두 벌로 갈라지면
	// "봇까지 8명인데 소형 맵" 같은 티어·인원 어긋남이 생긴다.
	// bOutFromCVar: 콘솔 변수가 덮어썼는지 (로그 표기용).
	int32 GetBotFillTargetPlayers(bool& bOutFromCVar) const;

	// 쌓인 사망 통지를 한 번에 해소한다 — 공동 등수 부여 → AliveCount 갱신 → 종료 판정.
	// NotifyPlayerDeath 가 예약한 다음 틱 타이머에서만 불린다.
	void ResolvePendingDeaths();

	// ─── 서든데스 (Task 24, 서버 전용) ───
	// GameState 플래그를 **여기서만** 쓴다 — 낙하 스케줄러(USuddenDeathSubsystem)가 직접 쓰면
	// GameState 갱신 주체가 둘이 되어 "구동 중인데 플래그는 false" 같은 모순이 표현 가능해진다.

	// BeginPlay 가 Rules->SuddenDeathStart 초 뒤로 예약. 플래그 세팅 + 낙하 루프 시작.
	void StartSuddenDeath();

	// 매치 종료 지점에서 호출 — 플래그 해제 + 예고 중인 웨이브까지 취소.
	void StopSuddenDeath();

	// 레벨에서 탐색해 캐시 (BeginPlay).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// 생성기 출력 스폰 셀 — ChoosePlayerStart 가 순서대로 소비.
	TArray<FIntVector> SpawnCells;
	int32 NextSpawnIndex = 0;

	// 스폰 셀 인덱스별로 스폰한 임시 APlayerStart 캐시 — 리스폰/재접속 때 재사용해 액터 누적 방지.
	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> SpawnStartActors;

	// ─── 스폰 게이트 (PostLogin ↔ BeginPlay 순서 무관화) ───

	// BeginPlay 가 "지형이 됐는지 안 됐는지"를 판가름 냈는가. 성공·실패 **둘 다** true 다 —
	// 실패(레벨에 AVoxelWorld 없음)는 진짜 비정상이라 엔진 기본 폴백으로 내보내야 하고,
	// 여기서 계속 붙잡아 두면 폰이 영영 안 생긴다.
	bool bMatchStartResolved = false;

	// 지형이 판가름 나기 전에 입장해 폰 스폰을 보류한 컨트롤러들. FlushPendingSpawns 가 비운다.
	UPROPERTY()
	TArray<TObjectPtr<APlayerController>> PendingSpawnControllers;

	// 보류된 컨트롤러를 **정확히 한 번씩** 스폰한다.
	void FlushPendingSpawns();

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

	// ─── 봇 채우기 (Task 20, 서버 전용) ───

	// 봇 표시 이름(`Bot 1`, `Bot 2` …)의 번호. 참가 순번(ColorIndex)과 달리 봇끼리만 세므로
	// 사람이 섞여도 이름이 건너뛰지 않는다.
	int32 BotNameCounter = 0;

	FTimerHandle BotFillTimer;

	// ─── 서든데스 (Task 24) ───

	FTimerHandle SuddenDeathTimer;

	friend class FCA3DGameModeTest;    // 자동화 테스트가 Rules·고정 시드 주입과 스폰 배정 검증을 위한 접근
	friend class FCA3DSpawnGateTest;   // 자동화 테스트가 "지형 준비 전 입장"을 헤드리스로 재현하기 위한 접근
	friend class FCA3DPlayerStateTest; // 자동화 테스트가 참가 인원·사망 버퍼를 직접 구성하기 위한 접근
	friend class FBotControllerTest;   // 자동화 테스트가 봇 채우기·참가 등록을 직접 호출하기 위한 접근 (Task 20)
	friend class FSuddenDeathTest;     // 자동화 테스트가 서든데스 시작·정지 배선을 직접 호출하기 위한 접근 (Task 24)
	friend class FMatchLeaveTest;      // 자동화 테스트가 중도 이탈 매치를 직접 구성하기 위한 접근 (2026-08-10)
};
