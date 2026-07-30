// ACA3DCharacter 자동화 테스트 (Task 10).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.Character"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: CMC 튜닝 파생 값(1칸/2칸 점프 경계 포함)과
// GetFootCell 좌표. 실제 점프 체감·CMC 예측·카메라는 PIE 검증 (체크리스트 10/11).
//
// 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다 — friend 로 TryApplyMovementTuning 을
// 직접 호출한다 (기존 HISMVoxelRendererTests 의 수동 배선 관례).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DCharacterTest, "CrazyArcade3D.Gameplay.Character",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCA3DCharacterTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 튜닝은 기본 룰셋(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DCharacter 스폰"), Character))
	{
		World->DestroyWorld(false);
		return false;
	}

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	// ─── 1. CMC 튜닝 파생 값 — 셀 크기를 바꿔도 "계수 × CellSize" 관계와 점프 경계 유지 ───
	for (const float CellSize : { 100.f, 60.f, 150.f })
	{
		VoxelWorld->CellSize = CellSize;
		Character->bMovementTuningApplied = false; // friend — 재적용 강제
		Character->TryApplyMovementTuning();

		TestTrue(FString::Printf(TEXT("CellSize %.0f: 튜닝 적용됨"), CellSize),
			Character->bMovementTuningApplied);
		TestTrue(FString::Printf(TEXT("CellSize %.0f: VoxelWorld 캐시"), CellSize),
			Character->VoxelWorld.Get() == VoxelWorld);

		// 이동속도 = 칸/초 × CellSize.
		TestTrue(FString::Printf(TEXT("CellSize %.0f: MaxWalkSpeed == %.1f칸/s × 셀"),
				CellSize, Rules->MoveSpeedCellsPerSec),
			FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Rules->MoveSpeedCellsPerSec * CellSize, 0.01f));

		// 점프 정점 = v² / 2g — 1칸은 넘고(오를 수 있고) 2칸은 못 넘는 경계 (GDD 2.1).
		const float Gravity = FMath::Abs(Movement->GetGravityZ());
		TestTrue(FString::Printf(TEXT("CellSize %.0f: 중력 > 0"), CellSize), Gravity > 0.f);
		const float Apex = FMath::Square(Movement->JumpZVelocity) / (2.f * Gravity);
		UE_LOG(LogCA3D, Display, TEXT("CellSize %.0f: JumpZVelocity %.1f → 정점 %.1f (%.2f칸)"),
			CellSize, Movement->JumpZVelocity, Apex, Apex / CellSize);
		TestTrue(FString::Printf(TEXT("CellSize %.0f: 점프 정점 > 1칸 (1블록 오름)"), CellSize),
			Apex > CellSize);
		TestTrue(FString::Printf(TEXT("CellSize %.0f: 점프 정점 < 2칸 (2블록 못 오름)"), CellSize),
			Apex < 2.f * CellSize);

		// 자동 오르기 한계 < 1칸 — "층간 이동은 점프만" 유지.
		TestTrue(FString::Printf(TEXT("CellSize %.0f: MaxStepHeight < 1칸"), CellSize),
			Movement->MaxStepHeight < CellSize);
	}

	// ─── 2. GetFootCell — 1차 정의: 캡슐 하단 위치가 속한 셀 ───
	VoxelWorld->CellSize = 100.f;
	const float CellSize = VoxelWorld->CellSize;
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FIntVector Cell(3, 5, 1);
	const FVector StandLocation = VoxelWorld->CellToWorldFloor(Cell) + FVector(0.f, 0.f, HalfHeight);

	// 셀 바닥에 정확히 선 위치.
	Character->SetActorLocation(StandLocation, false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("바닥 밀착: 발밑 셀 == 서 있는 셀"), Character->GetFootCell() == Cell);

	// CMC 는 바닥 위 ~2유닛 부양 상태를 유지한다 — 같은 셀이어야 한다.
	Character->SetActorLocation(StandLocation + FVector(0.f, 0.f, 2.f), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("CMC 부양 여유(+2): 발밑 셀 유지"), Character->GetFootCell() == Cell);

	// 점프 상승 0.5칸 — 아직 같은 셀.
	Character->SetActorLocation(StandLocation + FVector(0.f, 0.f, 0.5f * CellSize), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("점프 +0.5칸: 발밑 셀 유지"), Character->GetFootCell() == Cell);

	// 점프 상승 1.2칸 — 1차 정의로는 한 칸 위가 나온다 (⚠️ 공중 정의 미결정 — 튜닝에서 확정).
	Character->SetActorLocation(StandLocation + FVector(0.f, 0.f, 1.2f * CellSize), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("점프 +1.2칸: 발밑 셀 한 칸 위 (1차 정의의 특성)"),
		Character->GetFootCell() == Cell + FIntVector(0, 0, 1));

	// ─── 3. 반응성·공중 이동 — 가감속은 "시간"에서, 공중은 수평 속도만 계수배 ───
	// 체공 시간(점프 높이)은 그대로 두고 속도만 줄이므로 점프 이동 "거리"가 정확히 계수배가 된다.
	Movement->SetMovementMode(MOVE_Walking); // 테스트 월드는 바닥이 없어 모드를 명시한다
	Character->bMovementTuningApplied = false;
	Character->TryApplyMovementTuning();

	const float GroundSpeed = Movement->MaxWalkSpeed;
	TestTrue(TEXT("지상: 가속도 == 속도 ÷ MoveAccelTime"),
		FMath::IsNearlyEqual(Movement->MaxAcceleration, GroundSpeed / Rules->MoveAccelTime, 0.01f));
	TestTrue(TEXT("지상: 제동 감속도 == 속도 ÷ MoveBrakeTime"),
		FMath::IsNearlyEqual(Movement->BrakingDecelerationWalking, GroundSpeed / Rules->MoveBrakeTime, 0.01f));
	TestTrue(TEXT("제동 마찰 0 — 정지 시간이 감속도만으로 결정 (예측 가능한 정지 거리)"),
		Movement->bUseSeparateBrakingFriction && FMath::IsNearlyZero(Movement->BrakingFriction));
	TestTrue(TEXT("공중 조작감: AirControl 1.0 (지상과 동일 가속)"),
		FMath::IsNearlyEqual(Movement->AirControl, 1.f, KINDA_SMALL_NUMBER));

	// 전력 질주 상태로 공중 진입 — 도약 관성이 공중 상한으로 깎여야 한다
	// (안 깎으면 달리다 뛴 경우만 지상 속도로 날아가 거리 계수가 무의미해진다).
	Movement->Velocity = FVector(GroundSpeed, 0.f, 0.f);
	Movement->SetMovementMode(MOVE_Falling);

	const float AirSpeed = GroundSpeed * Rules->JumpAirSpeedFactor;
	TestTrue(TEXT("공중: 속도 상한 == 지상 × JumpAirSpeedFactor"),
		FMath::IsNearlyEqual(Movement->MaxWalkSpeed, AirSpeed, 0.01f));
	TestTrue(TEXT("공중: 도약 관성이 상한으로 깎임"),
		FMath::IsNearlyEqual(FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.f).Size(), AirSpeed, 0.01f));
	TestTrue(TEXT("공중: 가감속도 같은 시간 기준으로 재계산"),
		FMath::IsNearlyEqual(Movement->MaxAcceleration, AirSpeed / Rules->MoveAccelTime, 0.01f)
		&& FMath::IsNearlyEqual(Movement->BrakingDecelerationFalling, AirSpeed / Rules->MoveBrakeTime, 0.01f));

	// 착지 — 지상 상한 복귀 (공중 계수가 눌러앉지 않는다).
	Movement->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("착지: 지상 속도 상한 복귀"),
		FMath::IsNearlyEqual(Movement->MaxWalkSpeed, GroundSpeed, 0.01f));

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
