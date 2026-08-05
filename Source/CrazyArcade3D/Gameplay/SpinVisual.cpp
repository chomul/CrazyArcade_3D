#include "Gameplay/SpinVisual.h"

#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Components/SceneComponent.h"
#include "Engine/World.h"

float CA3DSpin::ResolveDegreesPerSecond(const UWorld* World)
{
	if (World)
	{
		if (const ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>())
		{
			if (GameState->Rules)
			{
				return GameState->Rules->PickupSpinDegreesPerSecond;
			}
		}
	}
	return GetDefault<UCA3DRuleSet>()->PickupSpinDegreesPerSecond;
}

void CA3DSpin::ApplyYaw(USceneComponent* Component, float DeltaSeconds, float DegreesPerSecond)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용

	if (!Component || FMath::IsNearlyZero(DegreesPerSecond))
	{
		return; // 룰셋에서 0 을 주면 "안 돈다" 가 정상 동작이다 (끄는 스위치를 따로 두지 않는다)
	}

	// 로컬 회전 — 액터가 아니라 메시 컴포넌트만 돌린다. 액터를 돌리면 판정 컴포넌트도 함께 돈다.
	Component->AddLocalRotation(FRotator(0.f, DegreesPerSecond * DeltaSeconds, 0.f));
}
