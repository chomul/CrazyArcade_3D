#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Math/RandomStream.h"
#include "BotController.generated.h"

class ACA3DCharacter;
class AVoxelWorld;
class UCA3DRuleSet;
class UStatusComponent;
struct FVoxelGrid;

// 봇 FSM 상태 (Task 20 · 구조 결정 12 — BT 미사용). 전이 우선순위는 **Evade > Attack > Wander**:
// 위험은 다른 어떤 판단보다 먼저다. 발밑이 물줄기 범위면 상대가 코앞이어도 먼저 빠져나간다.
UENUM()
enum class EBotState : uint8
{
	Wander,     // 도달 가능한 셀 하나를 골라 이동 (계단·점프 포함)
	Attack,     // 도달 가능한 상대 쪽으로 접근 — 놓을 만한 자리에 오면 설치 후 Evade
	Evade,      // 발밑 셀이 위험 → 위험하지 않은 가장 가까운 셀로 탈출
};

// 서버 전용 FSM 봇 (Task 20).
//
// 이 클래스의 존재 이유는 "봇을 만드는 것"이 아니라 **인원을 채워 한 판을 완주시키는 것**이다.
// 그래서 봇은 플레이어와 완전히 같은 경로만 탄다 — 이동은 ACA3DCharacter::Move,
// 점프는 DoJump, 설치는 TryPlaceBombPredicted. 그리드 직접 수정·ServerKill 직접 호출 같은
// 봇 전용 치트 경로는 하나도 없다. 그래야 봇으로 돌린 한 판이 사망 복제·최후 1인 종료를
// 실제로 검증한 것이 된다 (봇 전용 경로가 하나라도 있으면 그만큼은 검증되지 않은 채 남는다).
//
// 위험 판정·설치 판단은 전부 UExplosionSubsystem::Propagate(static 순수 함수 — 불변식 2)를
// 호출한다. 봇이 자기만의 전파 계산을 가지면 "표시·실폭발·봇 판단" 셋이 갈라진다.
//
// 난이도·성격 튜닝은 이 Task 범위가 아니다 (GDD 8장 "더미 봇") — 돌아다니고, 위험을 피하고,
// 가끔 폭탄을 놓는 것까지가 목표다.
UCLASS()
class CRAZYARCADE3D_API ABotController : public AAIController
{
	GENERATED_BODY()

public:
	ABotController();

	// 서버 가드 + FSM 평가. **매 틱 BFS 를 돌리지 않는다** — 룰셋 BotReplanInterval 주기로만
	// 재계획하고 그 사이에는 캐시된 경로를 따라간다 (봇 8대 × 매 틱 BFS 는 서버 스파이크가 된다).
	virtual void Tick(float DeltaSeconds) override;

private:
	// ─── FSM 상태 ───

	EBotState State = EBotState::Wander;

	// 현재 따라가는 경로 (시작 셀 포함). PathIndex 가 "다음 웨이포인트".
	// 매 틱 갱신하지 않고 재계획 때만 통째로 교체한다.
	TArray<FIntVector> PathCells;
	int32 PathIndex = 0;

	float TimeSinceReplan = 0.f;
	float TimeSinceBombAttempt = 0.f;

	// 직전 재계획이 빈손이었나 (갈 곳이 없거나 탈출로가 막혔다).
	// 이 플래그가 없으면 "경로가 비었다 → 재계획 → 또 실패 → 경로가 비었다"가 매 틱 돌아
	// **가장 답답한 상황(전원이 갇힘)에서 서버가 가장 비싸진다**. 실패한 뒤에는 재계획 주기를
	// 기다린다 — 그 사이 다른 폭발이 벽을 뚫어 길이 생겼을 수 있다.
	bool bPlanFailed = false;

	// 배회 목적지 추첨용 — FMath::Rand() 가 아니라 스트림을 쓰는 이유는 봇마다 독립된
	// 난수열을 갖게 하기 위함이다 (전역 Rand 를 쓰면 봇 수·틱 순서가 서로의 행동을 바꾼다).
	FRandomStream RandomStream;
	bool bRandomSeeded = false;

	// ─── 판정 (전부 Propagate 재사용 — 봇 전용 계산 없음) ───

	// 위험 판정: 활성 폭탄 전부에 대해 Propagate 를 호출해 WaterCells 에 Cell 이 들어가는지 본다.
	// 프리뷰 데칼·실폭발과 **같은 함수**라 봇이 "안 피해지는 폭발"에 속을 수 없다.
	bool IsCellDangerous(const FIntVector& Cell) const;

