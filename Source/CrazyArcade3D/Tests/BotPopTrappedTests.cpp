// 봇이 갇힌 적을 노리는 목표(EBotState::PopTrapped) 자동화 테스트 (2026-08-10 사용자 확정).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.AI.BotPopTrapped" 로 실행.
//
// 검증 항목:
//   ① 갇힌 적이 있으면 그쪽으로 경로를 잡는다 — **폭탄 설치보다 먼저** (확정 킬이 기대값이 높다)
//   ② 대상이 니들로 탈출하거나 죽으면 목표를 즉시 버린다
//   ③ **위험 셀을 통과하는 경로는 고르지 않는다** (이게 제일 중요하다 — 자살 방지)
//   ④ 최대 거리(BotPopTrappedMaxCells)를 넘으면 안 쫓는다 — **실제 걸음 수**로 잰다
//   ⑤ 도달 불가면 원래 목표(Wander)로 복귀한다
//   ⑥ 갇힌 적이 둘이면 매번 같은 하나를 고른다 (액터 스폰 순서를 뒤집어도 같다)
//
// 월드 구성은 BotControllerTests 관례를 그대로 따른다 (GameInstance 표준 초기화 → SetGameMode →
// InitializeActorsForPlay). World->BeginPlay() 는 부르지 않고 지형은 손으로 구성한다(friend).
// FSM 은 Tick 이 아니라 Replan/PlanPopTrapped 를 직접 불러 검증한다 — 재계획 주기·타이머가
// 끼어들지 않아야 "무엇을 골랐는가"가 결정론적으로 드러난다.
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Bpt~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AI/BotController.h"
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelWorld.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotPopTrappedTest, "CrazyArcade3D.AI.BotPopTrapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	constexpr int32 BptSizeX = 13;
	constexpr int32 BptSizeY = 3;
	constexpr int32 BptSizeZ = 3;

	// z=0 전면 Floor 바닥판 + 그 위(z=1)는 전부 통행 가능한 평지.
	// 판정이 손으로 따라가지도록 지형을 최대한 단순하게 둔다 — 이 스위트의 관심사는
	// "무엇을 목표로 고르는가"이지 지형 탐색 자체가 아니다 (그건 BotControllerTests 담당).
	//
	// (1,0,1) 만 파괴 블록이다: 봇이 (1,1,1) 에 섰을 때 "폭탄을 놓을 이유"가 성립하게 만들어
	// **설치보다 갇힌 적이 먼저**라는 우선순위를 실제로 겨루게 하기 위한 것이다.
	void BptBuildGrid(FVoxelGrid& Grid)
	{
		Grid.Init(FIntVector(BptSizeX, BptSizeY, BptSizeZ));
		for (int32 X = 0; X < BptSizeX; ++X)
		{
			for (int32 Y = 0; Y < BptSizeY; ++Y)
			{
				Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
			}
		}
		Grid.Set(FIntVector(1, 0, 1), EBlockType::Destructible);
	}

	// 캡슐 바닥이 셀 (X,Y,Z) 안에 오도록 배치하는 좌표 (CellSize 100, 엔진 기본 반높이 88).
	FVector BptLocForFootCell(int32 X, int32 Y, int32 Z)
	{
		constexpr float HalfHeight = 88.f;
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	}
}

