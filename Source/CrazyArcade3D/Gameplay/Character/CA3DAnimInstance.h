#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "CA3DAnimInstance.generated.h"

class ACA3DCharacter;
enum class ELifeState : uint8;

// 8종 캐릭터 AnimBP 의 공용 C++ 베이스 (Task 37).
//
// AnimBP 는 이 클래스의 값을 읽는 **상태 머신만** 갖는다 (BP 로직 금지 — 코딩 규칙).
// 판정·계산은 전부 여기 C++ 이다: "움직이는 중인가" 의 임계값, "죽었는가" 의 enum 매핑이
// AnimBP 8종에 흩어지면 캐릭터마다 애니메이션 전이 기준이 조용히 갈라진다.
//
// 값 가공은 static 순수 함수로 분리했다 — 인스턴스 없이 테스트된다
// (UMatchWidget 의 표시 가공 함수와 같은 관례).
UCLASS()
class CRAZYARCADE3D_API UCA3DAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// ─── AnimBP 노출 값 (전부 읽기 전용 — 상태 머신 전이 조건의 재료) ───
	// Transient: 매 프레임 NativeUpdateAnimation 이 다시 계산한다 — 저장·복제할 값이 아니다.

	// 수평 속도(cm/s). Z(점프·낙하)는 뺀다 — 이동 애니메이션은 평면 이동만 반영해야
	// 낙하 중 제자리에서 달리기가 재생되지 않는다.
	UPROPERTY(BlueprintReadOnly, Transient, Category="CA3D|Anim")
	float Speed = 0.f;

	// Speed 가 의미 있는 크기인가 — Idle ↔ Move 전이 조건.
	UPROPERTY(BlueprintReadOnly, Transient, Category="CA3D|Anim")
	bool bMoving = false;

	// 공중(CMC IsFalling) — 점프·낙하 상태 전이 조건.
	UPROPERTY(BlueprintReadOnly, Transient, Category="CA3D|Anim")
	bool bInAir = false;

	// 물방울에 갇힘 (원본: UStatusComponent::LifeState == Trapped).
	UPROPERTY(BlueprintReadOnly, Transient, Category="CA3D|Anim")
	bool bTrapped = false;

	// 사망 (원본: UStatusComponent::LifeState == Dead — Spectating 도 사망 취급).
	UPROPERTY(BlueprintReadOnly, Transient, Category="CA3D|Anim")
	bool bDead = false;

	// 소유 ACA3DCharacter 캐시. 소유자가 ACA3DCharacter 가 아니면(AnimBP 에디터 프리뷰 등)
	// 캐시가 비고 위 값들은 기본값을 유지한다 — 안전하게 Idle 로 보인다.
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ─── static 순수 함수 (값 가공) — 인스턴스 없이 테스트된다 (MatchWidget 관례) ───

	// 속도 벡터 → 수평 속력(cm/s). Z 성분 제외.
	static float ComputeHorizontalSpeed(const FVector& Velocity);

	// 수평 속력 → "움직이는 중". 임계값이 KINDA_SMALL_NUMBER 인 근거: CMC 는 제동 시
	// 속도를 정확히 0 으로 스냅하므로(BrakingDecelerationWalking 경로) 여기서 걸러야 할 것은
	// 부동소수 잔여뿐이다 — 게임적 임계(예: "30cm/s 이상")를 두면 근거 없는 매직 넘버가 된다.
	static bool ComputeMoving(float HorizontalSpeed);

	// LifeState → 갇힘 여부.
	static bool ComputeTrapped(ELifeState LifeState);

	// LifeState → 사망 여부. Spectating 은 현재 쓰이지 않지만(UStatusComponent::OnRep_Life 주석)
	// StatusComponent.cpp 의 "Dead || Spectating" 판정과 같은 묶음으로 취급한다 —
	// 나중에 쓰이게 돼도 시체 애니메이션 기준이 두 벌이 되지 않는다.
	static bool ComputeDead(ELifeState LifeState);

private:
	// NativeInitializeAnimation 이 캐시 — 매 프레임 Cast 를 피한다.
	UPROPERTY(Transient)
	TObjectPtr<ACA3DCharacter> OwnerCharacter;
};
