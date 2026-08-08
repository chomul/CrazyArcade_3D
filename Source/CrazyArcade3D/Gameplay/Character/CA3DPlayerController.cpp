#include "Gameplay/Character/CA3DPlayerController.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h" // 관전 진입 조건(내 폰이 죽었는가)의 출처
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Framework/CA3DPlayerState.h" // 관전 대상 목록(bAlive)의 출처 — .cpp 에서만
#include "Camera/PlayerCameraManager.h" // EViewTargetBlendFunction (SetViewTargetWithBlend 인자)
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

// WASD 입력 기준 (설계서 7장) — **카메라 기준으로 확정** (2026-07-30 PIE 비교 후 사용자 결정).
// 월드 축 기준은 카메라를 45도 돌린 뒤 "화면 위 = W" 가 깨져서 방향 감각이 어긋난다.
// 월드 축 동작은 비교·디버그용으로 0 을 남겨 둔다.
static TAutoConsoleVariable<int32> CVarCA3DCameraRelativeInput(
	TEXT("ca3d.CameraRelativeInput"),
	1,
	TEXT("WASD 입력 기준. 1 = 카메라 기준(기본, 45도 스냅각에 맞춰 회전), 0 = 월드 축"));

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
		// 관전 전환 래치 해제용 — 키를 다 떼면 Triggered 가 더 오지 않아 OnMove 안에서
		// 래치를 풀 기회가 없다. **관전 전용 IA 를 새로 만들지 않기 위한 비용이 이 한 줄뿐이다.**
		EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &ACA3DPlayerController::OnMoveCompleted);
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
	if (IA_UseNeedle)
	{
		EIC->BindAction(IA_UseNeedle, ETriggerEvent::Started, this, &ACA3DPlayerController::OnUseNeedle);
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

	// 관전 시점 유지 — 대상이 죽었으면 다음 생존자로 넘긴다 (시각 전용, 위 가드 안쪽).
	UpdateSpectateView();
}

void ACA3DPlayerController::OnMove(const FInputActionValue& V)
{
	// IMC 관례: X = 오른쪽(+D/-A), Y = 앞(+W/-S) — 에디터 연결 절차 참조.
	const FVector2D Axis = V.Get<FVector2D>();

	// 사망 중에는 좌/우가 **관전 대상 전환**이다 (2026-08-06 확정). 기존 이동 축을 재해석하므로
	// 새 IA_* 에셋이 필요 없다 — 사용자의 에디터 작업이 0 이다.
	// 분기를 컨트롤러에 둔 이유: 캐릭터의 Move/DoJump 생존 가드(봇 공용 경로)는 그대로 두어야
	// 한다. 거기에 관전을 섞으면 봇이 죽을 때마다 관전 코드를 지나가게 된다.
	if (IsLocalPawnDead())
	{
		HandleSpectateAxis(Axis.X);
		return;
	}

	// 살아 있으면 절대 관전으로 해석되지 않는다. 래치는 여기서 풀어 둔다 — 부활이 들어와도
	// 다음 사망의 첫 입력이 곧바로 먹는다.
	bSpectateAxisLatched = false;

	ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn());
	if (!CA3DCharacter) return;

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
		// 월드 축(비교·디버그용) — 카메라가 돌아도 W = 월드 +X, D = 월드 +Y 고정.
		WorldAxis = FVector2D(Axis.Y, Axis.X);
	}

	CA3DCharacter->Move(WorldAxis);
}

