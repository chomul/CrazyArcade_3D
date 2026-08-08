// 폭탄 킥(발로 차서 밀기) 자동화 테스트 (2026-08-06 사용자 확정 규칙).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.BombKick" 로 실행.
//
// 확정 규칙을 그대로 검증한다:
//   · 킥 아이템이 없으면 안 밀린다 (지금처럼 벽)
//   · 막는 것이 없으면 **정확히 BombKickMaxCells(10)칸**에서 멈춘다
//   · 벽/블록·다른 폭탄·플레이어 **앞 칸**에서 멈춘다 (그 안으로 안 들어간다)
//   · 이동 후 Cell 이 갱신되고 레지스트리 조회가 새 셀로 맞는다
//   · 미끄러지는 중 퓨즈가 만료되면 **그 시점 셀**에서 터진다
//
// 월드를 직접 만들므로 BeginPlay 는 돌지 않는다 — 그리드는 friend 로 손구성, CMC 튜닝은
// TryApplyMovementTuning 직접 호출 (BombTests.cpp 관례). 미끄러짐은 서버 틱에서 도는 이동이라
// ABomb::Tick 을 **직접 수동으로** 돌린다: 시간 스케일을 우리가 쥐고 있어야 "정확히 몇 칸"
// 같은 판정이 결정론적으로 검증된다. 퓨즈·연쇄 타이머는 월드 타이머 매니저를 수동 Tick.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombKickTest, "CrazyArcade3D.Gameplay.BombKick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (BombTests.cpp 의 관례를 그대로 따른다).
	void KickAdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);            // 만료
	}

}

