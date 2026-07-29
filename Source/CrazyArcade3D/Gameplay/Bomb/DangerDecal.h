#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/PooledActor.h"
#include "DangerDecal.generated.h"

class UDecalComponent;

// 위험 구역 프리뷰 데칼 1칸 (Task 16, 설계서 5장 9번) — 클라 시각 전용, 풀링 대상.
// ABomb 이 클라 BeginPlay 에서 Propagate(실폭발과 같은 함수) 결과의 WaterCells 마다
// 하나씩 획득·배치하고, 폭탄 폭발/파괴 시 반납한다 — 표시와 실제가 구조적으로 일치.
// 머티리얼은 BP 서브클래스(BP_DangerDecal)에서 지정 — 미지정이어도 동작한다 (로그 경고).
UCLASS()
class CRAZYARCADE3D_API ADangerDecal : public AActor, public IPooledActor
{
	GENERATED_BODY()

public:
	ADangerDecal();

	// 데칼 크기를 셀 크기에 맞춘다 — 값의 출처는 AVoxelWorld::CellSize (매직 넘버 금지).
	void SetCellSize(float CellSize);

	// ─── IPooledActor ───
	virtual void OnAcquiredFromPool() override;
	virtual void OnReleasedToPool() override;   // 데칼 가시성 off — 반납 후 투영 잔류 방지

protected:
	// BP 서브클래스에서 데칼 머티리얼만 지정 — BP 로직 금지.
	UPROPERTY(VisibleAnywhere, Category="FX")
	TObjectPtr<UDecalComponent> DecalComponent;

private:
	// 머티리얼 미지정 경고는 인스턴스당 1회만 — 풀 재사용마다 스팸 방지.
	bool bWarnedMissingMaterial = false;
};
