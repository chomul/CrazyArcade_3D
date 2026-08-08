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

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
