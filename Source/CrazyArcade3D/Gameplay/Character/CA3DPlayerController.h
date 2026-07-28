#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CA3DPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

// 입력(Enhanced Input)과 45도 스냅 회전 고정 각도 카메라만 담당 (Task 11, GDD 5장).
// 클라 로컬 전용 관심사 — 게임 상태를 바꾸지 않는다. 판정·상태 변경은 캐릭터(서버 RPC) 소관.
//
// 카메라 구조: "캐릭터 SpringArm(bUsePawnControlRotation) + 이 컨트롤러의 ControlRotation" 채택
// (택1 근거는 cpp 상단 주석). 컨트롤러는 yaw 스텝 상태만 소유한다.
//
// WASD 입력 기준(월드 축 vs 카메라 기준)은 ⚠️ 미확정 — 콘솔 변수 ca3d.CameraRelativeInput
// 으로 토글 (0 = 월드 축, 기본값). PIE 비교 후 사용자 확정.
UCLASS()
class CRAZYARCADE3D_API ACA3DPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;                 // IMC 등록 (로컬 컨트롤러만)
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override; // 카메라 yaw 보간 (시각 전용)

protected:
	// BP 서브클래스(BP_CA3DPlayerController)에서 에셋만 지정 — BP 로직 금지.
	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputMappingContext> DefaultIMC;

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputAction> IA_Move;       // Axis2D — X=오른쪽(D/A), Y=앞(W/S)

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputAction> IA_Jump;       // Digital

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputAction> IA_PlaceBomb;  // Digital — 핸들러는 Task 16 에서 구현 (바인딩 자리만)

	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputAction> IA_RotateCam;  // Axis1D — Q=-1 / E=+1, ±45도 스냅

	void OnMove(const FInputActionValue& V);
	void OnJumpStarted();
	void OnJumpCompleted();
	void OnRotateCam(const FInputActionValue& V); // ±45도 스냅, 보간 회전

private:
	// 45도 스냅 스텝 (GDD 5장 확정 — 구조 상수).
	static constexpr float CamYawStepDeg = 45.f;

	// 카메라 상태 — CamYawSteps × 45 가 목표각, SmoothCamYaw 가 보간 현재각.
	// 스텝은 랩하지 않고 누적한다 (보간이 FRotator 정규화로 최단 경로를 택하므로 안전).
	int32 CamYawSteps = 0;
	float SmoothCamYaw = 0.f;

	// 카메라 기준 입력 모드가 쓰는 스냅 목표각(도). 보간 중에도 이동 기준이 흔들리지
	// 않도록 보간각(SmoothCamYaw)이 아니라 스냅각을 쓴다 — 회전 중 이동이 끊기지 않는다.
	float GetSnappedCamYaw() const { return CamYawSteps * CamYawStepDeg; }
};
