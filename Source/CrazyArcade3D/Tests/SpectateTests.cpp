// 사망 후 관전(생존자 추적·순환) 자동화 테스트.
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.Spectate"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 대상 선정(자기 자신 제외·생존자만), 결정론적 순환,
// 대상 사망 시 자동 전환, 생존자 0명일 때 마지막 대상 유지, 매치 종료 후 시점 고정,
// 살아 있는 동안 좌/우가 관전으로 해석되지 않음(생존 대조군), 눌림 래치.
// 실제 카메라 연출(블렌드 체감·데디 클라 시점)은 PIE·실전 실행에서 본다.
//
// DeathHandlingTests 는 "사망이 폰에 무엇을 하는가" 담당이라 분리했다 — 여기는
// "죽은 뒤 무엇을 보는가" 만 본다.
//
// 월드는 CA3DPlayerStateTests 관례대로 GameInstance 표준 초기화 → SetGameMode →
// InitializeActorsForPlay 로 만든다 (GameState·PlayerArray 가 있어야 대상 목록이 성립).
// World->BeginPlay() 는 부르지 않는다 — 지형·CMC 튜닝은 관전과 무관하다.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Spec~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Core/CameraYawSnap.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputActionValue.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpectateTest, "CrazyArcade3D.Gameplay.Spectate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// GameMode 가 사망 해소에서 하는 일(bAlive 내리기)을 손으로 흉내 낸다.
	// 실제 경로(NotifyPlayerDeath → 다음 틱 ResolvePendingDeaths)는 CA3DPlayerStateTests 담당이고,
	// 여기서는 "생존 미러가 false 가 된 순간 관전이 어떻게 반응하는가" 만 본다.
	void SpecKill(ACA3DPlayerState* State)
	{
		if (!State)
		{
			return;
		}
		State->bAlive = false;
		if (ACA3DCharacter* Character = Cast<ACA3DCharacter>(State->GetPawn()))
		{
			if (UStatusComponent* Status = Character->GetStatus())
			{
				Status->ServerKill(EDeathCause::Fall); // 폰 쪽 사망 상태도 실제 경로로 적용
			}
		}
	}

	// 이동 입력 한 번 — Enhanced Input 의 Axis2D 값을 그대로 흉내 낸다 (X=좌우, Y=앞뒤).
	FInputActionValue SpecAxis(float X, float Y)
	{
		return FInputActionValue(FVector2D(X, Y));
	}
}

