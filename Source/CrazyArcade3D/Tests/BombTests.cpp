// ABomb + 설치 경로 + 연쇄 스케줄링 자동화 테스트 (Task 16).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.Bomb"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 설치 셀 계산(지상·공중 하향 스캔·경계 보정·발판 없음 거부),
// ServerPlaceBomb 권위 검증 4종(개수·점유 셀·솔리드 셀·사망), 퓨즈 만료 → 파괴 단일 경로 →
// 발밑 셀 피격 → 슬롯 반환, 연쇄 단계 분산 + bDetonated 중복 방지.
// 실제 리플리케이션(Listen+클라)·프리뷰 데칼·FX 연출은 PIE 검증 (체크리스트 16).
//
// 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다 — 그리드는 friend 로 손구성(결정론 시나리오),
// CMC 튜닝은 TryApplyMovementTuning 직접 호출 (기존 테스트 관례). 타이머(퓨즈·연쇄)는
// 월드 타이머 매니저를 수동 Tick 해 결정론적으로 진행시킨다 (StatusComponentTests 관례).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Bomb/WaterSegment.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombTest, "CrazyArcade3D.Gameplay.Bomb",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (StatusComponentTests 의 GFrameCounter 관례).
	void AdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);            // 만료
	}

	int32 CountBombs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ABomb> It(World); It; ++It) // 파괴 대기 액터는 이터레이터가 건너뛴다
		{
			++Count;
		}
		return Count;
	}

	int32 CountWaterSegments(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AWaterSegment> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

bool FBombTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 룰셋은 기본(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	// UFUNCTION RPC(ServerPlaceBomb·MulticastWaterCells)는 AActor::ProcessEvent 를 타는데,
	// ProcessEvent 는 AreActorsInitialized()==false 인 월드에서 이벤트를 조용히 버린다 —
	// 액터 초기화만 켠다 (World->BeginPlay() 는 부르지 않아 BeginPlay 미실행 전제는 유지).
	World->InitializeActorsForPlay(FURL());

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();

	// ── 손그리드 9×9×4 — z=0 은 Floor 바닥판, 단 (0,0,0) 만 Empty(구멍 — 발판 없음 시나리오) ──
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	VoxelWorld->Grid.Init(FIntVector(9, 9, 4));
	for (int32 X = 0; X < 9; ++X)
	{
		for (int32 Y = 0; Y < 9; ++Y)
		{
			if (!(X == 0 && Y == 0))
			{
				VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
			}
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
	ACA3DCharacter* Owner = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("설치자 캐릭터 스폰"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}
	Owner->TryApplyMovementTuning(); // VoxelWorld 캐시 — GetFootCell·설치 경로의 전제 (friend)
	UStatusComponent* Status = Owner->GetStatus();

	const float HalfHeight = 88.f; // 엔진 기본 캡슐 반높이 (ACA3DCharacter 생성자 주석)
	// 캡슐 바닥이 셀 (X,Y,Z) 중앙에 오도록 배치하는 헬퍼 좌표: 바닥 Z = Z*100 + 50.
	auto LocForFootCell = [HalfHeight](int32 X, int32 Y, int32 Z)
	{
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	};

	// ─── 1. 설치 셀 계산 (공중 규칙 — 잠정 확정) ───
	FIntVector PlaceCell;

	// ① 지상: 발밑 셀 (4,4,1) 그대로.
	Owner->SetActorLocation(LocForFootCell(4, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("① 지상: 설치 셀 계산 성공"), Owner->TryGetBombPlacementCell(PlaceCell));
	TestEqual(TEXT("① 지상: 발밑 셀 그대로"), PlaceCell, FIntVector(4, 4, 1));

	// ② 공중: 발밑 셀 (4,4,3) → -Z 스캔 → 첫 솔리드(z=0 Floor) 바로 위 (4,4,1).
	Owner->SetActorLocation(LocForFootCell(4, 4, 3), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("② 공중: 설치 셀 계산 성공"), Owner->TryGetBombPlacementCell(PlaceCell));
	TestEqual(TEXT("② 공중: 하향 스캔으로 (4,4,1)"), PlaceCell, FIntVector(4, 4, 1));

	// ③ 경계 파고듦: 캡슐 바닥이 바닥판(z=0) 안에 5cm 파고듦 → 발밑 셀이 솔리드 → 한 칸 위 보정.
	Owner->SetActorLocation(FVector(450.f, 450.f, 95.f + HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("③ 경계 보정: 설치 셀 계산 성공"), Owner->TryGetBombPlacementCell(PlaceCell));
	TestEqual(TEXT("③ 경계 보정: 솔리드 위 (4,4,1)"), PlaceCell, FIntVector(4, 4, 1));

	// ④ 발판 없음: (0,0) 기둥은 z=0 도 Empty → 스캔이 그리드 아래 밖 → 거부.
	Owner->SetActorLocation(LocForFootCell(0, 0, 1), false, nullptr, ETeleportType::TeleportPhysics);
	TestFalse(TEXT("④ 발판 없음: 하향 스캔이 그리드 밖 → 설치 거부"), Owner->TryGetBombPlacementCell(PlaceCell));

	// ─── 2. ServerPlaceBomb 권위 검증 (개수·점유 셀·솔리드 셀 — 사망은 마지막 ⑩) ───
	Owner->SetActorLocation(LocForFootCell(4, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);

	// ⑤ 성공: 폭탄 스폰 + 장전 + 슬롯 점유 + 레지스트리 등록.
	TestEqual(TEXT("⑤ 전제: BombPlaceCounter 0 (Task 38 — 설치 몽타주 신호)"),
		static_cast<int32>(Owner->BombPlaceCounter), 0);
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1));
	TestEqual(TEXT("⑤ 설치 성공: 폭탄 1개"), CountBombs(World), 1);
	TestEqual(TEXT("⑤ 설치 성공: ActiveBombCount == 1"), Status->ActiveBombCount, 1);
	TestEqual(TEXT("⑤ 설치 성공: BombPlaceCounter 0 → 1 (성공 확정 지점에서만 ++)"),
		static_cast<int32>(Owner->BombPlaceCounter), 1);
	ABomb* FirstBomb = Explosion->FindBombAt(FIntVector(4, 4, 1));
	if (TestNotNull(TEXT("⑤ 레지스트리에서 셀로 조회됨"), FirstBomb))
	{
		TestEqual(TEXT("⑤ Cell 기록"), FirstBomb->GetCell(), FIntVector(4, 4, 1));
		TestEqual(TEXT("⑤ Range 기록 (BombRange 1)"), FirstBomb->GetRange(), 1);
		TestTrue(TEXT("⑤ 퓨즈 타이머 가동 (서버 전용 — 불변식 3)"),
			World->GetTimerManager().IsTimerActive(FirstBomb->FuseTimer));
	}

	// ⑥ 개수 초과: MaxBombCount 1 인데 두 번째 설치 → 거부.
	Owner->ServerPlaceBomb(FIntVector(5, 4, 1));
	TestEqual(TEXT("⑥ 개수 초과: 폭탄 여전히 1개"), CountBombs(World), 1);
	TestEqual(TEXT("⑥ 개수 초과: ActiveBombCount 불변"), Status->ActiveBombCount, 1);

	// ⑦ 점유 셀: 슬롯을 늘려도 같은 셀에 기존 폭탄이 있으면 거부.
	Status->MaxBombCount = 2;
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1));
	TestEqual(TEXT("⑦ 점유 셀: 폭탄 여전히 1개"), CountBombs(World), 1);

	// ⑧ 솔리드 셀: Empty 가 아닌 칸(바닥판) → 거부.
	Owner->ServerPlaceBomb(FIntVector(4, 4, 0));
	TestEqual(TEXT("⑧ 솔리드 셀: 폭탄 여전히 1개"), CountBombs(World), 1);

	// 거부 3연속(⑥ 개수 초과 · ⑦ 점유 셀 · ⑧ 솔리드 셀) 뒤에도 카운터 불변 — 거부인데 올리면
	// 관전자 화면에서 헛스윙이 보인다 (Task 38: 표시와 실제의 어긋남 금지).
	TestEqual(TEXT("⑧ 거부 경로: BombPlaceCounter 불변 (여전히 1)"),
		static_cast<int32>(Owner->BombPlaceCounter), 1);

	// ─── 3. 퓨즈 만료 → 폭발 단계 1회: 파괴 단일 경로·발밑 셀 피격·슬롯 반환 ───
	// 소유자는 범위 밖으로 대피, 피해자는 -X 물줄기 칸에 배치, +X 에는 Destructible.
	VoxelWorld->Grid.Set(FIntVector(5, 4, 1), EBlockType::Destructible);
	Owner->SetActorLocation(LocForFootCell(7, 7, 1), false, nullptr, ETeleportType::TeleportPhysics);

	ACA3DCharacter* Victim = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("피해자 캐릭터 스폰"), Victim))
	{
		World->DestroyWorld(false);
		return false;
	}
	Victim->TryApplyMovementTuning();
	Victim->SetActorLocation(LocForFootCell(3, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);

	AdvanceTimers(World, Rules->BombFuseTime + 0.1f); // 퓨즈 만료 → RequestDetonate → 즉시 1단계

	TestEqual(TEXT("⑨ 폭발: 폭탄 소멸 (풀링 없이 Destroy)"), CountBombs(World), 0);
	TestEqual(TEXT("⑨ 폭발: ActiveBombCount 반환 → 0"), Status->ActiveBombCount, 0);
	TestEqual(TEXT("⑨ 폭발: Destructible 파괴 (ServerDestroyBlocks 단일 경로)"),
		VoxelWorld->GetBlock(FIntVector(5, 4, 1)), EBlockType::Empty);
	TestEqual(TEXT("⑨ 폭발: 바닥(Floor)은 bFloorDestructible=false 라 보존"),
		VoxelWorld->GetBlock(FIntVector(4, 4, 0)), EBlockType::Floor);
	TestEqual(TEXT("⑨ 피격: 물줄기 칸의 피해자 발밑 셀 → Trapped"),
		Victim->GetStatus()->LifeState, ELifeState::Trapped);
	TestEqual(TEXT("⑨ 회피: 범위 밖 소유자는 Alive"), Status->LifeState, ELifeState::Alive);
	// 물줄기 = 원점 + (-X, +Y, -Y, +Z) 4칸 (+X 는 Destructible 에 막히고, -Z 는 Floor 에 막힘).
	TestEqual(TEXT("⑨ FX: 물줄기 세그먼트 5칸 획득 (Multicast → 풀)"), CountWaterSegments(World), 5);

	// ─── 4. 연쇄: 인접 폭탄 유발 + ChainStepDelay 단계 분산 + bDetonated 중복 방지 ───
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1)); // MaxBombCount 2 — 두 개 설치 가능
	Owner->ServerPlaceBomb(FIntVector(5, 4, 1));
	TestEqual(TEXT("⑩ 연쇄 준비: 폭탄 2개"), CountBombs(World), 2);
	TestEqual(TEXT("⑩ 연쇄 준비: ActiveBombCount == 2"), Status->ActiveBombCount, 2);

	ABomb* BombA = Explosion->FindBombAt(FIntVector(4, 4, 1));
	ABomb* BombB = Explosion->FindBombAt(FIntVector(5, 4, 1));
	if (!TestNotNull(TEXT("⑩ 폭탄 A 조회"), BombA) || !TestNotNull(TEXT("⑩ 폭탄 B 조회"), BombB))
	{
		World->DestroyWorld(false);
		return false;
	}

	BombA->ServerForceDetonate(); // 큐가 놀고 있었다 → A 는 즉시 1단계 처리

	TestFalse(TEXT("⑪ 1단계: A 소멸"), IsValid(BombA));
	TestTrue(TEXT("⑪ 1단계: 물줄기에 닿은 B 는 연쇄 표시 (bDetonated)"), BombB->IsDetonated());
	TestTrue(TEXT("⑪ 1단계: B 는 아직 미폭발 (다음 단계 대기 — 프레임 분산)"), IsValid(BombB));
	TestEqual(TEXT("⑪ 1단계: PendingChain 에 B 하나"), Explosion->PendingChain.Num(), 1);
	TestEqual(TEXT("⑪ 1단계: A 슬롯만 반환 → ActiveBombCount 1"), Status->ActiveBombCount, 1);

	// 중복 폭발 방지 — 이미 연쇄 대기 중인 B 를 다시 유발해도 큐가 늘지 않는다.
	BombB->ServerForceDetonate();
	Explosion->RequestDetonate(BombB);
	TestEqual(TEXT("⑫ 중복 방지: 재유발해도 PendingChain 그대로 1"), Explosion->PendingChain.Num(), 1);

	AdvanceTimers(World, Rules->ChainStepDelay + 0.05f); // 다음 단계 — B 폭발

	TestEqual(TEXT("⑬ 2단계: B 소멸 — 폭탄 0개"), CountBombs(World), 0);
	TestEqual(TEXT("⑬ 2단계: ActiveBombCount 0 — 재설치 가능"), Status->ActiveBombCount, 0);
	TestEqual(TEXT("⑬ 2단계: PendingChain 비움 (소멸한 A 로 역연쇄 없음)"), Explosion->PendingChain.Num(), 0);

	// ─── 5. 사망 상태 거부 ───
	Status->ServerKill(EDeathCause::Fall);
	const int32 CounterBeforeDeadPlace = Owner->BombPlaceCounter; // 연쇄 절의 성공 2회까지 누적된 값
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1));
	TestEqual(TEXT("⑭ 사망 상태: 설치 거부 — 폭탄 0개"), CountBombs(World), 0);
	TestEqual(TEXT("⑭ 사망 거부: BombPlaceCounter 불변"),
		static_cast<int32>(Owner->BombPlaceCounter), CounterBeforeDeadPlace);

	// ─── 6. 설치 몽타주 재생 판정 (Task 38 — ShouldPlayFromCounter 순수 함수 전 분기) ───
	// 실제 몽타주 재생은 nullrhi 자동화에서 불가 — "이번 카운터 변화에 재생하는가" 라는
	// 판정만 고정한다 (AnimInstance 의 값 가공 함수 테스트와 같은 관례).

	// 변화 없음 — 폴링의 평상시 경로.
	TestFalse(TEXT("⑮ Counter == Snapshot → 재생 안 함"),
		ACA3DCharacter::ShouldPlayFromCounter(3, 3, false, false));

	// 첫 관측 — 스폰 직후(0 관측)든 늦은 접속(이미 5 인 값 관측)이든 동기화만.
	// 안 거르면 중간 접속자 화면에서 남들이 일제히 헛스윙한다.
	TestFalse(TEXT("⑮ 첫 관측(Snapshot INDEX_NONE, 값 0) → 동기화만"),
		ACA3DCharacter::ShouldPlayFromCounter(0, INDEX_NONE, false, false));
	TestFalse(TEXT("⑮ 첫 관측(늦은 접속 — 이미 5) → 동기화만"),
		ACA3DCharacter::ShouldPlayFromCounter(5, INDEX_NONE, false, false));

	// 원격 클라 본인(로컬 조종 + 비권한) — 예측이 이미 재생했으므로 동기화만.
	TestFalse(TEXT("⑮ 로컬 조종 + 비권한(원격 클라 본인) → 예측이 재생했으니 생략"),
		ACA3DCharacter::ShouldPlayFromCounter(1, 0, true, false));

	// 재생하는 세 갈래: 리슨 호스트 본인(예측 생략 경로) · 봇 · 원격 시뮬레이티드 프록시.
	TestTrue(TEXT("⑮ 로컬 조종 + 권한(리슨 호스트 본인) → 재생"),
		ACA3DCharacter::ShouldPlayFromCounter(1, 0, true, true));
	TestTrue(TEXT("⑮ 비조종 + 권한(봇·서버 시점) → 재생"),
		ACA3DCharacter::ShouldPlayFromCounter(1, 0, false, true));
	TestTrue(TEXT("⑮ 비조종 + 비권한(원격 시뮬레이티드 프록시) → 재생"),
		ACA3DCharacter::ShouldPlayFromCounter(1, 0, false, false));

	// uint8 랩어라운드(255 → 0)도 "변화" 다 — 카운터는 값이 아니라 변화가 신호다.
	TestTrue(TEXT("⑮ 랩어라운드(255 → 0)도 변화로 인정"),
		ACA3DCharacter::ShouldPlayFromCounter(0, 255, false, false));

	// 복제 등록 — 카운터가 Replicated 지정 + GetLifetimeReplicatedProps 에 실제로 추가됐는가
	// (CA3DPlayerStateTests ⑦ 관례. 캐릭터의 자체 복제 프로퍼티는 이것 하나뿐이다 — Task 38).
	{
		const FProperty* CounterProp =
			ACA3DCharacter::StaticClass()->FindPropertyByName(FName(TEXT("BombPlaceCounter")));
		TestTrue(TEXT("⑯ BombPlaceCounter 가 Replicated"),
			CounterProp && CounterProp->HasAnyPropertyFlags(CPF_Net));

		ACA3DCharacter::StaticClass()->SetUpRuntimeReplicationData(); // RepIndex 지연 초기화 강제 (PlayerStateTests ⑦ 주석)
		TArray<FLifetimeProperty> ChildProps;
		Owner->GetLifetimeReplicatedProps(ChildProps);
		TArray<FLifetimeProperty> ParentProps;
		Owner->ACharacter::GetLifetimeReplicatedProps(ParentProps);
		TestEqual(TEXT("⑯ GetLifetimeReplicatedProps 가 부모 대비 1개 추가 등록 (BombPlaceCounter)"),
			ChildProps.Num() - ParentProps.Num(), 1);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