void ACA3DPlayerController::OnMoveCompleted()
{
	// 이동 입력이 끝났다 — 관전 전환 래치만 푼다. 이동 자체는 CMC 가 스스로 멈춘다
	// (여기서 캐릭터를 건드리면 살아 있을 때의 이동 경로가 두 갈래가 된다).
	bSpectateAxisLatched = false;
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

void ACA3DPlayerController::OnUseNeedle()
{
	// 컨트롤러는 입력만 — 보유·상태 검사는 전부 캐릭터/서버 소관 (게임 상태 계산 금지).
	// 단일 진입점: TryUseNeedle (봇도 같은 함수를 탄다 — Task 23).
	if (ACA3DCharacter* CA3DCharacter = Cast<ACA3DCharacter>(GetPawn()))
	{
		CA3DCharacter->TryUseNeedle();
	}
}

void ACA3DPlayerController::OnRotateCam(const FInputActionValue& V)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 (로컬 입력이라 실도달 없음 — 명시 가드)

	const float Dir = V.Get<float>();
	if (FMath::IsNearlyZero(Dir)) return;

	CamYawSteps += (Dir > 0.f) ? 1 : -1; // ±45도 스냅 — 실제 회전은 PlayerTick 의 보간이
}

// ─── 관전 (사망 후 생존자 추적·순환) ─────────────────────────────────────────
//
// 시점 전환은 순수 시각이다 — 아래 함수는 전부 최상단 데디 가드를 갖는다 (불변식 5).
// 서버가 관전 대상을 정하지 않는 이유는 헤더 주석 참조 (로컬 표시일 뿐 복제 상태가 아니다).

bool ACA3DPlayerController::IsLocalPawnDead() const
{
	// 폰은 죽어도 파괴되지 않으므로(Task 27) 사망 후에도 GetPawn 은 유효하다 —
	// 그래서 "죽었는가" 를 폰의 상태 컴포넌트에서 그대로 읽을 수 있다.
	const ACA3DCharacter* OwnCharacter = Cast<ACA3DCharacter>(GetPawn());
	const UStatusComponent* Status = OwnCharacter ? OwnCharacter->GetStatus() : nullptr;
	return Status && Status->LifeState == ELifeState::Dead;
}

bool ACA3DPlayerController::IsMatchEnded() const
{
	const ACA3DGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACA3DGameState>() : nullptr;
	return GameState && GameState->bMatchEnded;
}

void ACA3DPlayerController::CollectSpectateCandidates(TArray<ACA3DPlayerState*>& OutCandidates) const
{
	OutCandidates.Reset();

	const ACA3DGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACA3DGameState>() : nullptr;
	if (!GameState)
	{
		return; // 접속 직후 등 — 후보 없음 (호출부가 마지막 대상을 유지한다)
	}

	const APlayerState* OwnState = PlayerState;

	// PlayerArray 를 **입력 순서 그대로** 훑는다. 순서가 실행마다 달라지면(TSet 순회 등)
	// "오른쪽 두 번 = 두 사람 뒤" 라는 예측이 깨져 순환이 무작위 점프처럼 느껴진다.
	for (APlayerState* Each : GameState->PlayerArray)
	{
		ACA3DPlayerState* Candidate = Cast<ACA3DPlayerState>(Each);
		if (!Candidate || Candidate == OwnState)
		{
			continue; // 자기 자신은 관전 대상이 아니다 (내 시체를 보려고 관전하는 게 아니다)
		}
		if (!Candidate->bAlive)
		{
			continue; // 생존 판단은 이미 복제되는 미러 하나로 — 새 상태를 만들지 않는다
		}
		if (!IsValid(Candidate->GetPawn()))
		{
			continue; // 시점을 붙일 액터가 없다 (스폰 전·정리 중)
		}
		OutCandidates.Add(Candidate);
	}
}

