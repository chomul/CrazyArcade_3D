#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CA3DPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class ACA3DPlayerState;
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

	// 클라→서버: 내 카메라 yaw 스냅 인덱스(0~7). **관전자에게 보여줄 표현 값**이라 서버가 받아
	// ACA3DPlayerState::CamYawIndex 로 복제한다 — 내 카메라 자체는 여전히 로컬 ControlRotation 이
	// 굴린다(서버는 내 시점을 몰라도 된다). 호출은 인덱스가 **바뀌는 순간에만** (PushCamYawIndex).
	UFUNCTION(Server, Reliable)
	void ServerSetCamYawIndex(uint8 NewIndex);

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

	// Digital — 니들 수동 사용 (Task 23, 사용자 확정: 자동 사용 아님).
	// 폭탄과 다른 키여야 한다: 갇힌 상태에서 폭탄 키를 누르는 것과 니들을 쓰는 것은
	// 전혀 다른 판단인데 한 키에 묶으면 "폭탄 놓으려다 니들을 태워먹는" 사고가 난다.
	UPROPERTY(EditDefaultsOnly, Category="Input")
	TObjectPtr<UInputAction> IA_UseNeedle;

	void OnMove(const FInputActionValue& V);
	void OnMoveCompleted();                       // 관전 전환 래치 해제 (살아 있을 땐 하는 일 없음)
	void OnJumpStarted();
	void OnJumpCompleted();
	void OnPlaceBomb();                           // 셀 계산·검증은 캐릭터/서버 소관 — 여기선 전달만
	void OnUseNeedle();                           // 판정은 캐릭터/서버 소관 — 여기선 전달만
	void OnRotateCam(const FInputActionValue& V); // ±45도 스냅, 보간 회전

private:
	// 카메라 상태 — 스냅 인덱스가 목표각이고 SmoothCamYaw 가 보간 현재각.
	// 스텝은 랩하지 않고 누적한다 (보간이 FRotator 정규화로 최단 경로를 택하므로 안전).
	// ⚠️ 45도 스텝 상수를 여기 두지 않는다 — 스냅 공식의 단일 출처는 CameraYawSnap 이다
	//    (Core/CameraYawSnap.h). 여기에 사본을 두는 순간 관전 각과 갈라진다.
	int32 CamYawSteps = 0;
	float SmoothCamYaw = 0.f;

	// 지금 고른 45도 칸 (0~7) — 복제 값 ACA3DPlayerState::CamYawIndex 와 같은 좌표계다.
	uint8 GetCamYawIndex() const;

	// 카메라 기준 입력 모드가 쓰는 스냅 목표각(도). 보간 중에도 이동 기준이 흔들리지
	// 않도록 보간각(SmoothCamYaw)이 아니라 스냅각을 쓴다 — 회전 중 이동이 끊기지 않는다.
	float GetSnappedCamYaw() const;

	// 스냅 인덱스가 **바뀐 순간에만** 서버로 올린다. Q/E 를 누를 때뿐이라 매우 드물다 —
	// 매 틱 보내면 8인 기준으로 초당 수백 개의 RPC 가 아무 변화 없이 오간다.
	void PushCamYawIndex();

	// 마지막으로 서버에 올린 인덱스. 복제 기본값(0)과 초기 스텝(0)이 이미 일치하므로
	// 한 번도 회전하지 않은 플레이어는 RPC 를 단 한 번도 보내지 않는다.
	uint8 LastSentCamYawIndex = 0;

	// ─── 관전 (사망 후 생존자 추적 — 2026-08-06 사용자 확정) ────────────────────
	//
	// **전부 로컬 표시다.** "지금 누구를 보고 있는가" 는 복제하지 않는다 — 시점은 각 클라의
	// 화면 사정이고, 복제하면 남의 관전 대상까지 동기화하는 순수한 낭비가 된다.
	// 반면 "누가 살아 있는가" 는 새로 만들지 않고 이미 복제되는 ACA3DPlayerState::bAlive 를 읽는다.
	//
	// 폰은 건드리지 않는다 (UnPossess·관전 폰 스폰 없음) — Task 27 의 "폰 유지" 결정 그대로다.
	// 관전은 **보는 대상을 바꾸는 것**이지 폰을 없애는 것이 아니라서, 부활 여지·입력 바인딩·
	// HUD 폰 캐시가 전부 그대로 살아 있다.

	// 매 프레임(PlayerTick): 대상이 없거나 죽었으면 다음 생존자로 넘긴다.
	void UpdateSpectateView();

	// 다음(+1)/이전(-1) 생존자로. 생존자가 하나도 없으면 마지막 대상을 그대로 둔다.
	void CycleSpectateTarget(int32 Step);

	// 관전 대상 후보 — GameState->PlayerArray 를 **고정 순서로** 훑어 bAlive 만 모은다.
	// 순서가 결정론적이어야 "다음/이전" 이 매번 같은 순환을 돈다.
	void CollectSpectateCandidates(TArray<ACA3DPlayerState*>& OutCandidates) const;

	void SetSpectateTarget(ACA3DPlayerState* NewTarget);
	void HandleSpectateAxis(float AxisX); // 사망 중 좌/우 축 → 대상 전환 (눌림 판정 포함)

	bool IsLocalPawnDead() const; // 관전 여부의 유일한 조건 — 별도 상태를 만들지 않았다 (Task 문서)
	bool IsMatchEnded() const;    // 종료 후에는 시점을 고정한다 (결과 화면 중 카메라 이동 금지)

	// 관전 대상 — 폰이 아니라 PlayerState 를 들고 있는다. 폰은 사망·재스폰으로 바뀔 수 있지만
	// PlayerState 는 매치 내내 같은 객체이고, 생존 여부(bAlive)의 출처이기도 하다.
	UPROPERTY()
	TObjectPtr<ACA3DPlayerState> SpectateTarget;

	// 좌/우 축 래치. Enhanced Input 의 Triggered 는 **누르고 있는 동안 매 프레임** 오므로
	// 래치가 없으면 한 번 눌러 생존자 전원을 지나쳐 버린다. 축이 임계값 아래로 돌아오거나
	// 입력이 끝나야(OnMoveCompleted) 다음 전환을 받는다.
	bool bSpectateAxisLatched = false;

	friend class FSpectateTest; // 자동화 테스트가 대상 선정·순환·자동 전환 검증을 위한 접근
};
