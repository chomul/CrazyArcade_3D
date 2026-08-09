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

	// ══ 낙하 (2026-08-09 사용자 확정) ═══════════════════════════════════════════
	//
	// 규칙: 새 셀 중심에 도달했을 때 그 칸 **바로 아래가 솔리드가 아니면** 수직 등속으로
	// 떨어진다. 수평 이동은 그 순간 끊기고(포물선 금지), 한 칸 내려갈 때마다 Cell.Z 가
	// 갱신되며, 착지하면 남은 킥 거리를 버리고 멈춘다. 착지할 것이 없는 열이면 폭발 없이 소멸.
	//
	// 손그리드는 z=0 이 전면 Floor 라 z=1 에서는 떨어질 곳이 없다 — 낙하 시나리오는 z=2 에
	// **발판(ledge)** 을 깔고 그 위 z=3 에서 시작한다. 시나리오마다 Y 레인을 달리 써서
	// 앞선 시나리오의 지형 변경과 섞이지 않게 한다.

	auto BuildLedge = [&](int32 Y, int32 FromX, int32 ToX)
	{
		for (int32 X = FromX; X <= ToX; ++X)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 2), EBlockType::Immortal);
		}
	};

	// 폭탄 셀 바로 왼쪽(-X) 칸에, 접촉 판정이 통과하는 거리로 세운다 (위 KickerLocation 과 같은 공식).
	auto StandKickerNextTo = [&](const FIntVector& BombCell)
	{
		const float BombCenterAxis = BombCell.X * CellSize + 50.f;
		Kicker->SetActorLocation(
			FVector(BombCenterAxis - (Radius + 45.f),
			        BombCell.Y * CellSize + 50.f,
			        BombCell.Z * CellSize + 50.f + HalfHeight),
			false, nullptr, ETeleportType::TeleportPhysics);
	};

	// 킥·낙하가 모두 끝날 때까지 수동 틱. **맵 밖 소멸도 정상 종료로 친다** — 그래서
	// KickTickUntilStopped 를 그대로 쓸 수 없다 (파괴된 폭탄을 계속 역참조한다).
	auto TickUntilSettled = [](ABomb* Bomb, float DeltaSeconds, int32 MaxTicks = 400)
	{
		for (int32 Index = 0;
		     Index < MaxTicks && IsValid(Bomb) && (Bomb->IsKicking() || Bomb->IsFalling());
		     ++Index)
		{
			Bomb->Tick(DeltaSeconds);
		}
		return !IsValid(Bomb) || (!Bomb->IsKicking() && !Bomb->IsFalling());
	};

	// ─── 7. 절벽 칸으로 밀면 낙하해 아래 솔리드 위에 착지한다 (+ 8. 착지 후 정지) ───
	{
		const int32 Lane = 2;
		BuildLedge(Lane, 2, 4); // z=2 발판 X 2..4 — 그 위 z=3 에 폭탄이 선다. X=5 부터가 절벽

		const FIntVector Start(4, Lane, 3);
		StandKickerNextTo(Start);
		TestEqual(TEXT("⑦ 전제: 차는 사람의 발밑 셀"), Kicker->GetFootCell(), FIntVector(3, Lane, 3));

		Kicker->ServerPlaceBomb(Start);
		ABomb* Bomb = Explosion->FindBombAt(Start);
		if (!TestNotNull(TEXT("⑦ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}
		TestFalse(TEXT("⑦ 전제: 발판 위라 아직 안 떨어진다"), Bomb->IsFalling());

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("⑦ 킥 발동"), Bomb->IsKicking());

		TestTrue(TEXT("⑦ 킥·낙하가 스스로 끝난다 (무한 이동 없음)"), TickUntilSettled(Bomb, 0.02f));
		TestTrue(TEXT("⑦ 폭탄은 살아 있다 — 착지한 것이지 맵 밖 소멸이 아니다"), IsValid(Bomb));

		// (5,Lane,2) 가 비어 있어 (5,Lane,3) 에 들어서는 순간 떨어지고, z=0 바닥판 위 z=1 에 선다.
		TestEqual(TEXT("⑦ 절벽 칸에서 낙하해 착지 — Cell.Z 가 3 → 1 로 줄었다"),
			Bomb->GetCell(), FIntVector(5, Lane, 1));
		TestFalse(TEXT("⑦ 착지 후에는 낙하 중이 아니다"), Bomb->IsFalling());
		TestFalse(TEXT("⑦ 착지 후에는 킥 중도 아니다 (수평 이동은 낙하 시작에 끊겼다)"), Bomb->IsKicking());
		TestTrue(TEXT("⑦ 착지 셀 중심에 스냅 — 폭발 원점과 보이는 위치가 일치"),
			Bomb->GetActorLocation().Equals(VoxelWorld->CellToWorld(FIntVector(5, Lane, 1)), 0.01));
		TestTrue(TEXT("⑦ 레지스트리 조회가 착지 셀로 맞는다"),
			Explosion->FindBombAt(FIntVector(5, Lane, 1)) == Bomb);
		TestNull(TEXT("⑦ 옛 셀(공중)에는 더 이상 없다"), Explosion->FindBombAt(FIntVector(5, Lane, 3)));

		// ── 8. 착지하면 멈춘다 — 10칸 중 1칸만 썼지만 **남은 킥 거리를 쓰지 않는다** ──
		const FVector Landed = Bomb->GetActorLocation();
		for (int32 Index = 0; Index < 60; ++Index)
		{
			Bomb->Tick(0.05f); // 총 3초 — 남은 9칸을 굴러갈 시간은 충분하다
		}
		TestEqual(TEXT("⑧ 착지 후 정지 — 남은 킥 거리를 쓰지 않는다"),
			Bomb->GetCell(), FIntVector(5, Lane, 1));
		TestTrue(TEXT("⑧ 착지 후 위치도 그대로"), Bomb->GetActorLocation().Equals(Landed, 0.01));

		ClearBomb(Bomb);
	}

	// ─── 9. 낙하 중 정지 — 그 순간 셀에서 멈추고, 그 셀에서 터진다 ───
	{
		const int32 Lane = 3;
		BuildLedge(Lane, 2, 4);

		const FIntVector Start(4, Lane, 3);
		const FIntVector AirCell(5, Lane, 3); // 발판 끝 다음 칸 — 여기서 낙하가 시작된다

		// ── 9-1. 공중에서 멈추면 **그 셀 중심**으로 스냅한다 (ServerStopFall 계약) ──
		{
			StandKickerNextTo(Start);
			Kicker->ServerPlaceBomb(Start);
			ABomb* Bomb = Explosion->FindBombAt(Start);
			if (!TestNotNull(TEXT("⑨-1 폭탄 설치"), Bomb))
			{
				World->DestroyWorld(false);
				return false;
			}

			Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));

			// 8칸/초 × 100cm × 0.125초 = 정확히 1칸 → AirCell 중심에서 낙하로 전환된다.
			Bomb->Tick(0.125f);
			TestEqual(TEXT("⑨-1 절벽 칸에 들어섰다"), Bomb->GetCell(), AirCell);
			TestTrue(TEXT("⑨-1 낙하 시작"), Bomb->IsFalling());
			TestFalse(TEXT("⑨-1 수평 이동은 낙하 시작 순간 끊긴다 (포물선 금지)"), Bomb->IsKicking());

			// 12칸/초 × 100cm × 0.04초 = 48cm — 한 칸을 못 내려가 공중에 떠 있다.
			Bomb->Tick(0.04f);
			TestEqual(TEXT("⑨-1 아직 같은 셀 (한 칸을 못 내려갔다)"), Bomb->GetCell(), AirCell);
			TestTrue(TEXT("⑨-1 전제: 셀 중심보다 아래에 떠 있다"),
				Bomb->GetActorLocation().Z < VoxelWorld->CellToWorld(AirCell).Z - 1.0);

			Bomb->ServerStopFall(); // ServerForceDetonate 가 타는 것과 같은 경로 (friend 접근)

			TestFalse(TEXT("⑨-1 낙하 정지"), Bomb->IsFalling());
			TestEqual(TEXT("⑨-1 셀은 그 순간 셀 그대로"), Bomb->GetCell(), AirCell);
			TestTrue(TEXT("⑨-1 그 순간 셀 중심으로 스냅 — 공중 셀이면 공중 셀에서"),
				Bomb->GetActorLocation().Equals(VoxelWorld->CellToWorld(AirCell), 0.01));

			ClearBomb(Bomb);
		}

		// ── 9-2. 낙하 중 ServerForceDetonate → **그 순간 셀**이 폭발 원점이다 ──
		{
			// 원점을 눈으로 구분하기 위한 표식: 공중 정지 셀 옆(+Y)과 시작 셀 옆(+Y).
			// 둘 다 원점에서 4방향으로만 뻗는 Propagate 의 사정거리 안/밖이 명확하다.
			VoxelWorld->Grid.Set(FIntVector(5, Lane + 1, 3), EBlockType::Destructible);
			VoxelWorld->Grid.Set(FIntVector(4, Lane + 1, 3), EBlockType::Destructible);

			StandKickerNextTo(Start);
			Kicker->ServerPlaceBomb(Start);
			ABomb* Bomb = Explosion->FindBombAt(Start);
			if (!TestNotNull(TEXT("⑨-2 폭탄 설치"), Bomb))
			{
				World->DestroyWorld(false);
				return false;
			}

			Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
			Bomb->Tick(0.125f); // 절벽 칸 도달 → 낙하 시작
			Bomb->Tick(0.04f);  // 48cm 하강 — 공중
			TestTrue(TEXT("⑨-2 전제: 낙하 중"), Bomb->IsFalling());

			// RequestDetonate 는 큐가 비어 있으면 첫 단계를 **즉시** 처리한다 —
			// 이 호출이 끝나면 폭탄은 이미 파괴돼 있다 (타이머 진행 불필요).
			Bomb->ServerForceDetonate();

			TestFalse(TEXT("⑨-2 폭발했다 — 폭탄 소멸"), IsValid(Bomb));
			TestNull(TEXT("⑨-2 레지스트리에서도 빠졌다"), Explosion->FindBombAt(AirCell));
			TestEqual(TEXT("⑨-2 폭발 원점은 **낙하가 멈춘 공중 셀** — (5,Lane+1,3) 파괴"),
				VoxelWorld->GetBlock(FIntVector(5, Lane + 1, 3)), EBlockType::Empty);
			TestEqual(TEXT("⑨-2 시작 셀에서는 안 터졌다 — (4,Lane+1,3) 보존"),
				VoxelWorld->GetBlock(FIntVector(4, Lane + 1, 3)), EBlockType::Destructible);
			TestEqual(TEXT("⑨-2 슬롯 반환 — ActiveBombCount 0"), KickerStatus->ActiveBombCount, 0);
		}
	}

	// ─── 10. 착지할 솔리드가 하나도 없는 열이면 폭발 없이 Destroy ───
	{
		const int32 Lane = 6;
		BuildLedge(Lane, 2, 4);
		VoxelWorld->Grid.Set(FIntVector(5, Lane, 0), EBlockType::Empty);      // 바닥판에 구멍
		VoxelWorld->Grid.Set(FIntVector(4, Lane, 1), EBlockType::Destructible); // "안 터졌다" 증거용 표식

		const FIntVector Start(4, Lane, 3);
		StandKickerNextTo(Start);
		Kicker->ServerPlaceBomb(Start);
		ABomb* Bomb = Explosion->FindBombAt(Start);
		if (!TestNotNull(TEXT("⑩ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));
		TestTrue(TEXT("⑩ 킥·낙하가 스스로 끝난다"), TickUntilSettled(Bomb, 0.02f));

		TestFalse(TEXT("⑩ 착지할 것이 없는 열 — 맵 밖으로 나가 소멸"), IsValid(Bomb));
		TestNull(TEXT("⑩ 레지스트리에서도 빠졌다"), Explosion->FindBombAt(FIntVector(5, Lane, 0)));
		TestEqual(TEXT("⑩ EndPlay 안전망이 슬롯을 반환한다 — ActiveBombCount 0"),
			KickerStatus->ActiveBombCount, 0);
		// **폭발 없이** 사라져야 한다 — 통과 경로 (5,Lane,1) 옆 칸이 살아 있는 것이 그 증거다.
		TestEqual(TEXT("⑩ 폭발하지 않았다 — 낙하 경로 옆 (4,Lane,1) 보존"),
			VoxelWorld->GetBlock(FIntVector(4, Lane, 1)), EBlockType::Destructible);

		VoxelWorld->Grid.Set(FIntVector(5, Lane, 0), EBlockType::Floor); // 바닥판 원복
	}

	// ─── 11. 큰 DeltaSeconds 로 틱해도 한 번에 여러 칸을 건너뛰지 않는다 ───
	{
		const int32 Lane = 7;
		BuildLedge(Lane, 2, 4);
		// **중간** 발판 — 맨 아래(z=0 Floor)가 아니라 z=1 에 세운다. 칸마다 판정하지 않으면
		// 이 발판을 그냥 지나쳐 맵 밖으로 사라지므로, 여기 착지하는 것 자체가 판정의 증거다.
		VoxelWorld->Grid.Set(FIntVector(5, Lane, 1), EBlockType::Immortal);

		const FIntVector Start(4, Lane, 3);
		StandKickerNextTo(Start);
		Kicker->ServerPlaceBomb(Start);
		ABomb* Bomb = Explosion->FindBombAt(Start);
		if (!TestNotNull(TEXT("⑪ 폭탄 설치"), Bomb))
		{
			World->DestroyWorld(false);
			return false;
		}

		Kicker->ServerTryKickBomb(FVector(1.f, 0.f, 0.f));

		// 10초 틱 = 킥 속도로 80칸, 낙하 속도로 120칸 분량. 한 번에 소비하면 규칙이 전부 무너진다.
		Bomb->Tick(10.f);
		TestTrue(TEXT("⑪ 큰 틱: 폭탄이 살아 있다"), IsValid(Bomb));
		TestEqual(TEXT("⑪ 큰 틱: 킥이 10칸을 순간이동하지 않고 절벽 칸에서 멈춰 섰다"),
			Bomb->GetCell(), FIntVector(5, Lane, 3));
		TestTrue(TEXT("⑪ 큰 틱: 절벽 칸에서 낙하로 전환"), Bomb->IsFalling());

		Bomb->Tick(10.f);
		TestTrue(TEXT("⑪ 큰 틱: 폭탄이 살아 있다 — 중간 발판을 지나쳐 맵 밖으로 나가지 않았다"),
			IsValid(Bomb));
		TestEqual(TEXT("⑪ 큰 틱: 중간 발판 위 (5,Lane,2) 에 착지 — 칸마다 판정"),
			Bomb->GetCell(), FIntVector(5, Lane, 2));
		TestFalse(TEXT("⑪ 착지 후 정지"), Bomb->IsFalling());
		TestTrue(TEXT("⑪ 착지 셀 중심에 스냅"),
			Bomb->GetActorLocation().Equals(VoxelWorld->CellToWorld(FIntVector(5, Lane, 2)), 0.01));

		ClearBomb(Bomb);
	}

	// ─── 12. 위치 복제가 켜져 있다 (멀티에서만 드러나는 회귀의 유일한 방벽) ───
	//
	// 이 스위트의 다른 모든 케이스는 **서버 한 곳**에서 돌기 때문에 전부 통과하면서도
	// 클라 화면에서는 폭탄이 제자리에 서 있을 수 있다. 실제로 그랬다 (2026-08-09):
	// AActor::bReplicateMovement 는 기본값이 false 고 — InitializeDefaults 에 대입이 없어
	// UObject 제로 초기화 값이 그대로 남는다 — APawn 처럼 움직이는 클래스가 각자 켜야 한다.
	// 켜지 않으면 ReplicatedMovement 가 DOREPCUSTOMCONDITION_ACTIVE_FAST(IsReplicatingMovement())
	// 로 통째로 비활성화돼 클라에는 **스폰 위치만** 도착한다. 그런데 Cell 은 잘 복제되므로
	// 위험 데칼만 옆 칸으로 옮겨 다니고 폭탄 메시는 안 움직인다.
	//
	// 폭탄은 무브먼트 컴포넌트 없이 킥·낙하가 액터를 직접 옮기는 구조라 이 플래그 말고는
	// 위치를 알릴 경로가 없다. EditDefaultsOnly 라 BP 에서 끌 수 있으므로 코드로 못 박는다.
	{
		ABomb* Bomb = PlaceStartBomb();
		if (TestNotNull(TEXT("⑫ 폭탄 설치"), Bomb))
		{
			TestTrue(TEXT("⑫ 액터 복제가 켜져 있다"), Bomb->GetIsReplicated());
			TestTrue(TEXT("⑫ **위치 복제**가 켜져 있다 — 끄면 클라에서 킥·낙하가 안 보인다"),
				Bomb->IsReplicatingMovement());
			ClearBomb(Bomb);
		}
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
