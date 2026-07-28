#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Bomb/ExplosionTypes.h"
#include "ExplosionSubsystem.generated.h"

struct FVoxelGrid;

// 폭발 전파 계산(static 순수 함수 Propagate — 불변식 2)
// + 서버의 연쇄 폭발 프레임 분산 스케줄링(GDD 7.3 — Task 16 에서 추가).
//
// Task 15 시점엔 ABomb 이 없어(Task 16 생성) 연쇄 스케줄링 멤버
// (RequestDetonate / ProcessChainStep / PendingChain / ChainTimer)를 넣으면
// 존재하지 않는 타입 참조로 컴파일이 깨진다 — 골격 + Propagate 만 둔다 (죽은 코드 금지).
UCLASS()
class CRAZYARCADE3D_API UExplosionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ─── 순수 함수 (불변식 2) ───
	// FVoxelGrid 만 읽고 FExplosionResult 를 반환한다. 부작용 0, 멤버 접근 0.
	// 서버(실폭발)·클라(위험 프리뷰 데칼)·봇 AI(설치 판단)가 전부 이 함수를 쓴다 —
	// 그래서 표시와 실제가 구조적으로 어긋날 수 없다.
	//
	// 시그니처 확장 (설계서 (Grid, Origin, Range) 대비 — Task 문서 "설계서와의 차이"):
	//   bFloorDestructible — 룰셋 값을 인자로 받는다. 안에서 룰셋을 읽으면 순수성이 깨진다.
	//   BombCells          — 현재 폭탄이 놓인 셀 목록 (정렬된 TArray — 결정론 유지). 연쇄 판정용.
	//
	// 전파 규칙 (GDD 2.2): 방향 순서 고정 [+X,-X,+Y,-Y,+Z,-Z], 방향별로 1..Range 진행.
	//   Immortal     → 막힘 (셀 미포함)
	//   Destructible → Broken 에 넣고 그 방향 멈춤
	//   Floor        → bFloorDestructible 이면 Broken, 어느 쪽이든 멈춤
	//   Empty        → Water. BombCells 에 있으면 Chained 표시 후 전파는 계속.
	static FExplosionResult Propagate(
		const FVoxelGrid& Grid, const FIntVector& Origin, int32 Range,
		bool bFloorDestructible, const TArray<FIntVector>& BombCells);

	// ─── 서버 전용 연쇄 스케줄링 — TODO(Task 16, ABomb 생성 후) ───
	// void RequestDetonate(ABomb* Bomb);
	//   폭탄 하나가 터졌다 → PendingChain 에 투입 (최상단 HasAuthority 가드 — 불변식 5).
	//   실제 처리는 ProcessChainStep 이 룰셋 ChainStepDelay(초) 간격 타이머로 단계별 수행 —
	//   서버 스파이크 제거 + "촤르륵" 연출 동시 획득 (GDD 7.3).
	//
	// void ProcessChainStep();  // 단계마다:
	//   1. PendingChain 에서 이번 단계 폭탄들을 꺼내 각각 Propagate 호출
	//   2. VoxelWorld->ServerDestroyBlocks(BrokenCells)  (Task 06 경로 — 불변식 1)
	//   3. 물줄기 셀 목록 Multicast → 클라가 풀(Task 14)에서 FX 획득, WaterLingerTime 후 반납
	//   4. WaterCells 안의 캐릭터(발밑 셀 기준, Task 10 GetFootCell) → StatusComponent::ServerTrap
	//   5. WaterCells 안의 아이템 소멸 (Task 23 이후)
	//   6. ChainedCells → ChainedBombs 해석, 다음 단계 PendingChain 으로 (중복 폭발 방지 플래그 필수)
	//
	// 멤버 (Task 16): UPROPERTY() TArray<TObjectPtr<ABomb>> PendingChain; FTimerHandle ChainTimer;
};
