// UStatusComponent 자동화 테스트 (Task 12).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.StatusComponent"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 상태 전이(Alive→Trapped→Dead, Escape 경로),
// 룰셋 Cap 클램프, 권한 가드, 낙사 연결, 이동속도 재계산 단일 경로.
// 실제 리플리케이션(Listen+클라)·비주얼은 PIE 검증 (체크리스트 12).
//
// 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다 — friend 로 TryApplyMovementTuning 을
// 직접 호출한다 (CA3DCharacterTests 의 수동 배선 관례). 갇힘 만료는 월드 타이머 매니저를
// 수동 Tick 해 결정론적으로 진행시킨다.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Item/ItemTypes.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStatusComponentTest, "CrazyArcade3D.Gameplay.StatusComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStatusComponentTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 룰셋은 기본(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld);
	VoxelWorld->CellSize = 100.f;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DCharacter 스폰"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}

	UStatusComponent* Status = Character->GetStatus();
	if (!TestNotNull(TEXT("StatusComponent 부착 (생성자 CreateDefaultSubobject)"), Status))
	{
		World->DestroyWorld(false);
		return false;
	}

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	// CMC 튜닝 수동 적용 — BaseWalkSpeed·CachedRules 를 채워 속도 재계산 경로를 완성 (friend).
	Character->TryApplyMovementTuning();
	TestTrue(TEXT("CMC 튜닝 적용됨"), Character->bMovementTuningApplied);
	const float BaseSpeed = Rules->MoveSpeedCellsPerSec * VoxelWorld->CellSize;

	// ─── 1. 초기 상태 ───
	TestEqual(TEXT("초기 LifeState == Alive"), Status->LifeState, ELifeState::Alive);
	TestEqual(TEXT("초기 MaxBombCount == 1"), Status->MaxBombCount, 1);
	TestEqual(TEXT("초기 BombRange == 1"), Status->BombRange, 1);
	TestTrue(TEXT("초기 MoveSpeedMul == 1"), FMath::IsNearlyEqual(Status->MoveSpeedMul, 1.f));
	TestFalse(TEXT("초기 니들 없음"), Status->bHasNeedle);
	TestFalse(TEXT("초기 킥 없음"), Status->bHasKick);
	TestEqual(TEXT("초기 ActiveBombCount == 0"), Status->ActiveBombCount, 0);
	TestTrue(TEXT("초기 MaxWalkSpeed == 기본 속도 × 1"), FMath::IsNearlyEqual(Movement->MaxWalkSpeed, BaseSpeed, 0.01f));

	// ─── 2. 권한 가드 — 비권한(SimulatedProxy)에서 Server* 호출 시 상태 불변 (불변식 5) ───
	Character->SetRole(ROLE_SimulatedProxy);
	Status->ServerApplyItem(EItemType::Balloon);
	Status->ServerTrap();
	Status->ServerKill(EDeathCause::Water);
	TestEqual(TEXT("비권한: MaxBombCount 불변"), Status->MaxBombCount, 1);
	TestEqual(TEXT("비권한: LifeState 불변 (Alive)"), Status->LifeState, ELifeState::Alive);
	Character->SetRole(ROLE_Authority);

	// ─── 3. 아이템 적용 + Cap 클램프 (전부 룰셋 값 기준 — 매직 넘버 없음) ───
	Status->ServerApplyItem(EItemType::Balloon);
	TestEqual(TEXT("Balloon: MaxBombCount +1"), Status->MaxBombCount, 2);
	Status->ServerApplyItem(EItemType::Potion);
	TestEqual(TEXT("Potion: BombRange +1"), Status->BombRange, 2);

	for (int32 i = 0; i < Rules->MaxBombCountCap + 5; ++i)
	{
		Status->ServerApplyItem(EItemType::Balloon);
	}
	TestEqual(TEXT("Balloon 스팸: MaxBombCountCap 에서 클램프"), Status->MaxBombCount, Rules->MaxBombCountCap);

	for (int32 i = 0; i < Rules->MaxBombRangeCap + 5; ++i)
	{
		Status->ServerApplyItem(EItemType::Potion);
	}
	TestEqual(TEXT("Potion 스팸: MaxBombRangeCap 에서 클램프"), Status->BombRange, Rules->MaxBombRangeCap);

	Status->ServerApplyItem(EItemType::Roller);
	TestTrue(TEXT("Roller: MoveSpeedMul 증가 (룰셋 RollerSpeedStep)"),
		FMath::IsNearlyEqual(Status->MoveSpeedMul, 1.f + Rules->RollerSpeedStep, 0.001f));
	TestTrue(TEXT("Roller: MaxWalkSpeed = 기본 × 배율 (RefreshMoveSpeed 단일 경로)"),
		FMath::IsNearlyEqual(Movement->MaxWalkSpeed, BaseSpeed * Status->MoveSpeedMul, 0.01f));

	for (int32 i = 0; i < 50; ++i)
	{
		Status->ServerApplyItem(EItemType::Roller);
	}
	TestTrue(TEXT("Roller 스팸: MoveSpeedMulCap 에서 클램프"),
		FMath::IsNearlyEqual(Status->MoveSpeedMul, Rules->MoveSpeedMulCap, 0.001f));
	const float BoostedSpeed = BaseSpeed * Status->MoveSpeedMul;

	Status->ServerApplyItem(EItemType::Needle);
	TestTrue(TEXT("Needle: 보유"), Status->bHasNeedle);
	Status->ServerApplyItem(EItemType::Kick);
	TestTrue(TEXT("Kick: 보유"), Status->bHasKick);

	// ─── 4. 상태 전이 — Alive 에서 Escape 는 무시 ───
	Status->ServerEscape();
	TestEqual(TEXT("Alive 에서 Escape: 무시"), Status->LifeState, ELifeState::Alive);
	TestTrue(TEXT("Alive 에서 Escape: 니들 유지"), Status->bHasNeedle);

	// ─── 5. Alive → Trapped: 속도 제한 + 만료 타이머 ───
	Status->ServerTrap();
	TestEqual(TEXT("Trap: Alive → Trapped"), Status->LifeState, ELifeState::Trapped);
	TestTrue(TEXT("Trap: MaxWalkSpeed == TrappedMoveSpeed (룰셋)"),
		FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Rules->TrappedMoveSpeed, 0.01f));
	TestTrue(TEXT("Trap: 만료 타이머 가동"), World->GetTimerManager().IsTimerActive(Status->TrappedTimer));

	Status->ServerTrap();
	TestEqual(TEXT("Trapped 중 재-Trap: 무시"), Status->LifeState, ELifeState::Trapped);

	// ─── 6. Trapped + 니들 → Escape → Alive: 니들 소모·타이머 해제·속도 복원 ───
	Status->ServerEscape();
	TestEqual(TEXT("Escape: Trapped → Alive"), Status->LifeState, ELifeState::Alive);
	TestFalse(TEXT("Escape: 니들 소모"), Status->bHasNeedle);
	TestFalse(TEXT("Escape: 만료 타이머 해제"), World->GetTimerManager().IsTimerActive(Status->TrappedTimer));
	TestTrue(TEXT("Escape: MaxWalkSpeed 복원 (기본 × 배율)"),
		FMath::IsNearlyEqual(Movement->MaxWalkSpeed, BoostedSpeed, 0.01f));

	// ─── 7. 니들 없이 Escape 는 무시, 만료 시 익사 (타이머 수동 진행 — 결정론) ───
	Status->ServerTrap();
	TestEqual(TEXT("재-Trap: Trapped"), Status->LifeState, ELifeState::Trapped);
	Status->ServerEscape();
	TestEqual(TEXT("니들 없이 Escape: 무시 (Trapped 유지)"), Status->LifeState, ELifeState::Trapped);

	// FTimerManager 는 틱 밖에서 SetTimer 된 타이머를 Pending 에 뒀다가 "다음 Tick 말미"에
	// 활성화한다 — 활성화 틱 + 만료 틱, 두 번이 필요하다. 같은 GFrameCounter 프레임의
	// 중복 Tick 은 무시되므로 수동 틱 사이에 프레임 카운터를 진행시킨다 (테스트 전용 조작).
	++GFrameCounter;
	World->GetTimerManager().Tick(KINDA_SMALL_NUMBER);            // Pending → Active
	++GFrameCounter;
	World->GetTimerManager().Tick(Rules->TrappedDuration + 0.1f); // 만료 → ServerKill(Water)
	TestEqual(TEXT("TrappedDuration 만료: 자동 사망 (Water)"), Status->LifeState, ELifeState::Dead);
	TestFalse(TEXT("사망: 타이머 정리"), World->GetTimerManager().IsTimerActive(Status->TrappedTimer));

	// ─── 8. Dead 이후 상태 전이·스탯 변경 없음 ───
	const int32 BombCountAtDeath = Status->MaxBombCount;
	Status->ServerTrap();
	TestEqual(TEXT("Dead 이후 Trap: 무시"), Status->LifeState, ELifeState::Dead);
	Status->ServerEscape();
	TestEqual(TEXT("Dead 이후 Escape: 무시"), Status->LifeState, ELifeState::Dead);
	Status->ServerApplyItem(EItemType::Balloon);
	TestEqual(TEXT("Dead 이후 아이템: 무시"), Status->MaxBombCount, BombCountAtDeath);
	Status->ServerKill(EDeathCause::Fall);
	TestEqual(TEXT("Dead 이후 재-Kill: 무시 (Dead 유지)"), Status->LifeState, ELifeState::Dead);

	// ─── 9. 낙사 연결 — KillZ 아래에서 Tick → ServerKill(Fall) (새 캐릭터로 검증) ───
	ACA3DCharacter* Faller = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (TestNotNull(TEXT("낙사용 캐릭터 스폰"), Faller))
	{
		UStatusComponent* FallerStatus = Faller->GetStatus();
		Faller->SetActorLocation(FVector(0.f, 0.f, Faller->KillZ - 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		Faller->Tick(0.016f);
		TestEqual(TEXT("KillZ 아래 Tick: ServerKill(Fall) → Dead"), FallerStatus->LifeState, ELifeState::Dead);
		Faller->Tick(0.016f); // 중복 호출 방지 확인 — Dead 스킵 경로 (크래시·상태 변화 없음)
		TestEqual(TEXT("Dead 이후 Tick: 상태 유지"), FallerStatus->LifeState, ELifeState::Dead);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
