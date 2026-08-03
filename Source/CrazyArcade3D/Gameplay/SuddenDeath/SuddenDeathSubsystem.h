#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Info.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/PooledActor.h"       // 인터페이스 자체가 가벼워 헤더 include 허용 (WaterSegment.h 관례)
#include "Engine/TimerHandle.h"
#include "Math/RandomStream.h"
#include "SuddenDeathSubsystem.generated.h"

struct FVoxelGrid;
class AVoxelWorld;
class UStaticMeshComponent;

// ─────────────────────────────────────────────────────────────────────────────
// 이 파일에 세 클래스가 함께 있는 이유: 셋 다 "서든데스 낙하" 하나를 위해서만 존재하고
// 서로 외에는 아무도 쓰지 않는다 (마커는 릴레이만, 릴레이는 서브시스템만 스폰한다).
//   ASuddenDeathDropMarker — 클라 예고 마커 (풀링 대상, 시각 전용)
//   ASuddenDeathRelay      — MulticastWarnDrop 의 소유 액터
//   USuddenDeathSubsystem  — 서버 낙하 스케줄러 (이 Task 의 본체)
// ─────────────────────────────────────────────────────────────────────────────

// 낙하 예고 마커 1개 (Task 24) — 클라 시각 전용, 풀링 대상 (설계서 2.8).
// "여기에 곧 떨어진다" 를 DropWarningTime 동안 보여 주고 스스로 풀에 반납한다.
// 판정은 100% 서버가 하고 이 액터는 아무것도 결정하지 않는다 — AWaterSegment 와 같은 구조.
// 메시는 BP 서브클래스(BP_DropMarker)에서 지정 — 미지정이어도 동작한다 (로그 경고 1회).
UCLASS()
class CRAZYARCADE3D_API ASuddenDeathDropMarker : public AActor, public IPooledActor
{
	GENERATED_BODY()

public:
	ASuddenDeathDropMarker();

	// 획득 직후 호출 — Seconds(= 룰셋 DropWarningTime) 후 스스로 풀에 반납한다.
	// 클라 로컬 타이머지만 시각 전용이라 불변식 3과 무관 (낙하 타이머는 서버 서브시스템 소유).
	void StartWarning(float Seconds);

	// ─── IPooledActor ───
	virtual void OnAcquiredFromPool() override;
	virtual void OnReleasedToPool() override;   // ⚠️ 타이머 정지 필수 (풀 오염 방지)

protected:
	// BP 서브클래스에서 메시 에셋만 지정 — BP 로직 금지.
	UPROPERTY(VisibleAnywhere, Category="FX")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

private:
	void ReleaseSelf();

	FTimerHandle WarnTimer;

	// 메시 미지정 경고는 인스턴스당 1회만 — 풀 재사용마다 스팸 방지.
	bool bWarnedMissingMesh = false;
};

// 낙하 예고 Multicast 의 소유 액터 (Task 24) — 서버 USuddenDeathSubsystem 이 lazy 스폰.
//
// Multicast 는 액터에만 가능한데 서브시스템은 액터가 아니다 (AExplosionFXRelay.h 와 같은 사정).
// 그래서 명세의 `USuddenDeathSubsystem::MulticastWarnDrop` 은 이 릴레이로 옮겼다.
// bAlwaysRelevant: 컬링으로 예고를 놓치면 **피할 수 없는 낙하**가 되어 규칙이 불공정해진다.
UCLASS()
class CRAZYARCADE3D_API ASuddenDeathRelay : public AInfo
{
	GENERATED_BODY()

public:
	ASuddenDeathRelay();

	// 서버 → 전 클라: 이번 웨이브의 낙하 예고 셀 목록과 남은 시간.
	// 웨이브 단위로 한 번에 보낸다 (셀 하나씩 보내면 DropsPerWave 만큼 RPC 가 늘어난다).
	// 클라는 풀(Task 14)에서 ASuddenDeathDropMarker 를 꺼내 배치하고, Delay 후 스스로 반납한다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWarnDrop(const TArray<FIntVector>& Cells, float Delay);

private:
	// 셀 → 월드 변환용 (CellSize 의 유일한 출처). lazy 탐색·캐시.
	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	AVoxelWorld* ResolveVoxelWorld();

	// 마커 클래스 미지정 경고는 1회만 — 낙하마다 찍으면 로그가 초당 한 줄씩 쌓인다.
	bool bWarnedMissingMarkerClass = false;
};

// 예고 중인 웨이브 하나 (서버 전용). 예고 시점에 확정한 셀을 그대로 들고 있다가
// 타이머 만료 시 **그 값으로** 터뜨린다 — 만료 시점에 다시 추첨하면 마커와 실제 낙하 지점이
// 어긋나고, 그러면 "마커를 보고 피한다" 는 규칙 자체가 성립하지 않는다 (체크리스트 24).
//
// 웨이브를 배열로 들고 있는 이유: DropInterval(1초) < DropWarningTime(1.5초) 이면
// 예고 중인 웨이브가 동시에 여러 개가 된다. 핸들·셀을 하나만 두면 뒤 웨이브가 앞 웨이브를
// 덮어써 앞 웨이브가 영영 떨어지지 않는다.
struct FSuddenDeathWave
{
	int32 Id = 0;
	TArray<FIntVector> Cells;
	FTimerHandle Timer;
};

