// 사망 처리·관전 자동화 테스트 (Task 27).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.DeathHandling"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 사망 시 캡슐 컬리전·MovementMode·메시 가시성,
// Move/DoJump 생존 가드(생존 대조군 포함), 폭탄 설치 거부 회귀, 중복 ServerKill 무시,
// 갇힘 중 사망 시 타이머 잔존 없음, Trapped 미세 이동 유지, 클라 OnRep 경로.
// 실제 리플리케이션(Listen+클라)·관전 카메라 체감·데디 시각 생략은 PIE 검증 (체크리스트 27).
//
// StatusComponentTests 는 "상태 전이·스탯" 담당이라 분리했다 — 여기는 "사망이 폰에 무엇을
// 하는가" 만 본다. 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다: 그리드는 friend 로
// 손구성, CMC 튜닝은 TryApplyMovementTuning 직접 호출 (기존 테스트 관례).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h" // DeathHideDelay 폴백 검증 — CDO 를 잠시 바꾼다 (Task 38)
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeathHandlingTest, "CrazyArcade3D.Gameplay.DeathHandling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 번역 단위 병합(build.md): 테스트 .cpp 들이 한 번역 단위로 합쳐지면 무명 네임스페이스가
	// 병합되어 동명 헬퍼가 C2084 로 충돌한다 — 로컬 헬퍼는 모듈 전체에서 고유한 이름으로 (Dh 접두사).
	int32 DhCountBombs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ABomb> It(World); It; ++It) // 파괴 대기 액터는 이터레이터가 건너뛴다
		{
			++Count;
		}
		return Count;
	}

	// 캡슐 바닥이 셀 (X,Y,Z) 중앙에 오도록 배치하는 좌표 (CellSize 100, 엔진 기본 반높이 88).
	FVector DhLocForFootCell(int32 X, int32 Y, int32 Z)
	{
		constexpr float HalfHeight = 88.f;
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	}
}

