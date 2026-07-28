// UPoolSubsystem — 월드당 1개 자동 생성되는 제네릭 액터 풀 (Task 14).
//
// 물줄기 세그먼트(최대 부하 ~700)·파편 FX·위험 데칼·아이템 픽업을 통일 API로 재사용한다.
// **클라 시각 요소 전용** — 서버 권한 상태를 가진 액터(ABomb)는 풀링하지 않는다.
// 리플리케이션과 얽혀 디버깅이 어려워지고, 재사용 시 상태 오염 위험이 이득보다 크다
// (설계서 2.8 — 강제 장치는 없으며 호출 규약이다).
//
// Release된 액터는 숨김 + 컬리전 off + 틱 off 상태로 클래스별 Free 리스트에 보관된다.
//
// Core 폴더는 아무것도 참조하지 않는다 — 엔진 헤더와 Core 내부만 include 한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "Templates/Casts.h"
#include "PoolSubsystem.generated.h"

class AActor;

// TMap 값으로 TArray를 직접 넣을 수 없으므로(UPROPERTY 중첩 컨테이너 금지) 래퍼 struct.
// UPROPERTY 라서 안의 액터 포인터가 GC에 수거되지 않는다.
USTRUCT()
struct FPooledActorArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Actors;
};

UCLASS()
class UPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 매치 시작 전 미리 스폰해 히치를 없앤다 (GDD 7.3: 100~200개 선확보).
	// 선스폰된 액터는 곧바로 Release 상태(콜백 포함)로 Free 리스트에 들어간다.
	void Prewarm(TSubclassOf<AActor> Class, int32 Count);

	// 풀에서 꺼낸다. 비어 있으면 새로 스폰. 상태 복원 + 위치 세팅 후
	// IPooledActor::OnAcquiredFromPool 호출.
	AActor* Acquire(TSubclassOf<AActor> Class, const FTransform& T);

	// 풀로 돌려보낸다. IPooledActor::OnReleasedToPool 호출 후 비활성화.
	void Release(AActor* Actor);

	// 타입 안전 헬퍼.
	template<typename T>
	T* Acquire(TSubclassOf<T> Class, const FTransform& X)
	{
		return Cast<T>(Acquire(TSubclassOf<AActor>(Class), X));
	}

private:
	// 풀 액터 공용 스폰 경로 — 항상 스폰 성공하도록 컬리전 핸들링 고정.
	AActor* SpawnPooledActor(TSubclassOf<AActor> Class, const FTransform& T);

	// 콜백 호출 → 숨김 + 컬리전 off + 틱 off → Free 리스트 보관. Prewarm/Release 공용.
	void DeactivateAndStore(AActor* Actor);

	// 클래스별(정확한 클래스 단위) 프리 리스트. UPROPERTY로 GC 수거 방지.
	// ⚠️ 이 TMap은 **조회만** 한다 — 순회 금지 (순서 비결정 문제와 무관하게 유지).
	UPROPERTY()
	TMap<TObjectPtr<UClass>, FPooledActorArray> Free;
};