bool FBotPopTrappedTest::RunTest(const FString& Parameters)
{
	// ─── 월드 구성 (BotControllerTests 와 같은 절차) ───
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

	ACA3DGameMode* GameMode = World->GetAuthGameMode<ACA3DGameMode>();
	if (!TestNotNull(TEXT("ACA3DGameMode 생성"), GameMode))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	World->InitializeActorsForPlay(URL);

	// 룰셋은 **GameState 에만** 주입한다 — 봇(ABotController::ResolveRules)·캐릭터
	// (TryApplyMovementTuning)·상태 컴포넌트가 전부 GameState->Rules 를 읽는다.
	// GameMode 쪽 필드는 이 스위트가 쓰지 않으므로 건드리지 않는다 (CDO 도 그대로 둔다 —
	// 바꾸면 다른 스위트로 샌다).
	ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>();
	if (!TestNotNull(TEXT("ACA3DGameState 존재"), GameState))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>(GameState);
	GameState->Rules = Rules;

	TestTrue(TEXT("전제: 갇힌 적 노리기가 룰셋 기본값으로 켜져 있다"),
		GetDefault<UCA3DRuleSet>()->bPopTrappedOnContact);
	TestTrue(TEXT("전제: 노리는 최대 거리 기본값이 넉넉하다 (TrappedDuration × 이동속도 = 16칸)"),
		GetDefault<UCA3DRuleSet>()->BotPopTrappedMaxCells >= 16);

	// ─── 손그리드 ───
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	BptBuildGrid(VoxelWorld->Grid); // friend
	VoxelWorld->bGridInitialized = true;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	auto SpawnCharacterAt = [&](const FVector& Location) -> ACA3DCharacter*
	{
		ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (Character)
		{
			Character->TryApplyMovementTuning(); // friend — VoxelWorld 캐시 (GetFootCell 의 전제)
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		return Character;
	};

	// 갇힌 상대를 만든다 — 사람과 **같은 진입점**(ServerTrap)을 탄다.
	auto SpawnTrappedAt = [&](int32 X, int32 Y, int32 Z) -> ACA3DCharacter*
	{
		ACA3DCharacter* Character = SpawnCharacterAt(BptLocForFootCell(X, Y, Z));
		if (Character)
		{
			Character->GetStatus()->ServerTrap();
		}
		return Character;
	};

	// ─── 봇 + 폰 ───
	FActorSpawnParameters BotParams;
	BotParams.ObjectFlags |= RF_Transient;
	ABotController* Bot = World->SpawnActor<ABotController>(ABotController::StaticClass(), BotParams);
	ACA3DCharacter* BotChar = SpawnCharacterAt(BptLocForFootCell(1, 1, 1));
	if (!TestNotNull(TEXT("ABotController 스폰"), Bot) || !TestNotNull(TEXT("봇 폰 스폰"), BotChar))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Bot->Possess(BotChar);
	TestEqual(TEXT("전제: 봇 발밑 셀 (1,1,1)"), BotChar->GetFootCell(), FIntVector(1, 1, 1));

	UStatusComponent* BotStatus = BotChar->GetStatus();
	BotStatus->BombRange = 1; // BeginPlay 가 돌지 않는 월드 — 룰셋 초기값 로드를 손으로 대신한다

	const FIntVector BotCell(1, 1, 1);
	const FIntVector TargetCell(5, 1, 1);
	const TArray<FIntVector> NoDanger;

	// ─── 1. 갇힌 적이 있으면 그쪽으로 — **설치보다 먼저** ───
	{
		ACA3DCharacter* Target = SpawnTrappedAt(TargetCell.X, TargetCell.Y, TargetCell.Z);
		if (!TestNotNull(TEXT("① 갇힌 적 스폰"), Target))
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return false;
		}
		TestEqual(TEXT("① 전제: 적이 갇힘 상태"), Target->GetStatus()->LifeState, ELifeState::Trapped);

		// 설치 분기가 **실제로 발동할 수 있는 상태**로 만들어 우선순위를 겨루게 한다.
		// (쿨다운이 남아 있으면 설치가 애초에 후보가 아니라 비교 자체가 성립하지 않는다.)
		Bot->TimeSinceBombAttempt = 999.f; // friend
		TestTrue(TEXT("① 전제: 이 자리는 놓을 만하다 (설치가 후보다)"), Bot->ShouldPlaceBombAt(BotCell));

		Bot->Replan(BotCell, NoDanger); // friend

		TestEqual(TEXT("① 갇힌 적을 노린다 — 설치보다 우선"), Bot->State, EBotState::PopTrapped);
		TestTrue(TEXT("① 경로를 잡았다"), Bot->PathCells.Num() > 1);
		if (Bot->PathCells.Num() > 1)
		{
			// 목표는 "옆 칸"이 아니라 **상대의 발밑 셀 그 자체**다 — 옆 칸 중심에 서면
			// 중심 거리가 100cm 라 접촉 사거리(78cm)에 못 미쳐 영원히 안 터진다.
			TestEqual(TEXT("① 경로의 끝이 갇힌 적의 발밑 셀 (접촉이 성립하는 목표)"),
				Bot->PathCells.Last(), TargetCell);
			TestEqual(TEXT("① 경로의 시작이 현재 칸"), Bot->PathCells[0], BotCell);
			TestEqual(TEXT("① 다음 웨이포인트부터 따라간다"), Bot->PathIndex, 1);
		}
		TestTrue(TEXT("① 노리는 대상을 기록했다"), Bot->PopTarget.Get() == Target);
		TestTrue(TEXT("① 목표가 유효하다"), Bot->IsPopTargetValid());

		// ─── 2. 목표 상실 — 탈출 / 사망 ───
		UStatusComponent* TargetStatus = Target->GetStatus();

		// (2-a) 니들 탈출 — 사람과 같은 진입점(ServerEscape)을 탄다.
		TargetStatus->bHasNeedle = true;
		TargetStatus->ServerEscape();
		TestEqual(TEXT("② 전제: 니들로 탈출해 Alive"), TargetStatus->LifeState, ELifeState::Alive);
		TestFalse(TEXT("② 탈출하면 목표가 **즉시** 무효 (재계획 주기를 기다리지 않는다)"),
			Bot->IsPopTargetValid());

		Bot->Replan(BotCell, NoDanger);
		TestTrue(TEXT("② 재계획하면 더 이상 PopTrapped 가 아니다"), Bot->State != EBotState::PopTrapped);

		// (2-b) 다시 가두면 또 노린다 → 그 상태에서 사망.
		TargetStatus->ServerTrap();
		Bot->Replan(BotCell, NoDanger);
		TestEqual(TEXT("② 다시 갇히면 또 노린다 (대조군)"), Bot->State, EBotState::PopTrapped);

		TargetStatus->ServerKill(EDeathCause::Water);
		TestFalse(TEXT("② 죽으면 목표가 즉시 무효"), Bot->IsPopTargetValid());

		Bot->Replan(BotCell, NoDanger);
		TestTrue(TEXT("② 시체는 안 노린다"), Bot->State != EBotState::PopTrapped);

		Target->Destroy();
	}

	// ─── 3. 위험 셀을 통과하는 경로는 고르지 않는다 (제일 중요) ───
	//
	// 갇힌 사람 주변은 방금 물줄기가 지나간 자리라 위험 구역과 겹치기 쉽다. 확정 킬을
	// 노리겠다고 폭발에 걸어 들어가면 멍청해 보이는 정도가 아니라 그냥 자살이다.
	{
		ACA3DCharacter* Target = SpawnTrappedAt(TargetCell.X, TargetCell.Y, TargetCell.Z);
		if (TestNotNull(TEXT("③ 갇힌 적 스폰"), Target))
		{
			// (3-a) 통로를 통째로 막는 위험 — x=3 열 전체. 우회로가 없으니 포기해야 한다.
			const TArray<FIntVector> WallOfDanger = {
				FIntVector(3, 0, 1), FIntVector(3, 1, 1), FIntVector(3, 2, 1) };
			TestFalse(TEXT("③ 위험이 길을 통째로 막으면 노리지 않는다 (뚫고 가지 않는다)"),
				Bot->PlanPopTrapped(BotCell, WallOfDanger));
			TestNull(TEXT("③ 포기했으면 대상 기록도 지운다"), Bot->PopTarget.Get());

			// (3-b) 일부만 위험 — 돌아가는 길이 있으면 **위험을 밟지 않는 경로**를 고른다.
			const TArray<FIntVector> PartialDanger = { FIntVector(3, 1, 1) };
			TestTrue(TEXT("③ 우회로가 있으면 노린다"), Bot->PlanPopTrapped(BotCell, PartialDanger));

			int32 DangerousSteps = 0;
			for (const FIntVector& Step : Bot->PathCells)
			{
				if (PartialDanger.Contains(Step))
				{
					++DangerousSteps;
				}
			}
			TestEqual(TEXT("③ 고른 경로에 위험 셀이 하나도 없다"), DangerousSteps, 0);
			TestEqual(TEXT("③ 그래도 목표는 갇힌 적 그대로"), Bot->PathCells.Last(), TargetCell);

			// (3-c) 발밑이 위험하면 **회피가 최우선** — Replan 은 갇힌 적을 쳐다보지도 않는다.
			const TArray<FIntVector> DangerUnderFoot = { BotCell };
			Bot->Replan(BotCell, DangerUnderFoot);
			TestEqual(TEXT("③ 발밑이 위험하면 갇힌 적보다 회피가 먼저"), Bot->State, EBotState::Evade);

			Target->Destroy();
		}
	}

	// ─── 4. 최대 거리 — **실제 걸음 수**로 잰다 ───
	{
		ACA3DCharacter* Target = SpawnTrappedAt(TargetCell.X, TargetCell.Y, TargetCell.Z);
		if (TestNotNull(TEXT("④ 갇힌 적 스폰"), Target))
		{
			const int32 SavedMax = Rules->BotPopTrappedMaxCells;

			// (4-a) 상한 3 < 4걸음 → 안 쫓는다.
			Rules->BotPopTrappedMaxCells = 3;
			TestFalse(TEXT("④ 상한을 넘으면 안 쫓는다"), Bot->PlanPopTrapped(BotCell, NoDanger));

			// (4-b) 상한 4 = 4걸음 → 쫓는다 (경계가 포함이다).
			Rules->BotPopTrappedMaxCells = 4;
			TestTrue(TEXT("④ 상한과 같으면 쫓는다"), Bot->PlanPopTrapped(BotCell, NoDanger));
			TestEqual(TEXT("④ 평지에서는 걸음 수 = 직선 거리"), Bot->PathCells.Num() - 1, 4);

			// (4-c) **직선 거리가 아니라 걸음 수**다 — 벽을 돌아가면 상한에 걸린다.
			// x=3 을 y=0,1 만 막아 y=2 로 우회하게 만든다 (직선 4칸 → 실제 6걸음).
			for (int32 Z = 1; Z < BptSizeZ; ++Z)
			{
				VoxelWorld->Grid.Set(FIntVector(3, 0, Z), EBlockType::Immortal); // friend
				VoxelWorld->Grid.Set(FIntVector(3, 1, Z), EBlockType::Immortal);
			}

			Rules->BotPopTrappedMaxCells = 5;
			TestFalse(TEXT("④ 직선으로는 4칸이어도 실제로 6걸음이면 상한 5 에 걸린다"),
				Bot->PlanPopTrapped(BotCell, NoDanger));

			Rules->BotPopTrappedMaxCells = 6;
			TestTrue(TEXT("④ 상한을 6 으로 올리면 우회로로 쫓는다"),
				Bot->PlanPopTrapped(BotCell, NoDanger));
			TestEqual(TEXT("④ 우회 경로는 6걸음"), Bot->PathCells.Num() - 1, 6);

			// 지형 원복 — 이후 절이 앞 절의 벽에 영향받지 않게.
			for (int32 Z = 1; Z < BptSizeZ; ++Z)
			{
				VoxelWorld->Grid.Set(FIntVector(3, 0, Z), EBlockType::Empty);
				VoxelWorld->Grid.Set(FIntVector(3, 1, Z), EBlockType::Empty);
			}
			Rules->BotPopTrappedMaxCells = SavedMax;

			Target->Destroy();
		}
	}

	// ─── 5. 도달 불가면 원래 목표(Wander)로 복귀 ───
	{
		ACA3DCharacter* Target = SpawnTrappedAt(TargetCell.X, TargetCell.Y, TargetCell.Z);
		if (TestNotNull(TEXT("⑤ 갇힌 적 스폰"), Target))
		{
			// x=3 을 통째로 막는다 — 갇힌 적도, (같은 BFS 를 쓰는) 추격도 도달 불가가 된다.
			for (int32 Y = 0; Y < BptSizeY; ++Y)
			{
				for (int32 Z = 1; Z < BptSizeZ; ++Z)
				{
					VoxelWorld->Grid.Set(FIntVector(3, Y, Z), EBlockType::Immortal); // friend
				}
			}

			// 설치 분기가 끼어들지 않게 쿨다운을 남겨 둔다 — 이 절의 관심사는 "복귀 대상"이다.
			Bot->TimeSinceBombAttempt = 0.f;

			TestFalse(TEXT("⑤ 도달 불가면 노리지 않는다"), Bot->PlanPopTrapped(BotCell, NoDanger));

			Bot->Replan(BotCell, NoDanger);
			TestEqual(TEXT("⑤ 원래 목표(배회)로 복귀"), Bot->State, EBotState::Wander);
			TestTrue(TEXT("⑤ 배회 경로는 잡았다 (벽 이쪽은 여전히 넓다)"), Bot->PathCells.Num() > 1);

			for (int32 Y = 0; Y < BptSizeY; ++Y)
			{
				for (int32 Z = 1; Z < BptSizeZ; ++Z)
				{
					VoxelWorld->Grid.Set(FIntVector(3, Y, Z), EBlockType::Empty);
				}
			}
			Target->Destroy();
		}
	}

	// ─── 6. 갇힌 적이 둘이면 매번 같은 하나 (결정론) ───
	//
	// 봇을 두 적의 **정확히 가운데**에 세운다 — 걸음 수가 같아 "가까운 쪽" 으로는 안 갈린다.
	// 그래도 결과가 하나로 고정되어야 한다: 고르는 것은 BFS 확장 순서(VoxelMove::PlanarDirs
	// 고정 순서)뿐이고, 갇힌 적 목록은 멤버십 조회로만 쓰이기 때문이다.
	{
		const FIntVector MidCell(3, 1, 1);
		const FIntVector LeftCell(1, 1, 1);
		const FIntVector RightCell(5, 1, 1);
		BotChar->SetActorLocation(BptLocForFootCell(MidCell.X, MidCell.Y, MidCell.Z),
			false, nullptr, ETeleportType::TeleportPhysics);
		TestEqual(TEXT("⑥ 전제: 봇이 두 적의 가운데"), BotChar->GetFootCell(), MidCell);

		// (6-a) 왼쪽 → 오른쪽 순으로 스폰.
		ACA3DCharacter* First  = SpawnTrappedAt(LeftCell.X,  LeftCell.Y,  LeftCell.Z);
		ACA3DCharacter* Second = SpawnTrappedAt(RightCell.X, RightCell.Y, RightCell.Z);
		FIntVector ChosenA = FIntVector::ZeroValue;
		if (TestNotNull(TEXT("⑥ 갇힌 적 A 스폰"), First) && TestNotNull(TEXT("⑥ 갇힌 적 B 스폰"), Second))
		{
			TestTrue(TEXT("⑥ 노릴 대상을 골랐다"), Bot->PlanPopTrapped(MidCell, NoDanger));
			ChosenA = Bot->PathCells.Last();
			TestEqual(TEXT("⑥ 두 적 중 하나를 골랐다"), Bot->PathCells.Num() - 1, 2);

			// 같은 조건에서 반복해도 흔들리지 않는다.
			int32 Unstable = 0;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				Bot->PlanPopTrapped(MidCell, NoDanger);
				if (Bot->PathCells.Last() != ChosenA)
				{
					++Unstable;
				}
			}
			TestEqual(TEXT("⑥ 8번 반복해도 같은 대상"), Unstable, 0);

			First->Destroy();
			Second->Destroy();
		}

		// (6-b) **스폰 순서를 뒤집어도** 같은 대상을 고른다 — 액터 이터레이션 순서가
		// 결과에 개입하지 않는다는 것이 이 절의 요점이다 (불변식 4 의 정신).
		ACA3DCharacter* ReversedFirst  = SpawnTrappedAt(RightCell.X, RightCell.Y, RightCell.Z);
		ACA3DCharacter* ReversedSecond = SpawnTrappedAt(LeftCell.X,  LeftCell.Y,  LeftCell.Z);
		if (TestNotNull(TEXT("⑥ 역순 갇힌 적 A 스폰"), ReversedFirst)
			&& TestNotNull(TEXT("⑥ 역순 갇힌 적 B 스폰"), ReversedSecond))
		{
			TestTrue(TEXT("⑥ 역순에서도 대상을 골랐다"), Bot->PlanPopTrapped(MidCell, NoDanger));
			TestEqual(TEXT("⑥ 스폰 순서를 뒤집어도 **같은 하나**를 고른다"),
				Bot->PathCells.Last(), ChosenA);

			ReversedFirst->Destroy();
			ReversedSecond->Destroy();
		}
	}

	// ─── 7. 룰셋 스위치를 끄면 봇도 안 노린다 (사람과 같은 값을 본다) ───
	{
		BotChar->SetActorLocation(BptLocForFootCell(BotCell.X, BotCell.Y, BotCell.Z),
			false, nullptr, ETeleportType::TeleportPhysics);

		ACA3DCharacter* Target = SpawnTrappedAt(TargetCell.X, TargetCell.Y, TargetCell.Z);
		if (TestNotNull(TEXT("⑦ 갇힌 적 스폰"), Target))
		{
			Rules->bPopTrappedOnContact = false;
			TestFalse(TEXT("⑦ 기능을 끄면 봇도 노리지 않는다"), Bot->PlanPopTrapped(BotCell, NoDanger));

			Rules->bPopTrappedOnContact = true;
			TestTrue(TEXT("⑦ 다시 켜면 노린다 (대조군)"), Bot->PlanPopTrapped(BotCell, NoDanger));

			Target->Destroy();
		}
	}

	// ─── 정리 ───
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