bool FDeathHandlingTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 룰셋은 기본(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	// ServerPlaceBomb 은 UFUNCTION RPC → AActor::ProcessEvent 를 타는데, ProcessEvent 는
	// AreActorsInitialized()==false 인 월드에서 이벤트를 조용히 버린다 (BombTests 주석).
	World->InitializeActorsForPlay(FURL());

	// ── 손그리드 5×5×4 — z=0 전면 Floor 바닥판, 그 위는 Empty (설치 가능 셀) ──
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	VoxelWorld->Grid.Init(FIntVector(5, 5, 4));
	for (int32 X = 0; X < 5; ++X)
	{
		for (int32 Y = 0; Y < 5; ++Y)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	VoxelWorld->bGridInitialized = true;

	if (!TestNotNull(TEXT("UExplosionSubsystem 존재"), World->GetSubsystem<UExplosionSubsystem>()))
	{
		World->DestroyWorld(false);
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DCharacter 스폰"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}
	Character->TryApplyMovementTuning(); // VoxelWorld 캐시 — 설치 경로의 전제 (friend)

	UStatusComponent* Status = Character->GetStatus();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	USkeletalMeshComponent* CharMesh = Character->GetMesh();
	if (!TestNotNull(TEXT("StatusComponent 부착"), Status)
		|| !TestNotNull(TEXT("CMC 존재"), Movement)
		|| !TestNotNull(TEXT("캡슐 존재"), Capsule)
		|| !TestNotNull(TEXT("스켈레탈 메시 컴포넌트 존재"), CharMesh))
	{
		World->DestroyWorld(false);
		return false;
	}

	Movement->SetMovementMode(MOVE_Walking); // 테스트 월드는 바닥 컬리전이 없어 모드를 명시 (CA3DCharacterTests 관례)
	Character->SetActorLocation(DhLocForFootCell(2, 2, 1), false, nullptr, ETeleportType::TeleportPhysics);

	// ─── 1. 생존 대조군 — 사망 후 "안 먹는다"가 의미를 가지려면 살아서는 먹어야 한다 ───
	Movement->ConsumeInputVector(); // 누적 초기화
	Character->Move(FVector2D(1.f, 0.f));
	TestFalse(TEXT("① 생존: Move 가 이동 입력을 누적"), Movement->GetPendingInputVector().IsNearlyZero());
	Movement->ConsumeInputVector();

	Character->DoJump();
	TestTrue(TEXT("① 생존: DoJump 가 점프 입력을 세움"), Character->bPressedJump);
	Character->StopJumping();

	TestTrue(TEXT("① 생존: 캡슐 컬리전 켜짐 (사망 전 기준선)"),
		Capsule->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	TestTrue(TEXT("① 생존: 메시 보임 (사망 전 기준선)"), CharMesh->GetVisibleFlag());

	// 폭탄 설치 대조군 — 슬롯을 2 로 늘려 두 번째 설치의 실패 사유가 '사망' 하나로 좁혀지게 한다.
	Status->MaxBombCount = 2;
	Character->ServerPlaceBomb(FIntVector(2, 2, 1));
	TestEqual(TEXT("① 생존: 폭탄 설치 성공 — 1개"), DhCountBombs(World), 1);

	// ─── 2. 사망 상태 적용 (ApplyDeathState 단일 지점) ───
	Status->ServerKill(EDeathCause::Fall);
	TestEqual(TEXT("② ServerKill: LifeState == Dead"), Status->LifeState, ELifeState::Dead);
	TestTrue(TEXT("② 캡슐 컬리전 off — 산 사람이 시체를 통과 (GDD 유령 방해 없음)"),
		Capsule->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	TestTrue(TEXT("② MovementMode == MOVE_None — 시체가 바닥을 뚫고 떨어지지 않는다(관전 시점 고정)"),
		Movement->MovementMode == MOVE_None);
	// 2026-08-14 (Task 38): "즉시 숨김" → "지연 숨김" 으로 갱신. 즉시 숨기면 AnimBP Dead
	// 상태의 Die 애님이 한 프레임도 안 보였다 — 사망 직후엔 보이는 채로 타이머만 걸려야 한다.
	// 2026-08-14 (Task 39): 숨김 시점 1순위가 캐릭터별 DeathMontage 실측 길이가 됐다.
	// 이 테스트는 PlayerState·캐릭터 정의가 없어(헤드리스는 재생도 0 반환) 몽타주 경로를 못
	// 타고 **DeathHideDelay 폴백**으로 떨어진다 — 아래 단언이 곧 폴백 경로의 스펙이다.
	TestFalse(TEXT("② 사망 직후엔 아직 보인다 — Die 애님 재생 구간 (지연 숨김)"), Character->IsHidden());
	TestTrue(TEXT("② 숨김 타이머 가동 (DeathHideDelay 뒤 HideAfterDeath)"),
		World->GetTimerManager().IsTimerActive(Character->DeathHideTimerHandle));
	// 타이머 만료 지점을 직접 호출 — 지연 후 도달하는 코드가 액터 단위 숨김인 것을 확인한다.
	// 액터 단위인 이유: 메시 컴포넌트 하나만 끄면 BP 가 추가한 메시가 남는다 (실제 PIE 버그였다).
	Character->HideAfterDeath();
	TestTrue(TEXT("② HideAfterDeath: 액터 숨김 (시각 — 데디 가드 안쪽, BP 추가 메시까지 포함)"),
		Character->IsHidden());
	TestTrue(TEXT("② 폰은 살아 있다 — 파괴·풀 반납 없음 (서버 권한 상태 액터 풀링 금지)"),
		IsValid(Character));

	// ─── 3. 사망 후 이동·점프 무시 (ACA3DCharacter::Move/DoJump 생존 가드) ───
	Movement->ConsumeInputVector();
	const FVector DeathLocation = Character->GetActorLocation();
	const FVector DeathVelocity = Movement->Velocity;

	Character->Move(FVector2D(1.f, 0.f));
	Character->DoJump();

	TestTrue(TEXT("③ 사망 후 Move: 이동 입력 누적 없음"), Movement->GetPendingInputVector().IsNearlyZero());
	TestFalse(TEXT("③ 사망 후 DoJump: 점프 입력 없음"), Character->bPressedJump);
	TestTrue(TEXT("③ 사망 후 위치 불변"), Character->GetActorLocation().Equals(DeathLocation));
	TestTrue(TEXT("③ 사망 후 속도 불변"), Movement->Velocity.Equals(DeathVelocity));

	// ─── 4. 사망 후 폭탄 설치 거부 (기존 ServerPlaceBomb Alive 검증 회귀) ───
	Character->ServerPlaceBomb(FIntVector(3, 2, 1)); // 빈 셀 + 남은 슬롯 — 사망만이 거부 사유
	TestEqual(TEXT("④ 사망 후 설치 거부 — 폭탄 여전히 1개"), DhCountBombs(World), 1);

	// ─── 5. 중복 ServerKill 무시 ───
	// 표식: 숨김을 되돌리고 숨김 타이머도 지운 뒤 재-Kill 한다. 사망 처리가 다시 돌면
	// 타이머가 다시 걸리거나(지연 경로) 다시 숨겨진다(즉시 경로) — 둘 다 잡힌다.
	// (2026-08-14 Task 38: 지연 숨김 도입으로 "숨김 되돌리기" 하나로는 재실행이 안 잡혀 타이머 표식 추가)
	Character->SetActorHiddenInGame(false);
	World->GetTimerManager().ClearTimer(Character->DeathHideTimerHandle);
	Status->ServerKill(EDeathCause::Water);
	TestEqual(TEXT("⑤ 중복 Kill: LifeState 그대로 Dead"), Status->LifeState, ELifeState::Dead);
	TestFalse(TEXT("⑤ 중복 Kill: 사망 처리 재실행 없음 (숨김 표식 유지)"), Character->IsHidden());
	TestFalse(TEXT("⑤ 중복 Kill: 숨김 타이머 재예약 없음 (타이머 표식 유지)"),
		World->GetTimerManager().IsTimerActive(Character->DeathHideTimerHandle));
	TestTrue(TEXT("⑤ 중복 Kill: 위치 불변"), Character->GetActorLocation().Equals(DeathLocation));

	// ─── 6. 갇힘(Trapped): 미세 이동 유지 + 갇힌 채 사망 시 타이머 잔존 없음 ───
	ACA3DCharacter* Captive = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("갇힘 검증용 캐릭터 스폰"), Captive))
	{
		Captive->TryApplyMovementTuning();
		UStatusComponent* CaptiveStatus = Captive->GetStatus();
		UCharacterMovementComponent* CaptiveMovement = Captive->GetCharacterMovement();
		CaptiveMovement->SetMovementMode(MOVE_Walking);
		Captive->SetActorLocation(DhLocForFootCell(1, 1, 1), false, nullptr, ETeleportType::TeleportPhysics);

		CaptiveStatus->ServerTrap();
		TestEqual(TEXT("⑥ Trap: Trapped"), CaptiveStatus->LifeState, ELifeState::Trapped);
		TestTrue(TEXT("⑥ Trap: 만료 타이머 가동"),
			World->GetTimerManager().IsTimerActive(CaptiveStatus->TrappedTimer));

		// 갇힘은 "느려질 뿐" 이동 가능 (GDD 2.3) — 생존 가드가 실수로 여기까지 막으면 안 된다.
		CaptiveMovement->ConsumeInputVector();
		Captive->Move(FVector2D(0.f, 1.f));
		TestFalse(TEXT("⑥ Trapped: 미세 이동 입력이 여전히 먹는다"),
			CaptiveMovement->GetPendingInputVector().IsNearlyZero());
		TestTrue(TEXT("⑥ Trapped: 캡슐 컬리전 유지 (사망이 아니다)"),
			Captive->GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
		TestTrue(TEXT("⑥ Trapped: 이동 모드 유지"), CaptiveMovement->MovementMode == MOVE_Walking);

		// 이동과 달리 **점프는 막힌다** (2026-08-02 확정) — 갇힘의 의미가 사라지므로.
		Captive->bPressedJump = false;
		Captive->DoJump();
		TestFalse(TEXT("⑥ Trapped: 점프는 막힌다 (이동과 조건이 다르다)"), Captive->bPressedJump);

		// 갇힌 채 익사 — 타이머가 남으면 죽은 컴포넌트를 다시 부른다.
		CaptiveStatus->ServerKill(EDeathCause::Water);
		TestEqual(TEXT("⑦ 갇힘 중 사망: Dead"), CaptiveStatus->LifeState, ELifeState::Dead);
		TestFalse(TEXT("⑦ 갇힘 중 사망: 갇힘 타이머 잔존 없음"),
			World->GetTimerManager().IsTimerActive(CaptiveStatus->TrappedTimer));
		TestTrue(TEXT("⑦ 갇힘 중 사망: 컬리전 off"),
			Captive->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("⑦ 갇힘 중 사망: MOVE_None"), CaptiveMovement->MovementMode == MOVE_None);
	}

	// ─── 8. 클라 경로 — OnRep_Life(Dead) 도 서버와 같은 ApplyDeathState 를 통과한다 ───
	// 테스트 월드에는 넷드라이버가 없으므로 "복제 도착"을 손으로 흉내 낸다: LifeState 를
	// 서버가 보낸 값처럼 써넣고 OnRep 을 직접 호출한다 (friend).
	// 2026-08-14 (Task 38): 이 절은 DeathHideDelay <= 0 폴백(= 예전의 즉시 숨김)도 겸해서
	// 검증한다 — GameState 없는 테스트 월드는 룰셋이 CDO 폴백이라 CDO 값을 잠시 0 으로
	// 바꿨다가 복원한다 (다른 스위트가 같은 CDO 를 본다 — 복원을 빼먹으면 안 된다).
	ACA3DCharacter* Remote = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("클라 경로 검증용 캐릭터 스폰"), Remote))
	{
		Remote->TryApplyMovementTuning();
		UStatusComponent* RemoteStatus = Remote->GetStatus();
		UCharacterMovementComponent* RemoteMovement = Remote->GetCharacterMovement();
		RemoteMovement->SetMovementMode(MOVE_Walking);

		UCA3DRuleSet* MutableRules = GetMutableDefault<UCA3DRuleSet>();
		const float SavedHideDelay = MutableRules->DeathHideDelay;
		MutableRules->DeathHideDelay = 0.f; // 0 이하 = 즉시 숨김 (기존 동작 폴백)

		RemoteStatus->LifeState = ELifeState::Dead; // 서버 복제분이 도착했다고 가정
		RemoteStatus->OnRep_Life();

		MutableRules->DeathHideDelay = SavedHideDelay;

		TestTrue(TEXT("⑧ 클라 OnRep: 캡슐 컬리전 off (복제되지 않는 값 — 클라가 직접 적용)"),
			Remote->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("⑧ 클라 OnRep: MOVE_None (ReplicatedMovementMode 는 COND_SimulatedOnly — 본인에게 안 온다)"),
			RemoteMovement->MovementMode == MOVE_None);
		TestTrue(TEXT("⑧ 클라 OnRep: DeathHideDelay 0 → 타이머 없이 즉시 액터 숨김"), Remote->IsHidden());
		TestFalse(TEXT("⑧ 클라 OnRep: 즉시 경로는 숨김 타이머를 걸지 않는다"),
			World->GetTimerManager().IsTimerActive(Remote->DeathHideTimerHandle));
		TestTrue(TEXT("⑧ 클라 OnRep: 폰 유지 (관전 시점은 죽은 자리 그대로)"), IsValid(Remote));

		// 사망 후 입력 차단은 클라에서도 같은 캐릭터 가드가 담당한다.
		RemoteMovement->ConsumeInputVector();
		Remote->Move(FVector2D(1.f, 0.f));
		TestTrue(TEXT("⑧ 클라 OnRep: 사망 후 이동 입력 누적 없음"),
			RemoteMovement->GetPendingInputVector().IsNearlyZero());
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
