#include "Gameplay/Character/CA3DPlayerController.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "HAL/IConsoleManager.h"

// ── 카메라 구조 택1 보고 (Task 11 응답 원칙) ─────────────────────────────────
// "컨트롤러 ViewTarget 별도 관리" 대신 "캐릭터 SpringArm(bUsePawnControlRotation) +
// 컨트롤러 ControlRotation" 을 채택했다.
// 이유: bAutoManageActiveCameraTarget 기본 파이프라인을 그대로 타므로 리스폰·폰 교체 시
// 카메라가 자동으로 따라오고(별도 카메라 액터·SetViewTarget 관리 코드 0줄), 카메라 각의
// 소유는 여전히 이 컨트롤러(ControlRotation)라 "카메라는 컨트롤러 관심사" 원칙도 유지된다.

// ⚠️ 미확정 — WASD 입력 기준 (설계서 7장). 기본 0 = 월드 축. PIE 비교 후 사용자 확정.
static TAutoConsoleVariable<int32> CVarCA3DCameraRelativeInput(
	TEXT("ca3d.CameraRelativeInput"),
	0,
	TEXT("WASD 입력 기준. 0 = 월드 축(기본), 1 = 카메라 기준(45도 스냅각에 맞춰 회전)"));

namespace
{
	// 룰셋 해석 — 카메라 pitch·거리·보간 속도의 출처. 복제 미도착이면 CDO 기본값으로 진행
	// (카메라는 클라 시각 전용이라 잠시 기본값이어도 무해 — 도착하면 다음 틱부터 반영된다).
	const UCA3DRuleSet* ResolveRules(const UWorld* World)
	{
		if (World)
		{
			if (const ACA3DGameState* CA3DGameState = Cast<ACA3DGameState>(World->GetGameState()))
			{
				if (CA3DGameState->Rules)
				{
					return CA3DGameState->Rules;
				}
			}
		}
		return GetDefault<UCA3DRuleSet>();
	}
}

void ACA3DPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer()) return; // 불변식 5 — 입력·카메라는 시각 전용
	if (!IsLocalPlayerController()) return; // 서버에 뜬 원격 플레이어의 컨트롤러도 제외

	// 시작 카메라 각 — 스폰 회전이 아니라 고정 내려보기 각도에서 출발.
	SmoothCamYaw = GetSnappedCamYaw();
	SetControlRotation(FRotator(ResolveRules(GetWorld())->CameraPitchDeg, SmoothCamYaw, 0.f));

	// IMC 등록 — 로컬 플레이어 서브시스템은 로컬 컨트롤러에만 존재한다.
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		if (DefaultIMC)
		{
			Subsystem->AddMappingContext(DefaultIMC, 0);
		}
		else
		{
			UE_LOG(LogCA3D, Warning,
				TEXT("ACA3DPlayerController: DefaultIMC 미지정 — BP_CA3DPlayerController 에 IMC_Default 를 지정할 것"));
		}
	}
}

void ACA3DPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		// UE5 기본 DefaultInputComponentClass 가 EnhancedInputComponent — 여기 도달은 설정 훼손뿐.
		UE_LOG(LogCA3D, Error,
			TEXT("ACA3DPlayerController: InputComponent 가 EnhancedInputComponent 아님 — DefaultInputComponentClass 확인"));
		return;
	}

	// IA 미지정은 경고 없이 건너뛴다 — BeginPlay 의 DefaultIMC 경고가 연결 누락을 대표한다.
	if (IA_Move)
	{
		EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ACA3DPlayerController::OnMove);
	}
	if (IA_Jump)
	{
		EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &ACA3DPlayerController::OnJumpStarted);
		EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &ACA3DPlayerController::OnJumpCompleted);
	}
	if (IA_RotateCam)
	{
		EIC->BindAction(IA_RotateCam, ETriggerEvent::Started, this, &ACA3DPlayerController::OnRotateCam);
	}
	if (IA_PlaceBomb)
	{
		EIC->BindAction(IA_PlaceBomb, ETriggerEvent::Started, this, &ACA3DPlayerController::OnPlaceBomb);
	}
}

void ACA3DPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (IsRunningDedicatedServer()) return; // 불변식 5 — 카메라는 시각 전용
	if (!IsLocalPlayerController()) return;

	const UCA3DRuleSet* Rules = ResolveRules(GetWorld());

	// 45도 스냅 보간 — FRotator 경유로 ±180 랩을 정규화해 항상 최단 경로로 돈다.
	// 회전 중에도 이동은 안 끊긴다: 이동 기준은 월드축 또는 스냅각(OnMove)이라 보간각과 무관.
	const FRotator Current(0.f, SmoothCamYaw, 0.f);
	const FRotator Target(0.f, GetSnappedCamYaw(), 0.f);
	SmoothCamYaw = FMath::RInterpTo(Current, Target, DeltaTime, Rules->CameraYawInterpSpeed).Yaw;

	// 고정 내려보기 pitch + 보간 yaw — 캐릭터의 CameraBoom(bUsePawnControlRotation)이 소비한다.
	SetControlRotation(FRotator(Rules->CameraPitchDeg, SmoothCamYaw, 0.f));
}

void ACA3DPlayerController::OnMove(const FInputActionValue& V)
{
	ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn());
	if (!CA3DCharacter) return;

	// IMC 관례: X = 오른쪽(+D/-A), Y = 앞(+W/-S) — 에디터 연결 절차 참조.
	const FVector2D Axis = V.Get<FVector2D>();

	FVector2D WorldAxis;
	if (CVarCA3DCameraRelativeInput.GetValueOnGameThread() != 0)
	{
		// 카메라 기준 — 45도 스냅각 기준으로 회전. 보간각이 아니라 스냅각을 쓰므로
		// 회전 연출 중에도 이동 기준이 흔들리지 않는다.
		const FRotationMatrix Basis(FRotator(0.f, GetSnappedCamYaw(), 0.f));
		const FVector Dir =
			Basis.GetUnitAxis(EAxis::X) * Axis.Y + Basis.GetUnitAxis(EAxis::Y) * Axis.X;
		WorldAxis = FVector2D(Dir.X, Dir.Y);
	}
	else
	{
		// 월드 축(기본) — 카메라가 돌아도 W = 월드 +X, D = 월드 +Y 고정.
		WorldAxis = FVector2D(Axis.Y, Axis.X);
	}

	CA3DCharacter->Move(WorldAxis);
}

void ACA3DPlayerController::OnJumpStarted()
{
	if (ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn()))
	{
		CA3DCharacter->DoJump();
	}
}

void ACA3DPlayerController::OnJumpCompleted()
{
	if (ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn()))
	{
		CA3DCharacter->StopJumping(); // bPressedJump 해제 — ACharacter 기본 점프 상태 관리
	}
}

void ACA3DPlayerController::OnPlaceBomb()
{
	// 컨트롤러는 입력만 — 셀 계산·로컬 검증·예측 비주얼·권위 검증 전부 캐릭터/서버 소관
	// (게임 상태 계산 금지). 단일 진입점: TryPlaceBombPredicted (Task 17, 데이터 흐름 3.1).
	if (ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn()))
	{
		CA3DCharacter->TryPlaceBombPredicted();
	}
}

void ACA3DPlayerController::OnRotateCam(const FInputActionValue& V)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 (로컬 입력이라 실도달 없음 — 명시 가드)

	const float Dir = V.Get<float>();
	if (FMath::IsNearlyZero(Dir)) return;

	CamYawSteps += (Dir > 0.f) ? 1 : -1; // ±45도 스냅 — 실제 회전은 PlayerTick 의 보간이
}