void ACA3DPlayerController::SetSpectateTarget(ACA3DPlayerState* NewTarget)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시점은 시각 전용

	SpectateTarget = NewTarget;

	APawn* TargetPawn = NewTarget ? NewTarget->GetPawn() : nullptr;
	if (!TargetPawn)
	{
		return;
	}

	// **시점만** 옮긴다. UnPossess + 관전 폰 스폰 경로를 쓰지 않는 이유: 내 폰이 사라지면
	// 부활 여지·입력 바인딩·HUD 의 폰 캐시가 전부 끊긴다 (Task 27 의 폰 유지 결정과 충돌).
	const UCA3DRuleSet* Rules = ResolveRules(GetWorld());
	SetViewTargetWithBlend(TargetPawn, Rules->SpectateBlendTime, VTBlend_Cubic);

	UE_LOG(LogCA3D, Log, TEXT("ACA3DPlayerController: 관전 대상 → %s"), *NewTarget->GetPlayerName());
}

void ACA3DPlayerController::CycleSpectateTarget(int32 Step)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5
	if (IsMatchEnded()) return;             // 결과 화면이 떠 있는데 카메라가 돌아다니면 안 된다

	TArray<ACA3DPlayerState*> Candidates;
	CollectSpectateCandidates(Candidates);
	if (Candidates.Num() == 0)
	{
		// 생존자가 하나도 없다 (전멸·무승부 직전) — 마지막 대상을 그대로 둔다.
		// 여기서 시점을 되돌리면 매치의 마지막 순간에 화면이 한 번 튄다.
		return;
	}

	const int32 Num = Candidates.Num();
	const int32 Current = Candidates.IndexOfByKey(SpectateTarget.Get());

	// 현재 대상이 목록에 없으면(방금 죽었거나 아직 안 정해짐) 방향과 무관하게 첫 항목부터.
	// 이래야 "대상이 죽어 자동으로 넘어가는" 경로가 매번 같은 결과를 낸다.
	const int32 NextIndex = (Current == INDEX_NONE)
		? 0
		: ((Current + Step) % Num + Num) % Num; // 음수 나머지 보정 — 좌(-1) 순환도 같은 식으로

	SetSpectateTarget(Candidates[NextIndex]);
}

void ACA3DPlayerController::HandleSpectateAxis(float AxisX)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 (로컬 입력이라 실도달 없음 — 명시 가드)

	const UCA3DRuleSet* Rules = ResolveRules(GetWorld());
	if (FMath::Abs(AxisX) < Rules->SpectateCycleAxisThreshold)
	{
		bSpectateAxisLatched = false; // 손을 뗐다 — 다음 입력을 받을 준비
		return;
	}
	if (bSpectateAxisLatched)
	{
		return; // 누르고 있는 동안은 한 번만 — 없으면 한 번 눌러 전원을 지나친다
	}

	bSpectateAxisLatched = true;
	CycleSpectateTarget(AxisX > 0.f ? 1 : -1);
}

void ACA3DPlayerController::UpdateSpectateView()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시점은 시각 전용

	// 매치가 끝나면 시점을 고정한다 — 결과 화면이 떠 있는 동안 카메라가 옮겨 다니면
	// "누가 이겼는지" 를 읽는 중에 화면이 흔들린다.
	if (IsMatchEnded()) return;

	if (!IsLocalPawnDead())
	{
		// 살아 있다(또는 부활했다). 관전 중이었다면 내 폰으로 되돌린다 — 부활 기능이
		// 들어와도 시점 복귀를 따로 배선할 필요가 없다.
		if (SpectateTarget)
		{
			SpectateTarget = nullptr;
			bSpectateAxisLatched = false;
			if (APawn* OwnPawn = GetPawn())
			{
				SetViewTargetWithBlend(OwnPawn, ResolveRules(GetWorld())->SpectateBlendTime, VTBlend_Cubic);
			}
		}
		return;
	}

	// 대상이 없거나(방금 죽었다) 대상이 죽었으면 다음 생존자로 자동 전환.
	// 이걸 안 하면 관전 중 대상이 죽는 순간 다시 빈 바닥을 보게 된다.
	if (!IsValid(SpectateTarget) || !SpectateTarget->bAlive || !IsValid(SpectateTarget->GetPawn()))
	{
		CycleSpectateTarget(1);
	}
}