bool FSpectateTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성 ───
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();

	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("GameInstance 월드 생성"), World))
	{
		return false;
	}

	World->GetWorldSettings()->DefaultGameMode = ACA3DGameMode::StaticClass();

	FURL URL;
	World->SetGameMode(URL);
	World->InitializeActorsForPlay(URL); // GameState 는 이 이후에야 존재한다 (CA3DPlayerStateTests 주석)

	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("GameState 가 ACA3DGameState"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// 블렌드 0 주입 — 블렌드가 있으면 카메라 매니저가 PendingViewTarget 으로 들고 있다가
	// 카메라 틱에서야 확정한다. 이 테스트는 카메라 연출이 아니라 **대상 선정 규칙**을 보므로
	// 시점 전환이 그 자리에서 확정되게 만든다.
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GameState);
	Rules->SpectateBlendTime = 0.f;
	GameState->Rules = Rules;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// ─── 로컬 컨트롤러 + 참가자 4명 (나 + A/B/C) ───
	// PlayerArray 등록 순서 = 스폰 순서라 순환 순서가 결정론적이다 (그 자체가 검증 대상).
	ACA3DPlayerController* PC = World->SpawnActor<ACA3DPlayerController>(
		ACA3DPlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DPlayerController 스폰"), PC))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// 참가자 한 명 = PlayerState + 폰. 실제 접속(PostLogin)은 헤드리스에서 재현하지 않고
	// 관전이 읽는 것(PlayerArray · bAlive · PlayerState↔Pawn 연결)만 손으로 구성한다.
	auto SpawnParticipant = [&](const TCHAR* Name) -> ACA3DPlayerState*
	{
		ACA3DPlayerState* State = World->SpawnActor<ACA3DPlayerState>(); // PostInitializeComponents 가 PlayerArray 에 등록
		ACA3DCharacter* Pawn = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (State && Pawn)
		{
			State->SetPlayerName(Name);
			Pawn->SetPlayerState(State); // APlayerState::GetPawn 의 역참조를 채운다
		}
		return State;
	};

	ACA3DPlayerState* OwnState = PC->GetPlayerState<ACA3DPlayerState>();
	ACA3DCharacter* OwnPawn = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!OwnState)
	{
		// PostLogin 없이 스폰된 컨트롤러는 PlayerState 가 없을 수 있다 — 직접 채운다.
		OwnState = World->SpawnActor<ACA3DPlayerState>();
		PC->SetPlayerState(OwnState);
	}
	if (!TestNotNull(TEXT("로컬 PlayerState 확보"), OwnState)
		|| !TestNotNull(TEXT("로컬 폰 스폰"), OwnPawn))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	OwnState->SetPlayerName(TEXT("Me"));
	OwnPawn->SetPlayerState(OwnState);
	PC->SetPawn(OwnPawn); // Possess 는 입력 시스템까지 깨우므로 폰 연결만 한다 (관전이 보는 것은 GetPawn 뿐)

	ACA3DPlayerState* A = SpawnParticipant(TEXT("A"));
	ACA3DPlayerState* B = SpawnParticipant(TEXT("B"));
	ACA3DPlayerState* C = SpawnParticipant(TEXT("C"));
	UStatusComponent* OwnStatus = OwnPawn->GetStatus();
	if (!TestNotNull(TEXT("참가자 A"), A) || !TestNotNull(TEXT("참가자 B"), B)
		|| !TestNotNull(TEXT("참가자 C"), C) || !TestNotNull(TEXT("로컬 StatusComponent"), OwnStatus))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// ─── 1. 대상 목록 — 자기 자신 제외, 생존자만, PlayerArray 순서 ───
	TArray<ACA3DPlayerState*> Candidates;
	PC->CollectSpectateCandidates(Candidates);
	TestEqual(TEXT("① 후보 3명 (나를 뺀 전원)"), Candidates.Num(), 3);
	TestFalse(TEXT("① 자기 자신은 관전 대상에서 제외"), Candidates.Contains(OwnState));
	if (Candidates.Num() == 3)
	{
		TestTrue(TEXT("① 후보 순서 = PlayerArray 순서 (A, B, C)"),
			Candidates[0] == A && Candidates[1] == B && Candidates[2] == C);
	}

	// ─── 2. 생존 대조군 — 살아 있으면 좌/우가 관전 전환이 아니다 ───
	PC->UpdateSpectateView();
	TestNull(TEXT("② 생존 중: 관전 대상 없음"), PC->SpectateTarget.Get());

	UCharacterMovementComponent* OwnMovement = OwnPawn->GetCharacterMovement();
	OwnMovement->ConsumeInputVector();
	PC->OnMove(SpecAxis(1.f, 0.f)); // 오른쪽 — 살아 있으면 그냥 이동이어야 한다
	TestNull(TEXT("② 생존 중 우측 입력: 관전 전환 없음"), PC->SpectateTarget.Get());
	TestFalse(TEXT("② 생존 중 우측 입력: 이동 입력이 캐릭터로 전달됨"),
		OwnMovement->GetPendingInputVector().IsNearlyZero());
	OwnMovement->ConsumeInputVector();

	// ─── 3. 사망 → 살아 있는 다른 참가자로 시점이 옮겨간다 ───
	OwnStatus->ServerKill(EDeathCause::Fall);
	TestEqual(TEXT("③ 사망: LifeState == Dead"), OwnStatus->LifeState, ELifeState::Dead);
	TestTrue(TEXT("③ 폰은 그대로 살아 있다 (파괴·UnPossess 없음)"),
		IsValid(OwnPawn) && PC->GetPawn() == OwnPawn);

	PC->UpdateSpectateView();
	TestTrue(TEXT("③ 사망 직후: 첫 생존자(A)로 시점 이동"), PC->SpectateTarget == A);
	if (PC->PlayerCameraManager)
	{
		TestTrue(TEXT("③ ViewTarget 이 A 의 폰 (SetViewTargetWithBlend)"),
			PC->GetViewTarget() == A->GetPawn());
	}

	// ─── 4. 순환 — 결정론적 순서로 돈다 (다음/이전, 양쪽 랩) ───
	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("④ 다음: A → B"), PC->SpectateTarget == B);
	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("④ 다음: B → C"), PC->SpectateTarget == C);
	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("④ 다음: C → A (끝에서 처음으로 랩)"), PC->SpectateTarget == A);
	PC->CycleSpectateTarget(-1);
	TestTrue(TEXT("④ 이전: A → C (처음에서 끝으로 랩)"), PC->SpectateTarget == C);
	PC->CycleSpectateTarget(-1);
	TestTrue(TEXT("④ 이전: C → B"), PC->SpectateTarget == B);

	// ─── 5. 입력 경로 — 사망 중 좌/우가 전환이고, 누르고 있는 동안은 한 번만 ───
	PC->SetSpectateTarget(A);
	OwnMovement->ConsumeInputVector();

	PC->OnMove(SpecAxis(1.f, 0.f));
	TestTrue(TEXT("⑤ 사망 중 우측 입력: A → B"), PC->SpectateTarget == B);
	TestTrue(TEXT("⑤ 사망 중 입력은 이동으로 전달되지 않는다"),
		OwnMovement->GetPendingInputVector().IsNearlyZero());

	PC->OnMove(SpecAxis(1.f, 0.f)); // 계속 누르고 있는 프레임 — 래치가 막는다
	TestTrue(TEXT("⑤ 누르고 있는 동안은 한 번만 (B 유지)"), PC->SpectateTarget == B);

	PC->OnMoveCompleted(); // 키를 뗐다
	PC->OnMove(SpecAxis(-1.f, 0.f));
	TestTrue(TEXT("⑤ 다시 누른 좌측 입력: B → A"), PC->SpectateTarget == A);

	// 임계값 미만(대각의 약한 축 등)은 전환으로 세지 않는다.
	PC->OnMoveCompleted();
	PC->OnMove(SpecAxis(0.2f, 1.f));
	TestTrue(TEXT("⑤ 임계값 미만 축: 전환 없음 (A 유지)"), PC->SpectateTarget == A);

	// ─── 6. 관전 대상이 죽으면 자동으로 다음 생존자로 ───
	SpecKill(A);
	PC->UpdateSpectateView();
	TestTrue(TEXT("⑥ 대상 A 사망 → 다음 생존자 B 로 자동 전환"), PC->SpectateTarget == B);

	// 남은 후보가 하나뿐이면 순환해도 제자리 (크래시·널 대상 없음).
	SpecKill(C);
	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("⑥ 생존자 1명: 순환해도 B 유지"), PC->SpectateTarget == B);

	// ─── 7. 생존자 0명 — 마지막 대상 유지 (무승부·전멸) ───
	SpecKill(B);
	PC->CollectSpectateCandidates(Candidates);
	TestEqual(TEXT("⑦ 생존자 0명"), Candidates.Num(), 0);
	PC->UpdateSpectateView();
	TestTrue(TEXT("⑦ 생존자 0명: 마지막 대상(B) 유지 — 시점이 튀지 않는다"), PC->SpectateTarget == B);
	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("⑦ 생존자 0명: 순환 입력도 대상을 바꾸지 않는다"), PC->SpectateTarget == B);

	// ─── 8. 매치 종료 후에는 시점을 바꾸지 않는다 ───
	// 생존자를 되살려 "바꿀 대상이 있는데도 안 바꾼다" 를 확인한다 (없어서 안 바뀌는 게 아니다).
	C->bAlive = true;
	GameState->bMatchEnded = true;

	PC->CycleSpectateTarget(1);
	TestTrue(TEXT("⑧ 종료 후 순환 입력: 대상 불변 (B)"), PC->SpectateTarget == B);
	PC->UpdateSpectateView();
	TestTrue(TEXT("⑧ 종료 후 자동 전환도 없음 (B)"), PC->SpectateTarget == B);
	PC->OnMoveCompleted();
	PC->OnMove(SpecAxis(1.f, 0.f));
	TestTrue(TEXT("⑧ 종료 후 좌/우 입력: 대상 불변 (B)"), PC->SpectateTarget == B);

	// ─── 9. 스냅 공식 — 인덱스 ↔ 각도 왕복이 모든 칸에서 정확 ───
	//
	// 이 왕복이 깨지면 "내가 보는 각"과 "관전자가 나를 볼 때의 각"이 조용히 어긋난다.
	// 공식이 CameraYawSnap 한 곳에만 있다는 것이 이 검증의 전제다 — 컨트롤러가 사본을
	// 들고 있던 시절에는 이 테스트가 한쪽만 검증하는 셈이었다.
	for (int32 Step = 0; Step < CameraYawSnap::NumSteps; ++Step)
	{
		const uint8 Index = static_cast<uint8>(Step);
		const float Deg = CameraYawSnap::IndexToYawDeg(Index);

		TestEqual(TEXT("⑨ 인덱스 → 각도 → 인덱스 왕복"),
			static_cast<int32>(CameraYawSnap::YawDegToIndex(Deg)), Step);
		// 스텝 크기(90도)를 여기 상수로 박지 않는다 — 값이 바뀌면 테스트가 규칙을 검증하는 게
		// 아니라 옛 값을 지키게 된다. 검증할 것은 "인덱스 × 스텝" 이라는 관계다.
		TestEqual(TEXT("⑨ 각도는 스텝 배수"), Deg, Step * CameraYawSnap::StepDeg);

		// 한 바퀴 더 돌아온 각도도 같은 칸이어야 한다 (컨트롤러는 스텝을 랩하지 않는다).
		TestEqual(TEXT("⑨ +360도 랩"),
			static_cast<int32>(CameraYawSnap::YawDegToIndex(Deg + 360.f)), Step);
		TestEqual(TEXT("⑨ -360도 랩"),
			static_cast<int32>(CameraYawSnap::YawDegToIndex(Deg - 360.f)), Step);

		// 누적 스텝 → 인덱스: 양쪽으로 여러 바퀴 돌아도 같은 칸 (Q 를 계속 눌러 음수가 된다).
		TestEqual(TEXT("⑨ 스텝 → 인덱스"), static_cast<int32>(CameraYawSnap::StepsToIndex(Step)), Step);
		TestEqual(TEXT("⑨ 스텝 → 인덱스 (양수 랩)"),
			static_cast<int32>(CameraYawSnap::StepsToIndex(Step + CameraYawSnap::NumSteps * 5)), Step);
		TestEqual(TEXT("⑨ 스텝 → 인덱스 (음수 랩)"),
			static_cast<int32>(CameraYawSnap::StepsToIndex(Step - CameraYawSnap::NumSteps * 3)), Step);
	}

	// ─── 10. 히스테리시스 — 경계에서 흔들려도 카메라가 한 칸씩 튀지 않는다 ───
	//
	// 봇의 이동 방향은 연속 값이라 칸 경계(StepDeg/2)에 걸친 채 미세하게 떨 수 있다.
	// 히스테리시스가 없으면 그 떨림이 그대로 카메라 한 칸 왕복이 된다.
	const float Hysteresis = Rules->SpectateBotCamYawHysteresisDeg;
	const float Boundary = CameraYawSnap::StepDeg * 0.5f; // 칸 경계 — 스텝 크기가 바뀌면 같이 따라간다

	// 먼저 "히스테리시스가 없으면 실제로 넘어간다" 를 확인한다 — 이걸 안 보면 아래 검증이
	// 그냥 무딘 임계값 덕분인지 히스테리시스 덕분인지 알 수 없다.
	TestEqual(TEXT("⑩ 히스테리시스 0: 경계 바로 너머는 다음 칸으로"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(Boundary + 0.1f, 0, 0.f)), 1);
	TestEqual(TEXT("⑩ 히스테리시스 0: 경계 바로 앞은 제자리"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(Boundary - 0.1f, 0, 0.f)), 0);

	// 같은 각을 경계 너머로 미세하게 왕복시킨다 — 인덱스가 한 번도 바뀌면 안 된다.
	// 흔들림 폭(±1.5도)이 히스테리시스보다 작다는 것이 이 검증의 전제다.
	uint8 WobbleIndex = 0;
	const float WobbleSamples[] = { -0.1f, 0.1f, 0.9f, -0.7f, 0.f, 1.5f, -0.5f };
	for (const float Offset : WobbleSamples)
	{
		WobbleIndex = CameraYawSnap::YawDegToIndexWithHysteresis(Boundary + Offset, WobbleIndex, Hysteresis);
	}
	TestEqual(TEXT("⑩ 경계 왕복: 인덱스 불변 (카메라가 안 튄다)"), static_cast<int32>(WobbleIndex), 0);

	// 그렇다고 붙잡고 있으면 안 된다 — 봇이 확실히 방향을 틀면 따라가야 한다.
	TestEqual(TEXT("⑩ 한 칸 확실히 이동: 따라간다"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(CameraYawSnap::StepDeg, 0, Hysteresis)), 1);
	TestEqual(TEXT("⑩ 반대로 한 칸: 마지막 칸으로"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(360.f - CameraYawSnap::StepDeg, 0, Hysteresis)),
		CameraYawSnap::NumSteps - 1);

	// 0 ↔ 마지막 칸 경계도 ±180 랩을 넘어 이어져야 한다 (352도는 0번 칸에서 8도 거리다).
	TestEqual(TEXT("⑩ 0/마지막 칸 경계 랩: 352도는 0번 칸 유지"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(352.f, 0, Hysteresis)), 0);

	// 룰셋 값이 비상식적으로 커도 한 칸 이동은 반드시 통과해야 한다 (클램프 확인) —
	// 안 그러면 카메라가 특정 각에 영원히 붙는다.
	TestEqual(TEXT("⑩ 히스테리시스 과다(한 칸 전체)여도 한 칸 이동은 통과"),
		static_cast<int32>(CameraYawSnap::YawDegToIndexWithHysteresis(
			CameraYawSnap::StepDeg, 0, CameraYawSnap::StepDeg)), 1);

	// ─── 11. GetViewRotation — "누가 보고 있는가" 로 갈린다 ───
	//
	// 2026-08-09 개정: 각의 주인이 **보는 사람**이다. 같은 함수가 상황에 따라 다른 답을 낸다:
	//   · 로컬 컨트롤러가 보고 있는 폰(조종 중이든 관전 중이든) → 그 컨트롤러의 ControlRotation
	//   · 아무도 로컬에서 안 보는 폰(봇·원격)                   → 복제된 스냅 인덱스 = 이 폰의 표현
	// 두 번째가 첫 번째의 **시작각**이 된다 (⑬).
	const uint8 BotIndex = static_cast<uint8>(CameraYawSnap::NumSteps - 1); // 마지막 칸
	ACA3DCharacter* BotPawn = Cast<ACA3DCharacter>(C->GetPawn());
	if (TestNotNull(TEXT("⑪ 봇 역할 폰 확보"), BotPawn))
	{
		C->CamYawIndex = BotIndex; // 서버(ABotController)가 써 넣었을 값

		// FRotator 는 UE5 에서 double 기반이라 float 룰셋 값과 비교하려면 명시 캐스트가 필요하다
		// (섞어 쓰면 TestEqual 오버로드가 모호해진다).
		const FRotator BotView = BotPawn->GetViewRotation();
		TestEqual(TEXT("⑪ 아무도 안 보는 봇 폰: yaw = 복제된 스냅 인덱스"),
			static_cast<float>(BotView.Yaw), CameraYawSnap::IndexToYawDeg(BotIndex));
		TestEqual(TEXT("⑪ 아무도 안 보는 봇 폰: pitch = 룰셋 고정 내려보기 각 (지면에 눕지 않는다)"),
			static_cast<float>(BotView.Pitch), Rules->CameraPitchDeg);
	}

	// 로컬 컨트롤러가 보고 있는 폰은 그 컨트롤러의 각을 쓴다 — 복제 인덱스를 일부러 다르게
	// 세워 두고, 그래도 컨트롤러 값이 나오는지 본다.
	PC->SetAsLocalPlayerController(); // 테스트 월드에는 ULocalPlayer 가 없어 명시로 세운다
	OwnPawn->Controller = PC;         // Possess 는 입력 시스템까지 깨우므로 연결만 (위 SetPawn 관례)
	const FRotator LocalControlRotation(Rules->CameraPitchDeg, CameraYawSnap::StepDeg, 0.f);
	PC->SetControlRotation(LocalControlRotation);
	OwnState->CamYawIndex = BotIndex; // 복제 값은 다른 칸 — 여기 끌려가면 안 된다

	TestEqual(TEXT("⑪ 로컬 컨트롤러가 보는 폰: ControlRotation 을 그대로 쓴다"),
		static_cast<float>(OwnPawn->GetViewRotation().Yaw),
		static_cast<float>(LocalControlRotation.Yaw));
	TestTrue(TEXT("⑪ 로컬 컨트롤러가 보는 폰: 복제 인덱스에 끌려가지 않는다"),
		!FMath::IsNearlyEqual(static_cast<float>(OwnPawn->GetViewRotation().Yaw),
			CameraYawSnap::IndexToYawDeg(BotIndex)));

	// **관전 중 회전이 먹는가** — 관전 대상 폰이 관전자의 각을 따라야 한다 (2026-08-09 사용자 요청).
	// ViewTarget 배선은 PlayerCameraManager 가 있어야 성립하는데 테스트 월드에는 없을 수 있다.
	// 없으면 조용히 건너뛴다 — 여기서 실패하면 "관전 규칙" 이 아니라 "테스트 월드 구성" 을
	// 검증하게 된다 (그 경우는 `-game` 실전이 잡는다).
	if (BotPawn && PC->GetViewTarget() == BotPawn)
	{
		const FRotator SpectateRotation(Rules->CameraPitchDeg, CameraYawSnap::StepDeg * 2.f, 0.f);
		PC->SetControlRotation(SpectateRotation);
		TestEqual(TEXT("⑪ 관전 중인 남의 폰: 관전자의 ControlRotation 을 따른다 (Q/E 가 먹는다)"),
			static_cast<float>(BotPawn->GetViewRotation().Yaw),
			static_cast<float>(SpectateRotation.Yaw));
	}

	// ─── 12. 서버 RPC 는 스냅이 **바뀔 때만** 나간다 ───
	//
	// 이 테스트 월드는 스탠드얼론(권한 있음)이라 Server RPC 가 곧바로 _Implementation 으로
	// 들어간다 — 그래서 "PlayerState 값이 덮였는가"가 곧 "RPC 가 나갔는가"다.
	// 표식(99)을 심어 두고, 덮이지 않으면 RPC 가 없었던 것으로 센다.
	constexpr uint8 RpcSentinel = 99;

	OwnState->CamYawIndex = RpcSentinel;
	PC->PushCamYawIndex();
	PC->PushCamYawIndex();
	PC->PushCamYawIndex();
	TestEqual(TEXT("⑫ 회전 입력이 없으면 RPC 가 한 번도 나가지 않는다"),
		static_cast<int32>(OwnState->CamYawIndex), static_cast<int32>(RpcSentinel));

	PC->OnRotateCam(FInputActionValue(1.f)); // E — 한 칸 오른쪽
	PC->PushCamYawIndex();
	TestEqual(TEXT("⑫ 스냅이 바뀐 순간에만 RPC — 서버 PlayerState 갱신"),
		static_cast<int32>(OwnState->CamYawIndex), static_cast<int32>(PC->GetCamYawIndex()));
	TestEqual(TEXT("⑫ 갱신된 값이 실제 스냅 인덱스(1)"),
		static_cast<int32>(OwnState->CamYawIndex), 1);

	OwnState->CamYawIndex = RpcSentinel; // 다시 표식 — 같은 각이면 재전송이 없어야 한다
	PC->PushCamYawIndex();
	PC->PushCamYawIndex();
	TestEqual(TEXT("⑫ 같은 각을 유지하는 동안은 재전송 없음 (매 틱 보내지 않는다)"),
		static_cast<int32>(OwnState->CamYawIndex), static_cast<int32>(RpcSentinel));

	// 한 바퀴 돌아 같은 칸으로 돌아오면 인덱스가 같으므로 그동안 나간 RPC 는 매 칸 1번씩이다 —
	// 마지막 한 칸에서 다시 0번 칸이 되는 것만 확인한다.
	for (int32 Turn = 0; Turn < CameraYawSnap::NumSteps - 1; ++Turn)
	{
		PC->OnRotateCam(FInputActionValue(1.f));
		PC->PushCamYawIndex();
	}
	TestEqual(TEXT("⑫ 한 바퀴를 돌면 0번 칸으로 복귀"),
		static_cast<int32>(OwnState->CamYawIndex), 0);

	// ─── 13. 관전 대상을 바꾸면 **그 사람이 보던 각**을 시작각으로 받아 온다 ───
	//
	// "다른 플레이어가 보는 시점 그대로" 는 여기서 성립한다 (2026-08-09 사용자 요청).
	// 받은 뒤부터는 관전자 것이라 Q/E 로 자유롭게 돌린다 — ⑪ 의 마지막 검증이 그 절반이고,
	// 이 검증이 나머지 절반(시작각을 실제로 받아 오는가)이다.
	{
		// 관전자의 현재 각과 **다른** 칸을 대상에 심는다 — 안 그러면 우연히 같아서 통과한다.
		const uint8 BeforeIndex = PC->GetCamYawIndex();
		const uint8 TargetIndex = CameraYawSnap::StepsToIndex(static_cast<int32>(BeforeIndex) + 1);
		B->CamYawIndex = TargetIndex;

		PC->SetSpectateTarget(B);

		TestEqual(TEXT("⑬ 관전 시작각 = 대상의 복제된 스냅 인덱스"),
			static_cast<int32>(PC->GetCamYawIndex()), static_cast<int32>(TargetIndex));

		// 이어서 Q/E 를 누르면 **그 각을 기준으로** 한 칸 더 돈다 (받아온 값 위에 쌓인다).
		PC->OnRotateCam(FInputActionValue(1.f));
		TestEqual(TEXT("⑬ 받아온 각에서 이어서 회전한다"),
			static_cast<int32>(PC->GetCamYawIndex()),
			static_cast<int32>(CameraYawSnap::StepsToIndex(static_cast<int32>(TargetIndex) + 1)));
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