// 서든데스 낙하 스케줄러 (Task 24, GDD 2.4).
// 하늘에서 물폭탄이 랜덤 낙하 → 블록·바닥 파괴 → 구멍 → 낙사로 매치가 자연 축소된다.
//
// 모든 결정은 서버다 — 클라는 MulticastWarnDrop 으로 "어디에 언제" 만 받는다
// (권한 매트릭스 마지막 행). 낙하 폭발의 적용은 폭탄과 **같은 함수**를 통과한다:
// UExplosionSubsystem::ServerApplyExplosionAt — 파괴·FX·갇힘·아이템이 폭탄과 동일해야 하고,
// 여기서 따로 구현하면 그 동일성이 조용히 깨진다.
UCLASS()
class CRAZYARCADE3D_API USuddenDeathSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 서버: GameMode 가 매치 시각 SuddenDeathStart 에 호출. 낙하 루프 시작.
	// (GameState 의 bSuddenDeathActive 는 GameMode 가 쓴다 — 갱신 주체 단일화, GameState.h 주석)
	void ServerStart();

	// 서버: 매치 종료 시 정리. 예고 중이던 웨이브까지 전부 취소한다 —
	// 결과 화면이 떠 있는데 블록이 계속 떨어지면 안 된다.
	void ServerStop();

	bool IsRunning() const { return bRunning; }

	// ─── 순수 함수: 낙하 지점 선정 ───
	//
	// 그리드와 난수 스트림만 받아 셀을 돌려준다 — 월드·액터·룰셋 접근 0.
	// 그래서 300회 낙하 시뮬레이션(외곽 가중 검증)을 PIE 없이 돌릴 수 있다
	// (UExplosionSubsystem::Propagate 를 프리뷰·봇·테스트가 공유하는 것과 같은 이유).
	//
	// 선정 규칙:
	//   ① XY 기둥을 외곽 가중 랜덤으로 고른다. 가중치는 중심 거리 선형 보간 —
	//      중심 100, 최외곽 OuterWeightPercent. **전부 정수 연산**이라 같은 시드면 같은 순서다.
	//   ② 그 기둥에서 가장 높은 solid 블록을 찾아 **그 위 칸**을 폭발 원점으로 삼는다.
	//   ③ 기둥이 이미 전부 Empty(= 뚫린 구멍)면 낭비이므로 재추첨한다. MaxAttempts 회
	//      실패하면 false — 호출자가 그 웨이브를 건너뛴다 (맵 전체가 뚫린 말기 상황).
	//
	// ⚠️ Stream 은 전진한다(난수 소비) — 순수 함수지만 "같은 스트림 상태 ⇒ 같은 출력" 의 의미다.
	static bool PickDropCell(const FVoxelGrid& Grid, FRandomStream& Stream,
	                         int32 OuterWeightPercent, int32 MaxAttempts, FIntVector& OutCell);

	// 룰셋 float(OuterWeightBias) → 선정 함수가 쓰는 정수 퍼센트. 1.0 ⇒ 100.
	// 변환을 한 곳에 모아 두어야 테스트와 실행이 같은 값을 쓴다.
	static int32 ToOuterWeightPercent(float OuterWeightBias);

	// 레벨 전환·월드 정리 안전망 — 타이머가 남아 죽은 월드를 건드리지 않게 한다.
	virtual void Deinitialize() override;

private:
	// 서버: DropInterval 마다 1회 — 셀 DropsPerWave 개 선정 → 전 클라 예고 →
	// DropWarningTime 후 ExecuteWave 예약. (명세의 ProcessDrop)
	void ProcessDrop();

	// 서버: 예고 시간 만료 — 저장해 둔 셀 그대로 폭발시킨다.
	void ExecuteWave(int32 WaveId);

	// 예고 중인 웨이브들의 타이머를 전부 해제하고 비운다 (ServerStop·Deinitialize 공용).
	void ClearPendingWaves();

	AVoxelWorld* ResolveVoxelWorld();
	ASuddenDeathRelay* ResolveRelay(); // 서버: lazy 스폰·캐시 (Multicast 소유 액터)

	FTimerHandle DropTimer;

	// 서버 전용 — 낙하 지점 롤. 클라 재현 불필요(결과만 Multicast 로 전송)하므로
	// 불변식 4(시드 재현)의 대상이 아니다. 그래도 선정 함수 자체는 정수 연산으로 두어
	// 테스트에서 "같은 시드 ⇒ 같은 순서" 를 검증할 수 있게 했다.
	FRandomStream Stream;

	// 예고 중인 웨이브들 (UObject 포인터가 없으므로 UPROPERTY 불필요).
	TArray<FSuddenDeathWave> PendingWaves;

	int32 NextWaveId = 1;

	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	UPROPERTY()
	TObjectPtr<ASuddenDeathRelay> Relay;

	bool bRunning = false;

	// 선정 실패(맵이 전부 뚫림) 경고는 1회만 — 매 웨이브 찍으면 초당 한 줄이 된다.
	bool bWarnedNoCandidate = false;

	friend class FSuddenDeathTest; // 자동화 테스트가 스케줄러 상태·예고 웨이브를 검증하기 위한 접근
};