bool FBombKickTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 룰셋은 기본(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL()); // RPC(ServerPlaceBomb)가 ProcessEvent 를 타려면 필요

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();
	const float CellSize = 100.f;

	// 폭탄이 멈출 때까지 수동 틱. 상한을 넘으면 false — "영원히 미끄러진다" 는 회귀를 잡는다.
	// (익명 네임스페이스 헬퍼가 아니라 여기 람다인 이유: ABomb::Tick 은 protected 라
	//  friend 인 이 테스트 클래스의 멤버 함수 안에서만 부를 수 있다.)
	auto KickTickUntilStopped = [](ABomb* Bomb, float DeltaSeconds, int32 MaxTicks = 400)
	{
		for (int32 Index = 0; Index < MaxTicks && Bomb->IsKicking(); ++Index)
		{
			Bomb->Tick(DeltaSeconds);
		}
		return !Bomb->IsKicking();
	};

	// ── 손그리드 16×9×4 — z=0 전면 Floor 바닥판, 나머지 Empty ──
	// X 를 16까지 잡은 이유: 최대 이동 10칸(기본값) 을 **막는 것 없이** 끝까지 가게 하려면
	// 시작 셀 X=2 기준으로 X=12 까지 비어 있어야 한다.
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = CellSize;
	VoxelWorld->Grid.Init(FIntVector(16, 9, 4));
	for (int32 X = 0; X < 16; ++X)
	{
		for (int32 Y = 0; Y < 9; ++Y)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	VoxelWorld->bGridInitialized = true;

	UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>();
	if (!TestNotNull(TEXT("UExplosionSubsystem 존재"), Explosion))
	{
		World->DestroyWorld(false);
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ACA3DCharacter* Kicker = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("차는 캐릭터 스폰"), Kicker))
	{
		World->DestroyWorld(false);
		return false;
	}
	Kicker->TryApplyMovementTuning(); // VoxelWorld 캐시 — GetFootCell·킥 판정의 전제 (friend)

	UStatusComponent* KickerStatus = Kicker->GetStatus();
	KickerStatus->MaxBombCount = 8; // 시나리오마다 폭탄을 새로 놓는다 — 슬롯 제한은 이 테스트의 관심사가 아님

	const float HalfHeight = 88.f;  // 엔진 기본 캡슐 반높이 (ACA3DCharacter 생성자 주석)
	const float Radius     = 34.f;  // 엔진 기본 캡슐 반지름
	// 캡슐 바닥이 셀 (X,Y,Z) 중앙에 오도록 배치하는 헬퍼 (BombTests.cpp 와 같은 식).
	auto LocForFootCell = [HalfHeight, CellSize](int32 X, int32 Y, int32 Z)
	{
		return FVector(X * CellSize + 50.f, Y * CellSize + 50.f, Z * CellSize + 50.f + HalfHeight);
	};

	// 차는 사람은 (2,4,1) 폭탄에 **닿아 있는** 위치에 세운다 — 접촉 판정(캡슐 반지름 +
	// 막힘 박스 반경)을 통과해야 킥이 발동한다. 발밑 셀은 (1,4,1) 이 된다.
	const FIntVector StartCell(2, 4, 1);
	const float BombCenterX = StartCell.X * CellSize + 50.f;              // 250
	const float ContactX    = BombCenterX - (Radius + 45.f);              // 250 - 79 = 171 → 셀 X=1
	const FVector KickerLocation(ContactX, 4 * CellSize + 50.f, 1 * CellSize + 50.f + HalfHeight);
	Kicker->SetActorLocation(KickerLocation, false, nullptr, ETeleportType::TeleportPhysics);
	TestEqual(TEXT("전제: 차는 사람의 발밑 셀은 폭탄 바로 옆 칸"), Kicker->GetFootCell(), FIntVector(1, 4, 1));

	// 시나리오 사이의 정리 — 슬롯 반환 + 파괴 (레지스트리도 ServerReleaseSlot 이 정리한다).
	auto ClearBomb = [](ABomb* Bomb)
	{
		if (IsValid(Bomb))
		{
			Bomb->ServerReleaseSlot();
			Bomb->Destroy();
		}
	};

	// 시나리오 시작 폭탄을 항상 같은 자리에 놓는 헬퍼.
	auto PlaceStartBomb = [&]() -> ABomb*
	{
		Kicker->ServerPlaceBomb(StartCell);
		return Explosion->FindBombAt(StartCell);
	};

	// ─── 1. 킥 없는 캐릭터는 못 민다 ───
	{
		ABomb* Bomb = PlaceStartBomb();
		if (!TestNotNull(TEXT("① 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		TestFalse(TEXT("① 전제: 킥 아이템 미보유"), KickerStatus->bHasKick);
		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f)); // +X 로 밀기
		TestFalse(TEXT("① 킥 없음: 폭탄은 안 움직인다 (지금처럼 벽)"), Bomb->IsKicking());

		Bomb->Tick(0.1f);
		TestEqual(TEXT("① 킥 없음: 셀도 그대로"), Bomb->GetCell(), StartCell);

		ClearBomb(Bomb);
	}

	KickerStatus->bHasKick = true; // 이후 시나리오는 전부 킥 보유 상태

	// ─── 2. 막는 것이 없으면 정확히 10칸 (+ Cell 갱신·레지스트리 추종) ───
	{
		ABomb* Bomb = PlaceStartBomb();
		if (!TestNotNull(TEXT("② 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("② 킥 보유: 밀린다"), Bomb->IsKicking());

		TestTrue(TEXT("② 미끄러짐이 스스로 멈춘다 (무한 이동 없음)"), KickTickUntilStopped(Bomb, 0.1f));

		const FIntVector Expected(StartCell.X + Rules->BombKickMaxCells, StartCell.Y, StartCell.Z);
		TestEqual(TEXT("② 정확히 BombKickMaxCells(10)칸 이동"), Bomb->GetCell(), Expected);

		// 이동 후 위치는 셀 중심에 정확히 스냅되어 있어야 한다 — 폭발 원점과 눈에 보이는
		// 위치가 어긋나면 위험 데칼(5장 9번)이 실폭발과 갈린다.
		TestTrue(TEXT("② 셀 중심에 스냅"),
			Bomb->GetActorLocation().Equals(VoxelWorld->CellToWorld(Expected), 0.01));

		// 레지스트리는 셀을 캐시하지 않고 ABomb::GetCell() 을 되묻는 구조라, Cell 갱신만으로
		// 조회가 새 셀을 따라와야 한다 (연쇄 판정·설치 검증이 전부 이 조회를 쓴다).
		TestTrue(TEXT("② 레지스트리 조회가 새 셀로 맞는다"), Explosion->FindBombAt(Expected) == Bomb);
		TestNull(TEXT("② 옛 셀에는 더 이상 없다"), Explosion->FindBombAt(StartCell));

		ClearBomb(Bomb);
	}

	// ─── 3. 벽/블록 앞 칸에서 멈춘다 (그 안으로 안 들어간다) ───
	{
		VoxelWorld->Grid.Set(FIntVector(6, 4, 1), EBlockType::Immortal);

		ABomb* Bomb = PlaceStartBomb();
		if (!TestNotNull(TEXT("③ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("③ 미끄러짐이 스스로 멈춘다"), KickTickUntilStopped(Bomb, 0.1f));
		TestEqual(TEXT("③ 블록 앞 칸 (5,4,1) 에서 정지"), Bomb->GetCell(), FIntVector(5, 4, 1));
		TestEqual(TEXT("③ 블록은 그대로 — 킥은 지형을 부수지 않는다 (불변식 1)"),
			VoxelWorld->GetBlock(FIntVector(6, 4, 1)), EBlockType::Immortal);

		ClearBomb(Bomb);
		VoxelWorld->Grid.Set(FIntVector(6, 4, 1), EBlockType::Empty);
	}

	// ─── 4. 다른 폭탄 앞 칸에서 멈춘다 ───
	{
		Kicker->ServerPlaceBomb(FIntVector(8, 4, 1));
		ABomb* Blocker = Explosion->FindBombAt(FIntVector(8, 4, 1));
		ABomb* Bomb    = PlaceStartBomb();
		if (!TestNotNull(TEXT("④ 막는 폭탄 설치"), Blocker) || !TestNotNull(TEXT("④ 차일 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("④ 미끄러짐이 스스로 멈춘다"), KickTickUntilStopped(Bomb, 0.1f));
		TestEqual(TEXT("④ 다른 폭탄 앞 칸 (7,4,1) 에서 정지"), Bomb->GetCell(), FIntVector(7, 4, 1));
		TestEqual(TEXT("④ 막은 폭탄은 제자리"), Blocker->GetCell(), FIntVector(8, 4, 1));

		ClearBomb(Bomb);
		ClearBomb(Blocker);
	}

	// ─── 5. 플레이어 앞 칸에서 멈춘다 (그리드에 없는 유일한 정지 조건) ───
	ACA3DCharacter* Victim = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("막는 캐릭터 스폰"), Victim))
	{
		World->DestroyWorld(false);
		return false;
	}
	Victim->TryApplyMovementTuning();
	{
		Victim->SetActorLocation(LocForFootCell(6, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);
		TestEqual(TEXT("⑤ 전제: 막는 캐릭터의 발밑 셀"), Victim->GetFootCell(), FIntVector(6, 4, 1));

		ABomb* Bomb = PlaceStartBomb();
		if (!TestNotNull(TEXT("⑤ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("⑤ 미끄러짐이 스스로 멈춘다"), KickTickUntilStopped(Bomb, 0.1f));
		TestEqual(TEXT("⑤ 플레이어 앞 칸 (5,4,1) 에서 정지"), Bomb->GetCell(), FIntVector(5, 4, 1));

		ClearBomb(Bomb);
	}

	// 이후 시나리오에 영향을 주지 않게 멀리 치운다 (경로 밖 — Y 를 바꾼다).
	Victim->SetActorLocation(LocForFootCell(14, 8, 1), false, nullptr, ETeleportType::TeleportPhysics);

	// ─── 6. 이동 중 퓨즈 만료 → 그 시점 셀에서 폭발 ───
	{
		// 폭발 원점을 눈으로 구분하기 위한 표식: 정지 셀 옆(+Y)과 시작 셀 옆(+Y)에 파괴 블록.
		// 경로(+X)를 막지 않는 자리라 미끄러짐 자체에는 영향이 없다.
		VoxelWorld->Grid.Set(FIntVector(5, 5, 1), EBlockType::Destructible);
		VoxelWorld->Grid.Set(FIntVector(2, 5, 1), EBlockType::Destructible);

		ABomb* Bomb = PlaceStartBomb();
		if (!TestNotNull(TEXT("⑥ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));

		// 8칸/초 × 100cm × 0.375초 = 300cm = 정확히 3칸 → (5,4,1) 셀 중심.
		Bomb->Tick(0.375f);
		TestEqual(TEXT("⑥ 3칸 이동 후 Cell 갱신"), Bomb->GetCell(), FIntVector(5, 4, 1));
		TestTrue(TEXT("⑥ 아직 미끄러지는 중 (남은 칸 있음)"), Bomb->IsKicking());

		// 여기서 퓨즈 만료 — 타이머는 킥과 무관하게 그대로 돌고 있었다.
		KickAdvanceTimers(World, Rules->BombFuseTime + 0.1f);

		TestNull(TEXT("⑥ 폭발: 폭탄 소멸"), Explosion->FindBombAt(FIntVector(5, 4, 1)));
		TestEqual(TEXT("⑥ 폭발 원점은 **그 시점 셀** — (5,5,1) 파괴"),
			VoxelWorld->GetBlock(FIntVector(5, 5, 1)), EBlockType::Empty);
		TestEqual(TEXT("⑥ 시작 셀에서는 안 터졌다 — (2,5,1) 보존"),
			VoxelWorld->GetBlock(FIntVector(2, 5, 1)), EBlockType::Destructible);
		TestEqual(TEXT("⑥ 슬롯 반환 — ActiveBombCount 0"), KickerStatus->ActiveBombCount, 0);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
