// PoolSubsystemTests.cpp 전용 임시 테스트 액터 (Task 14 검증용).
//
// UHT가 .cpp를 파싱하지 않으므로 UCLASS는 헤더에 있어야 한다 — 테스트 밖 코드에서 include 금지.
// (UCLASS를 WITH_AUTOMATION_TESTS 로 감싸면 UHT가 깨지므로 가드 없이 둔다.)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/PooledActor.h"
#include "PoolSubsystemTests.generated.h"

// IPooledActor를 구현하는 테스트 액터 — 콜백 호출 횟수와 "콜백 시점의 액터 상태"를 기록해
// 호출 순서(복원 후 Acquire 콜백 / Release 콜백 후 비활성화)를 검증할 수 있게 한다.
UCLASS(NotBlueprintable, NotPlaceable, Transient)
class ACA3DPoolTestActor : public AActor, public IPooledActor
{
	GENERATED_BODY()

public:
	ACA3DPoolTestActor()
	{
		// 틱을 켜 두어야 풀의 "Release 시 틱 off / Acquire 시 틱 on" 전환을 관측할 수 있다.
		PrimaryActorTick.bCanEverTick = true;
		PrimaryActorTick.bStartWithTickEnabled = true;

		// 루트 없는 액터는 트랜스폼이 없어 Acquire 의 위치 세팅이 무시된다 — 검증에 필수.
		RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	}

	virtual void OnAcquiredFromPool() override
	{
		++AcquiredCount;
		bHiddenAtLastAcquire = IsHidden(); // 기대: false — 풀이 표시 복원 후 콜백
		LastCallback = FName(TEXT("Acquired"));
	}

	virtual void OnReleasedToPool() override
	{
		++ReleasedCount;
		bHiddenAtLastRelease = IsHidden(); // 기대: false — 콜백 후에 숨김 처리
		LastCallback = FName(TEXT("Released"));
	}

	int32 AcquiredCount = 0;
	int32 ReleasedCount = 0;
	bool bHiddenAtLastAcquire = true;
	bool bHiddenAtLastRelease = true;
	FName LastCallback = NAME_None;
};
