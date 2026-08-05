#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OcclusionFadeComponent.generated.h"

class AVoxelWorld;

// 카메라와 내 캐릭터 사이를 막는 블록을 옅게 만든다 (GDD 5장 "가림 처리 = 반투명 디더 페이드").
//
// **클라 시각 전용이다.** 게임 상태를 하나도 건드리지 않는다 — 판정·이동·리플리케이션과 무관하고,
// 서버는 이 컴포넌트를 아예 만들지 않는다. 잘못 동작해도 게임 결과는 달라지지 않는다.
//
// 카메라 붐을 당겨 해결하지 않는 이유는 Task 11 에서 이미 정했다: 붐 컬리전을 켜면 벽에
// 붙을 때마다 화면이 확대돼 "몇 칸 떨어져 있나"를 눈으로 재는 감각이 무너진다.
//
// 역할 분담 (폴더 의존 규칙 유지):
//   · 어디가 가렸나 → 여기(Gameplay). 카메라·폰을 아는 쪽이다.
//   · 격자 위 직선이 지나는 칸 → VoxelRay::GatherSolidCells (Voxel, 순수 함수)
//   · 그 칸을 어떻게 옅게 보이게 하나 → IVoxelRenderer::SetCellFade (Voxel, 숫자만 받는다)
// 지형은 "카메라가 나를 못 본다"는 개념을 모른 채로 남는다.
//
// 성능: 다시 계산하는 것은 룰셋 OcclusionTraceInterval(0.1초)마다 — GDD 7.4 의 명시 항목.
// 페이드 값 보간만 매 프레임 돈다 (대상은 보통 한 자릿수 칸).
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class CRAZYARCADE3D_API UOcclusionFadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOcclusionFadeComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	// 페이드를 전부 되돌린다 — 안 하면 사망·레벨 전환 후에도 그 블록만 투명한 채로 남는다.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 카메라 → 캐릭터 샘플 지점들로 격자 레이 마칭. 결과를 CurrentTargets 에 채운다.
	void RefreshOccluders();

	// 페이드 값을 목표치로 한 걸음 옮기고 렌더러에 기록한다. 0 에 도달한 칸은 추적 해제.
	void AdvanceFades(float DeltaTime);

	AVoxelWorld* ResolveVoxelWorld(); // lazy 탐색·캐시 (ABomb 관례)

	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	// 지금 가리고 있다고 판정된 칸 (RefreshOccluders 가 매번 새로 채운다).
	TSet<FIntVector> CurrentTargets;

	// 추적 중인 칸의 현재 페이드 값. 목표에서 빠진 칸도 0 이 될 때까지 남는다 —
	// 즉시 0 으로 되돌리면 카메라를 조금만 돌려도 블록이 깜빡인다.
	TMap<FIntVector, float> FadeValues;

	// 재계산 누적 시간 (OcclusionTraceInterval 마다 1회).
	float TimeSinceTrace = 0.f;

	// 로그 스팸 방지 — 가려진 칸 수가 바뀔 때만 한 줄 찍는다. -1 = 아직 한 번도 안 찍음.
	int32 LastLoggedOccluderCount = -1;

	// 룰셋 값 — BeginPlay 에서 1회 조회 (틱마다 GameState 를 타지 않는다).
	float TraceInterval = 0.1f;
	float FadeSpeed = 6.f;
	float FadeAmount = 0.8f;
	int32 SampleCount = 5;

	friend class FOcclusionFadeTest; // 자동화 테스트가 페이드 진행·해제를 검증하기 위한 접근
};
