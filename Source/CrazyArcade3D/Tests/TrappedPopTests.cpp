// 갇힌 상대 터뜨리기 자동화 테스트 (2026-08-10 사용자 확정 — 원작 크레이지 아케이드 규칙).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.TrappedPop" 로 실행.
//
// 확정 규칙을 그대로 검증한다:
//   · 터뜨리는 쪽은 Alive 여야 한다 — 갇힌 사람끼리·시체는 서로 못 터뜨린다
//   · 당하는 쪽은 Trapped 일 때만. 자기 자신은 무관
//   · 즉시 사망, 사인은 **새 값** EDeathCause::Popped (익사와 섞이지 않는다)
//   · 접촉 사거리는 **킥과 같은 공식·같은 룰셋 값**을 쓴다 (여유 값을 바꾸면 둘이 함께 움직인다)
//   · 터뜨려 죽인 뒤 갇힘 타이머가 다시 죽이지 않는다
//   · 룰셋 스위치를 끄면 아무 일도 없다
//
// 월드를 직접 만들므로 BeginPlay 는 돌지 않는다 — 그리드는 friend 로 손구성, CMC 튜닝은
// TryApplyMovementTuning 직접 호출 (DeathHandlingTests·BombKickTests 관례). 룰셋은 CDO 를
// 건드리지 않고 **ACA3DGameState 에 인스턴스를 주입**한다 (CDO 를 바꾸면 다른 스위트에 샌다).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Tp~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelWorld.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrappedPopTest, "CrazyArcade3D.Gameplay.TrappedPop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr float TpCellSize   = 100.f;
	constexpr float TpHalfHeight = 88.f; // 엔진 기본 캡슐 반높이 (ACA3DCharacter 생성자 주석)
	constexpr float TpRadius     = 34.f; // 엔진 기본 캡슐 반지름

	// 캡슐 바닥이 셀 (X,Y,Z) 중앙에 오도록 배치하는 좌표 (DeathHandlingTests 와 같은 식).
	FVector TpLocForFootCell(int32 X, int32 Y, int32 Z)
	{
		return FVector(X * TpCellSize + 50.f, Y * TpCellSize + 50.f, Z * TpCellSize + 50.f + TpHalfHeight);
	}

	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (BombKickTests 의 관례를 그대로 따른다).
	void TpAdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);            // 만료
	}
}

