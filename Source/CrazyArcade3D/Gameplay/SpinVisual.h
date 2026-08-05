#pragma once

#include "CoreMinimal.h"

class USceneComponent;
class UWorld;

// 맵에 놓인 물건(폭탄·예측 폭탄·아이템)의 제자리 yaw 회전 (2026-08-06 사용자 요청).
// "다른 카메라 시점에서도 잘 보이게" — 카메라가 45도 스냅으로 돌아도(GDD 5장) 물건 쪽이
// 계속 돌기 때문에 어느 각도에서 봐도 실루엣이 바뀌며 눈에 띈다.
//
// 세 클래스가 이 한 곳을 공유하는 이유: APredictedBombVisual 은 ABomb 과 **구분되지 않아야**
// 정상이다(PredictedBombVisual.h). 회전 속도가 갈리면 서버 확정 순간의 예측→진짜 교체가
// 각도 점프로 눈에 보인다. 속도의 출처를 하나로 묶어 그 사고를 구조적으로 막는다.
//
// **시각 전용이다.** 판정 컴포넌트(ABomb::BlockingBox, AItemPickup::PickupSphere)는 돌지 않는다 —
// 박스를 함께 돌리면 막히는 방향이 시간에 따라 달라져 "이번엔 왜 통과했지"가 생긴다.
namespace CA3DSpin
{
	// 룰셋의 회전 속도(초당 도). GameState 복제 포인터 → CDO 폴백
	// (StatusComponent::ResolveRules 와 동일 관례 — 룰셋 도착 전에도 값이 나온다).
	float ResolveDegreesPerSecond(const UWorld* World);

	// Component 를 로컬 yaw 로 DeltaSeconds 만큼 돌린다.
	// 데디 서버·속도 0·컴포넌트 없음은 전부 no-op (불변식 5 — 호출부 가드의 이중 안전망).
	void ApplyYaw(USceneComponent* Component, float DeltaSeconds, float DegreesPerSecond);
}
