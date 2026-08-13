#include "Gameplay/Character/CA3DAnimInstance.h"

#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// ─── static 순수 함수 (값 가공) ─────────────────────────────────────────────

float UCA3DAnimInstance::ComputeHorizontalSpeed(const FVector& Velocity)
{
	// Z 제외 — 낙하 속도가 이동 애니메이션 속도로 새면 공중에서 제자리 달리기가 재생된다.
	return Velocity.Size2D();
}

bool UCA3DAnimInstance::ComputeMoving(float HorizontalSpeed)
{
	// 임계 근거는 헤더 주석 — CMC 가 정지 시 속도를 정확히 0 으로 만들므로 부동소수 잔여만 거른다.
	return HorizontalSpeed > KINDA_SMALL_NUMBER;
}

bool UCA3DAnimInstance::ComputeTrapped(ELifeState LifeState)
{
	return LifeState == ELifeState::Trapped;
}

bool UCA3DAnimInstance::ComputeDead(ELifeState LifeState)
{
	// Spectating 을 함께 보는 근거는 헤더 주석 (StatusComponent.cpp 의 판정과 같은 묶음).
	return LifeState == ELifeState::Dead || LifeState == ELifeState::Spectating;
}

// ─── 갱신 ───────────────────────────────────────────────────────────────────

void UCA3DAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// 소유자가 ACA3DCharacter 가 아니면(AnimBP 프리뷰·다른 폰) 캐시가 비고,
	// NativeUpdateAnimation 이 기본값(Idle)을 유지한다 — 크래시 없이 안전.
	OwnerCharacter = Cast<ACA3DCharacter>(TryGetPawnOwner());
}

void UCA3DAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerCharacter)
	{
		// 초기화 시점에 폰이 아직 없던 경우(컴포넌트 부착 순서) 대비 재시도 — 실패하면 기본값 유지.
		OwnerCharacter = Cast<ACA3DCharacter>(TryGetPawnOwner());
		if (!OwnerCharacter)
		{
			return;
		}
	}

	Speed   = ComputeHorizontalSpeed(OwnerCharacter->GetVelocity());
	bMoving = ComputeMoving(Speed);

	const UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	bInAir = Movement && Movement->IsFalling();

	// 생존 상태 — 원본은 UStatusComponent::LifeState (복제값). 컴포넌트가 없으면 Alive 취급.
	const UStatusComponent* Status = OwnerCharacter->GetStatus();
	const ELifeState Life = Status ? Status->LifeState : ELifeState::Alive;
	bTrapped = ComputeTrapped(Life);
	bDead    = ComputeDead(Life);
}