bool FTrappedPopTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// ── 룰셋 주입 — CDO 를 건드리지 않는다 (다른 스위트로 새면 원인 찾기가 매우 어렵다) ──
	// AGameStateBase::PostInitializeComponents 가 World->SetGameState 를 부르므로 스폰만 하면
	// ACA3DCharacter::TryApplyMovementTuning / UStatusComponent::ResolveRules 가 이 값을 읽는다.
	ACA3DGameState* GameState = World->SpawnActor<ACA3DGameState>(
		ACA3DGameState::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("ACA3DGameState 스폰"), GameState))
	{
		World->DestroyWorld(false);
		return false;
	}
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GameState);
	GameState->Rules = Rules;
	TestTrue(TEXT("전제: GameState 가 월드에 등록됨 (룰셋 주입 경로)"),
		World->GetGameState<ACA3DGameState>() == GameState);

	// ── 손그리드 8×8×4 — z=0 전면 Floor 바닥판, 나머지 Empty ──
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>(
		AVoxelWorld::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = TpCellSize;
	VoxelWorld->Grid.Init(FIntVector(8, 8, 4)); // friend
	for (int32 X = 0; X < 8; ++X)
	{
		for (int32 Y = 0; Y < 8; ++Y)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	VoxelWorld->bGridInitialized = true;

	// 캐릭터 스폰 헬퍼 — 튜닝(VoxelWorld·룰셋 캐시)까지 마친 상태로 돌려준다.
	// (람다인 이유: TryApplyMovementTuning 이 private 이라 friend 인 이 함수 안에서만 부를 수 있다.)
	auto SpawnCharacterAt = [&](const FVector& Location) -> ACA3DCharacter*
	{
		ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Character)
		{
			Character->TryApplyMovementTuning(); // friend — VoxelWorld·룰셋 캐시
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking); // 테스트 월드엔 바닥 컬리전이 없다
			Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Character;
	};

	// ── 사거리 상수 확인 — 이후 시나리오의 좌표가 전부 여기서 파생된다 ──
	// 수평 = 34 + 34 + 0.1칸×100 = 78 / 수직 = 88 + 88 + 10 = 186.
	ACA3DCharacter* Probe = SpawnCharacterAt(TpLocForFootCell(0, 0, 1));
	if (!TestNotNull(TEXT("사거리 확인용 캐릭터 스폰"), Probe))
	{
		World->DestroyWorld(false);
		return false;
	}
	const float PlanarReach = Probe->GetContactReach(TpRadius, TpRadius);
	const float VerticalReach = Probe->GetContactReach(TpHalfHeight, TpHalfHeight);
	TestEqual(TEXT("전제: 수평 접촉 사거리 = 반지름 + 반지름 + 룰셋 여유"),
		PlanarReach, TpRadius + TpRadius + Rules->BombKickReachToleranceCells * TpCellSize);
	TestTrue(TEXT("전제: 사거리가 한 칸보다 짧다 — 옆 칸에 서 있는 것만으로는 안 터진다"),
		PlanarReach < TpCellSize);
	TestEqual(TEXT("전제: 수직 접촉 사거리 = 반높이 + 반높이 + 룰셋 여유"),
		VerticalReach, TpHalfHeight + TpHalfHeight + Rules->BombKickReachToleranceCells * TpCellSize);
	Probe->Destroy();

	// 시나리오마다 새 희생자를 만든다 — 죽은 캐릭터를 되살려 재사용하면 앞 시나리오의
	// 부작용(컬리전 off·숨김)이 뒤에 섞인다.
	const FVector VictimLocation = TpLocForFootCell(2, 2, 1);
	auto SpawnTrappedVictim = [&]() -> ACA3DCharacter*
	{
		ACA3DCharacter* Victim = SpawnCharacterAt(VictimLocation);
		if (Victim)
		{
			Victim->GetStatus()->ServerTrap();
		}
		return Victim;
	};

	// 희생자 기준 +X 로 Distance 만큼 떨어진 위치 (같은 높이).
	auto LocationAtPlanarDistance = [&](float Distance)
	{
		return VictimLocation + FVector(Distance, 0.f, 0.f);
	};

	// ─── 1. 갇힌 상대에 닿으면 즉사하고 사인이 새 값이다 ───
	{
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		ACA3DCharacter* Popper = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (!TestNotNull(TEXT("① 희생자 스폰"), Victim) || !TestNotNull(TEXT("① 터뜨리는 사람 스폰"), Popper))
		{
			World->DestroyWorld(false);
			return false;
		}

		UStatusComponent* VictimStatus = Victim->GetStatus();
		TestEqual(TEXT("① 전제: 희생자는 갇힘"), VictimStatus->LifeState, ELifeState::Trapped);
		TestEqual(TEXT("① 전제: 터뜨리는 사람은 Alive"), Popper->GetStatus()->LifeState, ELifeState::Alive);
		TestEqual(TEXT("① 전제: 아직 안 죽었으므로 사인은 None"),
			VictimStatus->LastDeathCause, EDeathCause::None);

		// **컨트롤러가 없는 폰**이다 — 사람이든 봇이든 이 판정이 컨트롤러를 보지 않는다는 뜻이고,
		// 그래서 봇이 지나가다 닿아도 사람이 민 것과 완전히 같은 경로를 탄다 (설계서 2.5).
		TestNull(TEXT("① 컨트롤러 없이도 판정이 돈다 — 사람·봇 공용 경로"), Victim->GetController());

		Victim->ServerTryPopIfTouched(); // friend — Tick 의 다른 부작용 없이 이 판정만 돌린다

		TestEqual(TEXT("① 즉시 사망"), VictimStatus->LifeState, ELifeState::Dead);
		TestEqual(TEXT("① 사인은 새 값 Popped — 익사(Water)와 섞이지 않는다"),
			VictimStatus->LastDeathCause, EDeathCause::Popped);
		TestFalse(TEXT("① 갇힘 만료 타이머 해제됨"),
			World->GetTimerManager().IsTimerActive(VictimStatus->TrappedTimer));

		// ── 터뜨려 죽인 뒤 갇힘 타이머가 **다시 죽이지 않는다** ──
		// 타이머가 남아 있었다면 여기서 ServerKill(Water) 가 돌아 사인이 덮인다.
		TpAdvanceTimers(World, Rules->TrappedDuration + 1.f);
		TestEqual(TEXT("① 갇힘 시간이 지나도 사인은 그대로 Popped (타이머 재사망 없음)"),
			VictimStatus->LastDeathCause, EDeathCause::Popped);
		TestEqual(TEXT("① 갇힘 시간이 지나도 여전히 Dead"), VictimStatus->LifeState, ELifeState::Dead);

		Victim->Destroy();
		Popper->Destroy();
	}

	// ─── 2. 접촉 거리 경계 — 닿기 직전은 안 죽고, 닿으면 죽는다 ───
	{
		// (2-a) 사거리 바로 밖 — 안 죽는다.
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		ACA3DCharacter* Popper = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach + 1.f));
		if (TestNotNull(TEXT("② 희생자 스폰"), Victim) && TestNotNull(TEXT("② 터뜨리는 사람 스폰"), Popper))
		{
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("② 사거리 바로 밖(+1cm): 안 죽는다"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);

			// (2-b) 한 칸 옆(100cm)도 안 죽는다 — "옆 칸에 들어서자마자 터진다"가 아니다.
			Popper->SetActorLocation(LocationAtPlanarDistance(TpCellSize), false, nullptr, ETeleportType::TeleportPhysics);
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("② 한 칸 옆(100cm): 안 죽는다"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);

			// (2-c) 사거리 안 — 죽는다.
			Popper->SetActorLocation(LocationAtPlanarDistance(PlanarReach - 1.f), false, nullptr, ETeleportType::TeleportPhysics);
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("② 사거리 안(-1cm): 죽는다"), Victim->GetStatus()->LifeState, ELifeState::Dead);

			Victim->Destroy();
			Popper->Destroy();
		}
	}

	// ─── 3. 사거리가 **킥과 같은 룰셋 값**을 탄다 (공식이 두 벌이 아니다) ───
	//
	// 여유 값을 키우면 킥 사거리와 터뜨리기 사거리가 **함께** 늘어나야 한다.
	// 하나만 움직이면 언젠가 "차지는 거리"와 "터지는 거리"가 갈린다.
	{
		const float SavedTolerance = Rules->BombKickReachToleranceCells;
		Rules->BombKickReachToleranceCells = 0.5f; // 50cm — 한 칸(100) 옆까지 닿게 만든다

		ACA3DCharacter* Victim = SpawnTrappedVictim();
		ACA3DCharacter* Popper = SpawnCharacterAt(LocationAtPlanarDistance(TpCellSize));
		if (TestNotNull(TEXT("③ 희생자 스폰"), Victim) && TestNotNull(TEXT("③ 터뜨리는 사람 스폰"), Popper))
		{
			TestEqual(TEXT("③ 여유를 키우면 사거리도 같이 커진다"),
				Victim->GetContactReach(TpRadius, TpRadius), TpRadius + TpRadius + 50.f);

			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("③ 킥 여유 값(BombKickReachToleranceCells)이 터뜨리기 사거리도 정한다"),
				Victim->GetStatus()->LifeState, ELifeState::Dead);

			Victim->Destroy();
			Popper->Destroy();
		}

		Rules->BombKickReachToleranceCells = SavedTolerance;
	}

	// ─── 4. 갇힌 사람끼리는 서로 못 터뜨린다 ───
	{
		ACA3DCharacter* VictimA = SpawnTrappedVictim();
		ACA3DCharacter* VictimB = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (TestNotNull(TEXT("④ 갇힌 사람 A 스폰"), VictimA) && TestNotNull(TEXT("④ 갇힌 사람 B 스폰"), VictimB))
		{
			VictimB->GetStatus()->ServerTrap();
			TestEqual(TEXT("④ 전제: 둘 다 갇힘"), VictimB->GetStatus()->LifeState, ELifeState::Trapped);

			VictimA->ServerTryPopIfTouched();
			VictimB->ServerTryPopIfTouched();

			TestEqual(TEXT("④ A 는 안 죽는다"), VictimA->GetStatus()->LifeState, ELifeState::Trapped);
			TestEqual(TEXT("④ B 도 안 죽는다"), VictimB->GetStatus()->LifeState, ELifeState::Trapped);

			VictimA->Destroy();
			VictimB->Destroy();
		}
	}

	// ─── 5. 시체는 못 터뜨린다 ───
	{
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		ACA3DCharacter* Corpse = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (TestNotNull(TEXT("⑤ 희생자 스폰"), Victim) && TestNotNull(TEXT("⑤ 시체 스폰"), Corpse))
		{
			Corpse->GetStatus()->ServerKill(EDeathCause::Fall);
			TestEqual(TEXT("⑤ 전제: 시체는 Dead"), Corpse->GetStatus()->LifeState, ELifeState::Dead);

			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑤ 시체에 닿아도 안 죽는다 (GDD 유령 방해 없음과 같은 정신)"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);

			Victim->Destroy();
			Corpse->Destroy();
		}
	}

	// ─── 6. 자기 자신은 무관 — 혼자 갇혀 있는 것만으로는 안 죽는다 ───
	{
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		if (TestNotNull(TEXT("⑥ 희생자 스폰"), Victim))
		{
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑥ 혼자 갇혀 있으면 아무 일도 없다"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);
			Victim->Destroy();
		}
	}

	// ─── 7. 갇히지 않은 상대에게 닿아도 아무 일이 없다 ───
	// (검사 주체가 갇힌 쪽이라 "갇히지 않은 사람" 은 애초에 최상단에서 되돌아간다.)
	{
		ACA3DCharacter* AliveA = SpawnCharacterAt(VictimLocation);
		ACA3DCharacter* AliveB = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (TestNotNull(TEXT("⑦ 산 사람 A 스폰"), AliveA) && TestNotNull(TEXT("⑦ 산 사람 B 스폰"), AliveB))
		{
			AliveA->ServerTryPopIfTouched();
			AliveB->ServerTryPopIfTouched();

			TestEqual(TEXT("⑦ 산 사람끼리 닿아도 A 는 그대로"), AliveA->GetStatus()->LifeState, ELifeState::Alive);
			TestEqual(TEXT("⑦ 산 사람끼리 닿아도 B 는 그대로"), AliveB->GetStatus()->LifeState, ELifeState::Alive);

			AliveA->Destroy();
			AliveB->Destroy();
		}
	}

	// ─── 8. 수직 — 위층에 서 있는 사람은 못 터뜨린다 ───
	{
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		// 평면상 완전히 겹치되 두 캡슐 반높이 합보다 높이 — 몸이 닿을 수 없는 높이다.
		ACA3DCharacter* Upstairs = SpawnCharacterAt(VictimLocation + FVector(0.f, 0.f, VerticalReach + 1.f));
		if (TestNotNull(TEXT("⑧ 희생자 스폰"), Victim) && TestNotNull(TEXT("⑧ 위층 사람 스폰"), Upstairs))
		{
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑧ 평면상 겹쳐도 위층 사람은 못 터뜨린다"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);

			// 대조군: 같은 높이로 내려오면 터진다 (수직 게이트가 판정을 통째로 죽인 게 아니다).
			Upstairs->SetActorLocation(LocationAtPlanarDistance(PlanarReach - 1.f), false, nullptr, ETeleportType::TeleportPhysics);
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑧ 같은 높이로 내려오면 터진다 (대조군)"),
				Victim->GetStatus()->LifeState, ELifeState::Dead);

			Victim->Destroy();
			Upstairs->Destroy();
		}
	}

	// ─── 9. 같은 프레임에 두 명이 밀어도 사망은 한 번 ───
	{
		ACA3DCharacter* Victim  = SpawnTrappedVictim();
		ACA3DCharacter* Left    = SpawnCharacterAt(LocationAtPlanarDistance(-(PlanarReach - 1.f)));
		ACA3DCharacter* Right   = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (TestNotNull(TEXT("⑨ 희생자 스폰"), Victim)
			&& TestNotNull(TEXT("⑨ 왼쪽 스폰"), Left) && TestNotNull(TEXT("⑨ 오른쪽 스폰"), Right))
		{
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑨ 사망"), Victim->GetStatus()->LifeState, ELifeState::Dead);

			// 두 번째 호출(같은 프레임의 다른 접촉을 흉내)이 상태를 덮어쓰지 않는다 —
			// ServerKill 최상단의 Dead 가드가 중복 사망을 막고, 사인도 그대로다.
			Victim->GetStatus()->ServerKill(EDeathCause::Water);
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑨ 중복 사망 무시 — 사인은 첫 번째 것 그대로"),
				Victim->GetStatus()->LastDeathCause, EDeathCause::Popped);

			Victim->Destroy();
			Left->Destroy();
			Right->Destroy();
		}
	}

	// ─── 10. 룰셋 스위치를 끄면 아무 일도 없다 ───
	{
		Rules->bPopTrappedOnContact = false;

		ACA3DCharacter* Victim = SpawnTrappedVictim();
		ACA3DCharacter* Popper = SpawnCharacterAt(LocationAtPlanarDistance(PlanarReach - 1.f));
		if (TestNotNull(TEXT("⑩ 희생자 스폰"), Victim) && TestNotNull(TEXT("⑩ 터뜨리는 사람 스폰"), Popper))
		{
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑩ 기능을 끄면 닿아도 안 죽는다"),
				Victim->GetStatus()->LifeState, ELifeState::Trapped);

			// 켜면 다시 죽는다 — 위 결과가 "판정 자체가 고장난 것"이 아님을 못 박는다.
			Rules->bPopTrappedOnContact = true;
			Victim->ServerTryPopIfTouched();
			TestEqual(TEXT("⑩ 다시 켜면 죽는다 (대조군)"), Victim->GetStatus()->LifeState, ELifeState::Dead);

			Victim->Destroy();
			Popper->Destroy();
		}

		TestTrue(TEXT("⑩ 기본값은 켬"), GetDefault<UCA3DRuleSet>()->bPopTrappedOnContact);
	}

	// ─── 11. 갇힘 규칙은 그대로다 — 이번 변경이 이동·점프 규칙을 건드리지 않았다 ───
	{
		ACA3DCharacter* Victim = SpawnTrappedVictim();
		if (TestNotNull(TEXT("⑪ 희생자 스폰"), Victim))
		{
			UCharacterMovementComponent* Movement = Victim->GetCharacterMovement();

			TestEqual(TEXT("⑪ 갇힘 이동속도는 여전히 TrappedMoveSpeed"),
				Movement->MaxWalkSpeed, Rules->TrappedMoveSpeed);

			Movement->ConsumeInputVector();
			Victim->Move(FVector2D(1.f, 0.f));
			TestFalse(TEXT("⑪ 갇힘 중 미세 이동은 여전히 허용"),
				Movement->GetPendingInputVector().IsNearlyZero());

			Victim->bPressedJump = false;
			Victim->DoJump();
			TestFalse(TEXT("⑪ 갇힘 중 점프는 여전히 금지"), Victim->bPressedJump);

			Victim->Destroy();
		}
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
