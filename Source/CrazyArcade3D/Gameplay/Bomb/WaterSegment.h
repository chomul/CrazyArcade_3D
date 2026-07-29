#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/PooledActor.h"
#include "WaterSegment.generated.h"

class UStaticMeshComponent;

// 물줄기 세그먼트 1칸 (Task 16) — 클라 시각 전용, 풀링 대상 (설계서 2.8).
// 판정은 100% 서버(ExplosionSubsystem)가 하고, 이 액터는 "젖은 칸을 보여주기"만 한다.
// 메시는 BP 서브클래스(BP_WaterSegment)에서 지정 — 미지정이어도 동작한다 (로그 경고).
// 위쪽 분수·아래쪽 폭포 연출(GDD 5장)은 3주차 아트 패스 — 여기 넣지 않는다.
UCLASS()
class CRAZYARCADE3D_API AWaterSegment : public AActor, public IPooledActor
{
	GENERATED_BODY()

public:
	AWaterSegment();

	// 획득 직후 호출 — WaterLingerTime(룰셋) 후 스스로 풀에 반납한다.
	// 클라 로컬 타이머지만 시각 전용이라 불변식 3과 무관 (판정 타이머는 서버 ABomb 소유).
	void StartLinger(float Seconds);

	// ─── IPooledActor ───
	virtual void OnAcquiredFromPool() override;
	virtual void OnReleasedToPool() override;   // ⚠️ 타이머 정지 필수 (풀 오염 방지)

protected:
	// BP 서브클래스에서 메시 에셋만 지정 — BP 로직 금지.
	UPROPERTY(VisibleAnywhere, Category="FX")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

private:
	void ReleaseSelf();

	FTimerHandle LingerTimer;

	// 메시 미지정 경고는 인스턴스당 1회만 — 풀 재사용마다 스팸 방지.
	bool bWarnedMissingMesh = false;
};
