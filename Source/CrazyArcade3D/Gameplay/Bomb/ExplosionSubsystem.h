#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Gameplay/Bomb/ExplosionTypes.h"
#include "ExplosionSubsystem.generated.h"

struct FVoxelGrid;
class ABomb;
class AVoxelWorld;
class AExplosionFXRelay;
class AItemPickup;
class UCA3DRuleSet;

// 폭발 전파 계산(static 순수 함수 Propagate — 불변식 2)
// + 서버의 연쇄 폭발 프레임 분산 스케줄링(GDD 7.3 — Task 16).
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

	// ─── 서버 전용: 연쇄 스케줄링 (Task 16) ───

	// 폭탄 하나가 터졌다 → PendingChain 투입. 큐가 놀고 있으면 즉시 1단계 처리(퓨즈 시각 정확),
	// 연쇄로 유발된 폭탄은 ProcessChainStep 이 룰셋 ChainStepDelay 간격으로 단계별 처리 —
	// 서버 스파이크 제거 + "촤르륵" 연출 동시 획득 (GDD 7.3).
	// 유일한 호출자는 ABomb::ServerForceDetonate — bDetonated 기록이 선행된다 (중복 방지).
	void RequestDetonate(ABomb* Bomb);

	// ─── 서버 전용: 활성 폭탄 레지스트리 ───
	// ABomb::ServerArm 이 등록, ServerReleaseSlot(폭발 처리·EndPlay 안전망)이 해제.
	// 소비처: 설치 검증("그 셀에 폭탄 있음")·연쇄 셀 해석·Propagate 의 BombCells 인자.
	void RegisterBomb(ABomb* Bomb);
	void UnregisterBomb(ABomb* Bomb);
	ABomb* FindBombAt(const FIntVector& Cell) const;

	// 현재 폭탄 셀 목록 — 좌표 사전순 정렬 TArray (결정론 유지 — 불변식 4 준용).
	TArray<FIntVector> GetActiveBombCellsSorted() const;

	// ─── 서버 전용: 활성 아이템 레지스트리 (Task 23) ───
	// AItemPickup::ServerInit 이 등록, EndPlay(획득·소멸·월드 정리)가 해제.
	// 폭탄 레지스트리와 같은 이유로 TActorIterator 를 쓰지 않는다 — 액터 순회 순서는
	// 실행마다 달라질 수 있어 판정 입력으로 부적합하다 (불변식 4 준용).
	void RegisterItem(AItemPickup* Item);
	void UnregisterItem(AItemPickup* Item);
	AItemPickup* FindItemAt(const FIntVector& Cell) const;

private:
	// 단계마다 (명세 6단계): ① PendingChain 의 폭탄들 Propagate → ② ServerDestroyBlocks(Broken)
	// (불변식 1) → ③ 물줄기 셀 Multicast(AExplosionFXRelay — 클라 FX) → ④ WaterCells 안
	// 캐릭터(GetFootCell) ServerTrap → ⑤ 아이템 소멸 후 노출 스폰(Task 23 — 순서 근거는 구현부
	// 주석) → ⑥ 연쇄 셀을 폭탄으로 해석해 다음 단계 투입(ServerForceDetonate 의 bDetonated
	// 가드가 중복 방지).
	void ProcessChainStep();

	// ⑤ 단계 본체 — 물줄기 안 기존 아이템 소멸 + 이번에 부서진 셀의 숨은 아이템 노출.
	void ProcessStepItems(AVoxelWorld* VoxelWorld, const UCA3DRuleSet* Rules,
	                      const TArray<FIntVector>& StepWater, const TArray<FIntVector>& StepBroken);

	AVoxelWorld* ResolveVoxelWorld();
	AExplosionFXRelay* ResolveFXRelay(); // 서버: lazy 스폰·캐시 (Multicast 소유 액터)

	// 연쇄 프레임 분산 (GDD 7.3): 다음 단계에 터질 폭탄들.
	UPROPERTY()
	TArray<TObjectPtr<ABomb>> PendingChain;

	// 서버 전용 활성 폭탄 레지스트리 (등록 순서는 서버 로컬 — 판정은 정렬 결과만 쓴다).
	UPROPERTY()
	TArray<TObjectPtr<ABomb>> ActiveBombs;

	// 서버 전용 활성 아이템 레지스트리 (Task 23) — 셀 조회 전용이라 순서에 의존하지 않는다.
	UPROPERTY()
	TArray<TObjectPtr<AItemPickup>> ActiveItems;

	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	UPROPERTY()
	TObjectPtr<AExplosionFXRelay> FXRelay;

	FTimerHandle ChainTimer;

	// 단계 처리 중 재진입 방지 — 처리 중 유발된 연쇄(RequestDetonate)는 즉시 처리하지 않고
	// 단계 종료 시 예약되는 다음 타이머를 기다린다.
	bool bProcessingStep = false;

	friend class FBombTest; // 자동화 테스트가 PendingChain·타이머 상태 검증을 위한 접근
	friend class FItemPickupTest; // 자동화 테스트가 아이템 레지스트리 검증을 위한 접근 (Task 23)
};
