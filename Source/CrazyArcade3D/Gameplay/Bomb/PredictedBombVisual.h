#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/PooledActor.h"
#include "PredictedBombVisual.generated.h"

class UStaticMeshComponent;

// 폭탄 설치의 로컬 예측 비주얼 (Task 17, 데이터 흐름 3.1) — 클라 전용, 풀링 대상. bReplicates = false.
// 로직 0 — 타이머·판정·연쇄는 100% 서버 ABomb 소유 (불변식 3). 여기에 타이머·폭발 코드를
// 넣는 순간 불변식 3이 깨진다 — 리뷰에서 무조건 반려.
// 서버 확정(ABomb 복제 도착) 또는 거부(ClientRejectBomb) 시 Cell 매칭으로 풀에 반납된다 —
// 예측에 상태가 없으므로 이펙트만 지우면 되고 상태 불일치가 원천적으로 불가능하다.
// 메시는 BP 서브클래스(BP_PredictedBombVisual)에서 지정 — BP_Bomb 과 같은 메시(구분 어려워야 정상).
// 데디 서버 가드는 스폰 경로(ACA3DCharacter::TryPlaceBombPredicted — 권한 있으면 예측 생략)가
// 담당한다 — 이 액터는 원격 클라에서만 스폰되므로 BeginPlay 메시 파괴(ABomb 관례)가 필요 없다.
UCLASS()
class CRAZYARCADE3D_API APredictedBombVisual : public AActor, public IPooledActor
{
	GENERATED_BODY()

public:
	APredictedBombVisual();

	// 어느 셀의 예측인지 — 서버 확정(ABomb::BeginPlay)/거부(ClientRejectBomb) 때 매칭 키.
	// 획득 경로(TryAcquirePredictedVisual)가 매번 덮어쓴다 — 풀 재사용 잔존값이 새 나갈 틈 없음.
	FIntVector Cell = FIntVector::ZeroValue;

	// ─── IPooledActor ───
	virtual void OnAcquiredFromPool() override;  // 메시 표시
	virtual void OnReleasedToPool() override;    // 메시 숨김 (돌던 것 없음 — 타이머 0개가 이 클래스의 계약)

protected:
	// BP 서브클래스에서 메시 에셋만 지정 — BP 로직 금지.
	UPROPERTY(VisibleAnywhere, Category="Bomb")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

private:
	// 메시 미지정 경고는 인스턴스당 1회만 — 풀 재사용마다 스팸 방지.
	bool bWarnedMissingMesh = false;
};