	// 설치 판단: 여기 놓으면 ① 파괴 가능한 블록이나 상대가 범위에 들어오고
	// ② **자기 탈출로가 남는가**. ②를 빠뜨리면 봇이 자폭만 반복한다 (놓을 이유는 늘 있으므로).
	bool ShouldPlaceBombAt(const FIntVector& Cell) const;

	// 그리드 BFS. 인접은 ±X,±Y 이고 **높이 차 1칸까지만** 이동 가능 (점프 높이 1칸 확정 —
	// 2칸 차이는 못 오른다). NavMesh 미사용 — 지형이 실시간으로 무너지는 게임이라
	// 런타임 재생성 비용이 그리드 BFS 보다 크다.
	// 반환 배열은 From 을 포함하고 To 로 끝난다. 도달 불가면 빈 배열.
	TArray<FIntVector> FindPath(const FIntVector& From, const FIntVector& To) const;

	// ─── 내부 헬퍼 ───

	// 활성 폭탄 전부의 물줄기 셀 합집합. IsCellDangerous 와 BFS 가 **같은 이 함수**를 쓴다 —
	// 셀마다 Propagate 를 다시 돌리면 (a) 비용이 셀 수만큼 곱해지고 (b) 두 경로가 갈라질 여지가 생긴다.
	void GatherDangerCells(TArray<FIntVector>& OutCells) const;

	// 살아 있는 다른 캐릭터들의 발밑 셀 — 좌표 사전순 정렬.
	// 액터 이터레이션 순서는 실행마다 다를 수 있어 정렬로 고정한다 (불변식 4 준용).
	void GatherEnemyFootCells(TArray<FIntVector>& OutCells) const;

	// 그 칸에 서 있을 수 있는가: 몸이 들어갈 빈 칸 + 머리 공간 + 발판.
	// 머리 공간을 보는 이유는 캡슐 높이(176)가 셀(100)보다 커서 1칸 높이 틈에는 못 들어가기 때문 —
	// 안 보면 봇이 영원히 못 들어가는 칸을 목적지로 잡고 벽에 붙어 비빈다.
	bool IsStandable(const FVoxelGrid& Grid, const FIntVector& Cell) const;

	// BFS 본체 (경로 탐색·탈출로 확인·배회 후보 수집이 전부 이 하나를 쓴다).
	// 방문 집합은 조회 전용 TMap 이고 **확장 순서는 고정 방향 배열**이라 결정론적이다.
	// OutVisited 를 주면 방문한 셀을 BFS 순서 그대로 채운다 (배회 목적지 후보).
	bool RunBFS(
		const FIntVector& Start,
		TFunctionRef<bool(const FIntVector&)> IsPassable,
		TFunctionRef<bool(const FIntVector&)> IsGoal,
		TArray<FIntVector>& OutPath,
		TArray<FIntVector>* OutVisited = nullptr) const;

	// 재계획 — 상태 전이와 경로 교체를 한 곳에서 한다 (전이 조건이 흩어지면 우선순위가 깨진다).
	void Replan(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells);

	// 위험하지 않은 가장 가까운 셀로의 경로를 잡는다 (Evade).
	void PlanEscape(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells);

	// 도달 가능한 안전 셀 중 하나를 추첨해 목적지로 (Wander).
	void PlanWander(const FIntVector& FootCell, const TArray<FIntVector>& DangerCells);

	// 캐시된 경로의 다음 웨이포인트로 이동 입력을 만든다 — 플레이어와 같은 Move/DoJump.
	void FollowPath(const FIntVector& FootCell);

	// lazy 탐색·캐시 (레벨에 1개 배치가 계약 — ExplosionSubsystem::ResolveVoxelWorld 와 동일 관례).
	AVoxelWorld* ResolveVoxelWorld() const;

	// 룰셋 해석 — GameState 복제 포인터 → CDO 폴백 (StatusComponent::ResolveRules 와 동일 관례).
	const UCA3DRuleSet* ResolveRules() const;

	// 조종 대상 — GetPawn() 캐스트의 단일 지점.
	ACA3DCharacter* GetBotCharacter() const;

	// 판정·탐색이 전부 이 그리드를 읽는다. 봇은 그리드를 **읽기만** 한다 (불변식 1).
	UPROPERTY()
	mutable TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	friend class FBotControllerTest; // 자동화 테스트가 FSM 판정·BFS 를 직접 호출하기 위한 접근
};
