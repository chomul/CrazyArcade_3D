// AItemPickup + 아이템 배치·노출·소멸·획득 자동화 테스트 (Task 23).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.ItemPickup"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 맵 생성기 아이템 배치의 결정론(불변식 4)·배치 대상 한정,
// 파괴 → 노출 스폰 + **자기를 드러낸 폭발에 즉사하지 않음**(순서 회귀), 물줄기 소멸(효과 없음),
// 생존 상태별 획득 가부(Alive 만), 스택 상한 클램프, 니들 수동 사용·1회 소모, 킥 플래그.
// 실제 오버랩(물리)·리플리케이션(Listen+클라)·메시 표시는 PIE 검증 (체크리스트 23).
//
// 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다 — 그리드·아이템 배치는 friend 로 손구성,
// CMC 튜닝은 TryApplyMovementTuning 직접 호출 (기존 테스트 관례). 물리 오버랩도 돌지 않으므로
// 획득 판정은 오버랩 핸들러(OnOverlap)를 직접 호출해 검증한다 — "누가 먹을 수 있나" 규칙의
// 회귀는 이걸로 충분하고, "실제로 밟으면 트리거되나"는 PIE 몫이다.
//
// ⚠️ 로컬 헬퍼 이름은 전부 Item 접두사 — UBT 가 여러 .cpp 를 한 번역 단위로 합치면
// 무명 네임스페이스가 병합돼 다른 테스트 파일의 같은 이름과 충돌한다 (mds/build.md).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/Item/ItemPickup.h"
#include "Gameplay/Item/ItemTypes.h"
#include "MapGen/FallbackMapGenerator.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FItemPickupTest, "CrazyArcade3D.Gameplay.ItemPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (BombTests 관례).
	void ItemAdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);            // 만료
	}

	int32 ItemCountPickups(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AItemPickup> It(World); It; ++It) // 파괴 대기 액터는 이터레이터가 건너뛴다
		{
			++Count;
		}
		return Count;
	}

	bool ItemPlacementsEqual(const TArray<FItemPlacement>& A, const TArray<FItemPlacement>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Cell != B[Index].Cell || A[Index].Type != B[Index].Type)
			{
				return false;
			}
		}
		return true;
	}

	// 스탯 스냅샷 — "효과가 적용되지 않았다" 를 한 줄로 비교하기 위한 것.
	struct FItemStatSnapshot
	{
		int32 MaxBombCount = 0;
		int32 BombRange = 0;
		float MoveSpeedMul = 0.f;
		bool  bHasNeedle = false;
		bool  bHasKick = false;

		explicit FItemStatSnapshot(const UStatusComponent* Status)
			: MaxBombCount(Status->MaxBombCount)
			, BombRange(Status->BombRange)
			, MoveSpeedMul(Status->MoveSpeedMul)
			, bHasNeedle(Status->bHasNeedle)
			, bHasKick(Status->bHasKick)
		{}

		bool Equals(const UStatusComponent* Status) const
		{
			return MaxBombCount == Status->MaxBombCount
				&& BombRange == Status->BombRange
				&& FMath::IsNearlyEqual(MoveSpeedMul, Status->MoveSpeedMul, 0.0001f)
				&& bHasNeedle == Status->bHasNeedle
				&& bHasKick == Status->bHasKick;
		}
	};
}

bool FItemPickupTest::RunTest(const FString& Parameters)
{
	// ══════════════════════════════════════════════════════════════════════════
	// 1부. 맵 생성기 아이템 배치 (월드 불필요 — 순수 함수 영역, 불변식 4)
	// ══════════════════════════════════════════════════════════════════════════

	UCA3DRuleSet* GenRules = NewObject<UCA3DRuleSet>();      // 기본 MapSize 21×21×4
	UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();

	FVoxelGrid GridA, GridB, GridC;
	TArray<FIntVector> SpawnsA, SpawnsB, SpawnsC;
	TArray<FItemPlacement> ItemsA, ItemsB, ItemsC;

	const bool bGenA = Generator->Generate(12345u, GenRules->MapSize, GenRules, GridA, SpawnsA, ItemsA);
	const bool bGenB = Generator->Generate(12345u, GenRules->MapSize, GenRules, GridB, SpawnsB, ItemsB); // 같은 Seed
	const bool bGenC = Generator->Generate(99999u, GenRules->MapSize, GenRules, GridC, SpawnsC, ItemsC); // 다른 Seed
	if (!TestTrue(TEXT("① 생성 3회 전부 성공"), bGenA && bGenB && bGenC))
	{
		return false;
	}

	UE_LOG(LogCA3D, Display, TEXT("아이템 배치 개수 — Seed 12345: %d / 재생성: %d / Seed 99999: %d"),
		ItemsA.Num(), ItemsB.Num(), ItemsC.Num());

	TestTrue(TEXT("① 기본 룰셋으로 아이템이 배치된다"), ItemsA.Num() > 0);

	// 불변식 4 의 핵심 회귀 — 서버·클라가 같은 Seed 로 **비트 단위 같은 배치**를 만들어야 한다.
	TestTrue(TEXT("② 결정론: 같은 Seed ⇒ 같은 배치 (셀·종류·순서까지)"),
		ItemPlacementsEqual(ItemsA, ItemsB));

	// 지형은 Seed 무관(하드코딩)이지만 아이템은 매치마다 달라야 한다.
	TestTrue(TEXT("③ 다른 Seed ⇒ 다른 배치 (지형만 고정, 아이템은 Seed 소비)"),
		!ItemPlacementsEqual(ItemsA, ItemsC));

	// 배치 대상 한정 — 크아식으로 "부숴야 나온다".
	bool bAllOnDestructible = true;
	bool bNoDuplicateCell = true;
	TArray<FIntVector> SeenCells;
	SeenCells.Reserve(ItemsA.Num());
	for (const FItemPlacement& Placement : ItemsA)
	{
		if (GridA.Get(Placement.Cell) != EBlockType::Destructible)
		{
			bAllOnDestructible = false;
		}
		if (SeenCells.Contains(Placement.Cell))
		{
			bNoDuplicateCell = false;
		}
		SeenCells.Add(Placement.Cell);
	}
	TestTrue(TEXT("④ 전 배치가 Destructible 셀 위 (Empty·Floor·Immortal 에 놓이지 않는다)"), bAllOnDestructible);
	TestTrue(TEXT("④ 한 셀에 두 개가 겹치지 않는다"), bNoDuplicateCell);

	// 니들 희소성 (GDD 3장 "낮은 드랍률") — 여러 Seed 를 모아 분포로 본다.
	int32 TypeCounts[5] = { 0, 0, 0, 0, 0 };
	for (uint32 Seed = 1; Seed <= 50; ++Seed)
	{
		FVoxelGrid Grid;
		TArray<FIntVector> Spawns;
		TArray<FItemPlacement> Items;
		Generator->Generate(Seed, GenRules->MapSize, GenRules, Grid, Spawns, Items);
		for (const FItemPlacement& Placement : Items)
		{
			++TypeCounts[static_cast<int32>(Placement.Type)];
		}
	}
	UE_LOG(LogCA3D, Display, TEXT("50 Seed 누적 종류 분포 — 풍선 %d / 물약 %d / 롤러 %d / 니들 %d / 킥 %d"),
		TypeCounts[0], TypeCounts[1], TypeCounts[2], TypeCounts[3], TypeCounts[4]);
	TestTrue(TEXT("⑤ 니들도 나오기는 한다"), TypeCounts[3] > 0);
	TestTrue(TEXT("⑤ 니들은 확연히 희소 (풍선의 1/3 미만 — 룰셋 가중치 5 vs 30)"),
		TypeCounts[3] * 3 < TypeCounts[0]);

	// 드랍률 0 → 아이템 없음 (룰셋이 실제로 소비되는지 = 하드코딩 아님).
	UCA3DRuleSet* ZeroRules = NewObject<UCA3DRuleSet>();
	ZeroRules->ItemDropPercent = 0;
	FVoxelGrid ZeroGrid;
	TArray<FIntVector> ZeroSpawns;
	TArray<FItemPlacement> ZeroItems;
	Generator->Generate(12345u, ZeroRules->MapSize, ZeroRules, ZeroGrid, ZeroSpawns, ZeroItems);
	TestEqual(TEXT("⑥ ItemDropPercent 0 → 배치 없음 (확률이 룰셋 소관)"), ZeroItems.Num(), 0);

	// ══════════════════════════════════════════════════════════════════════════
	// 2부. 월드 — 노출 스폰 · 물줄기 소멸 · 획득 · 니들 · 킥
	// ══════════════════════════════════════════════════════════════════════════

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}
	// RPC(ServerPlaceBomb 등)가 ProcessEvent 에서 버려지지 않게 액터 초기화만 켠다 (BombTests 주석).
	World->InitializeActorsForPlay(FURL());

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();

	// ── 손그리드 9×9×4 — z=0 전부 Floor, (5,4,1) 만 Destructible ──
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
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	const FIntVector HiddenCell(5, 4, 1);
	VoxelWorld->Grid.Set(HiddenCell, EBlockType::Destructible);
	VoxelWorld->bGridInitialized = true;

	// 아이템 배치 주입 (friend) — 정식 흐름에서는 생성기가 채우고 InitGridFromSeed 가 보관한다.
	FItemPlacement Hidden;
	Hidden.Cell = HiddenCell;
	Hidden.Type = EItemType::Balloon;
	VoxelWorld->ItemPlacements.Add(Hidden);

	UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>();
	if (!TestNotNull(TEXT("UExplosionSubsystem 존재"), Explosion))
	{
		World->DestroyWorld(false);
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const float HalfHeight = 88.f; // 엔진 기본 캡슐 반높이 (ACA3DCharacter 생성자 주석)
	auto LocForFootCell = [HalfHeight](int32 X, int32 Y, int32 Z)
	{
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	};

	ACA3DCharacter* Owner = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("설치자 캐릭터 스폰"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}
	Owner->TryApplyMovementTuning(); // VoxelWorld 캐시 — 설치 경로의 전제 (friend)
	UStatusComponent* OwnerStatus = Owner->GetStatus();

	// ─── ⑦ 파괴 → 노출 스폰 + 자기를 드러낸 폭발에 즉사하지 않음 (순서 회귀) ───
	Owner->SetActorLocation(LocForFootCell(4, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1));                                    // Range 1 → +X 가 (5,4,1)
	Owner->SetActorLocation(LocForFootCell(7, 7, 1), false, nullptr, ETeleportType::TeleportPhysics); // 범위 밖 대피

	TestEqual(TEXT("⑦ 폭발 전: 아이템 0개 (블록 안에 숨어 있다)"), ItemCountPickups(World), 0);

	ItemAdvanceTimers(World, Rules->BombFuseTime + 0.1f);

	TestEqual(TEXT("⑦ 파괴됨: Destructible → Empty (ApplyDestruction 단일 경로)"),
		VoxelWorld->GetBlock(HiddenCell), EBlockType::Empty);
	TestEqual(TEXT("⑦ 노출: 아이템 1개 스폰"), ItemCountPickups(World), 1);

	AItemPickup* Revealed = Explosion->FindItemAt(HiddenCell);
	if (!TestNotNull(TEXT("⑦ 레지스트리에서 셀로 조회됨 (TActorIterator 대신 셀→아이템)"), Revealed))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("⑦ **자기를 드러낸 폭발에 즉사하지 않는다** (소멸 → 노출 순서)"), IsValid(Revealed));
	TestEqual(TEXT("⑦ 종류가 배치 그대로"), Revealed->GetItemType(), EItemType::Balloon);
	TestEqual(TEXT("⑦ 셀 기록"), Revealed->GetCell(), HiddenCell);
	TestEqual(TEXT("⑦ 배치 목록에서 소비됨 (재파괴 시 복제 방지)"), VoxelWorld->GetItemPlacements().Num(), 0);

	// ─── ⑧ 물줄기 안 기존 아이템은 ServerBurn 으로 소멸 — 효과 적용 없음 ───
	const FItemStatSnapshot BeforeBurn(OwnerStatus);
	Owner->SetActorLocation(LocForFootCell(4, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);
	Owner->ServerPlaceBomb(FIntVector(4, 4, 1)); // (5,4,1) 은 이제 Empty → 물줄기가 아이템 칸을 덮는다
	Owner->SetActorLocation(LocForFootCell(7, 7, 1), false, nullptr, ETeleportType::TeleportPhysics);

	ItemAdvanceTimers(World, Rules->BombFuseTime + 0.1f);

	TestFalse(TEXT("⑧ 물줄기에 닿은 아이템 소멸"), IsValid(Revealed));
	TestEqual(TEXT("⑧ 월드에 남은 아이템 0개"), ItemCountPickups(World), 0);
	TestNull(TEXT("⑧ 레지스트리에서도 사라짐"), Explosion->FindItemAt(HiddenCell));
	TestTrue(TEXT("⑧ **효과 적용 없음** — 소유자 스탯 불변 (심리전: 태운 아이템은 아무도 못 먹는다)"),
		BeforeBurn.Equals(OwnerStatus));

	// ─── ⑨ 획득: Alive 만 (Trapped·Dead 는 실패) — 2026-08-02 사용자 확정 규칙의 회귀 ───
	ACA3DCharacter* Picker = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("획득자 캐릭터 스폰"), Picker))
	{
		World->DestroyWorld(false);
		return false;
	}
	Picker->TryApplyMovementTuning();
	UStatusComponent* PickerStatus = Picker->GetStatus();

	// 물리 오버랩은 이 월드에서 돌지 않는다 — 판정 함수를 직접 호출한다 (파일 상단 주석).
	auto SpawnItemAt = [World, &SpawnParams](EItemType Type, const FIntVector& Cell) -> AItemPickup*
	{
		AItemPickup* Item = World->SpawnActor<AItemPickup>(
			AItemPickup::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (Item)
		{
			Item->ServerInit(Type, Cell);
		}
		return Item;
	};

	// ⑨-a Trapped — 못 먹는다. 아이템도 그대로 남아야 한다 (헛되이 사라지면 그것도 버그).
	PickerStatus->ServerTrap();
	TestEqual(TEXT("⑨-a 전제: Trapped"), PickerStatus->LifeState, ELifeState::Trapped);
	AItemPickup* TrapItem = SpawnItemAt(EItemType::Balloon, FIntVector(1, 1, 1));
	if (!TestNotNull(TEXT("⑨-a 아이템 스폰"), TrapItem))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FItemStatSnapshot BeforeTrappedPickup(PickerStatus);
	TrapItem->OnOverlap(TrapItem, Picker);
	TestTrue(TEXT("⑨-a Trapped: 스탯 불변 (획득 실패)"), BeforeTrappedPickup.Equals(PickerStatus));
	TestTrue(TEXT("⑨-a Trapped: 아이템은 그대로 남는다"), IsValid(TrapItem));

	// ⑨-b Alive 복귀 후 획득 성공 — 니들로 탈출(니들 경로는 ⑪에서 따로 본다).
	PickerStatus->bHasNeedle = true;
	PickerStatus->ServerEscape();
	TestEqual(TEXT("⑨-b 전제: Alive 복귀"), PickerStatus->LifeState, ELifeState::Alive);

	const int32 BombCountBefore = PickerStatus->MaxBombCount;
	TrapItem->OnOverlap(TrapItem, Picker);
	TestEqual(TEXT("⑨-b Alive: 풍선 획득 → MaxBombCount +1 (ServerApplyItem 경유)"),
		PickerStatus->MaxBombCount, BombCountBefore + 1);
	TestFalse(TEXT("⑨-b Alive: 획득한 아이템 소멸"), IsValid(TrapItem));

	// ⑨-c Dead — 시체는 못 먹는다.
	ACA3DCharacter* Corpse = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("⑨-c 시체용 캐릭터 스폰"), Corpse))
	{
		World->DestroyWorld(false);
		return false;
	}
	Corpse->TryApplyMovementTuning();
	UStatusComponent* CorpseStatus = Corpse->GetStatus();
	CorpseStatus->ServerKill(EDeathCause::Fall);
	AItemPickup* DeadItem = SpawnItemAt(EItemType::Potion, FIntVector(2, 1, 1));
	if (TestNotNull(TEXT("⑨-c 아이템 스폰"), DeadItem))
	{
		const FItemStatSnapshot BeforeDeadPickup(CorpseStatus);
		DeadItem->OnOverlap(DeadItem, Corpse);
		TestTrue(TEXT("⑨-c Dead: 스탯 불변 (획득 실패)"), BeforeDeadPickup.Equals(CorpseStatus));
		TestTrue(TEXT("⑨-c Dead: 아이템은 그대로 남는다"), IsValid(DeadItem));
	}

	// ─── ⑩ 스택 상한 클램프 — ServerApplyItem 이 실제로 룰셋 Cap 을 건다 ───
	for (int32 Index = 0; Index < Rules->MaxBombCountCap + 5; ++Index)
	{
		PickerStatus->ServerApplyItem(EItemType::Balloon);
	}
	for (int32 Index = 0; Index < Rules->MaxBombRangeCap + 5; ++Index)
	{
		PickerStatus->ServerApplyItem(EItemType::Potion);
	}
	for (int32 Index = 0; Index < 50; ++Index)
	{
		PickerStatus->ServerApplyItem(EItemType::Roller);
	}
	TestEqual(TEXT("⑩ 풍선 상한: MaxBombCountCap 클램프"), PickerStatus->MaxBombCount, Rules->MaxBombCountCap);
	TestEqual(TEXT("⑩ 물약 상한: MaxBombRangeCap 클램프"), PickerStatus->BombRange, Rules->MaxBombRangeCap);
	TestTrue(TEXT("⑩ 롤러 상한: MoveSpeedMulCap 클램프"),
		FMath::IsNearlyEqual(PickerStatus->MoveSpeedMul, Rules->MoveSpeedMulCap, 0.001f));

	// ─── ⑪ 니들: 획득 → 갇힘 → 수동 사용 → 탈출 + 1회 소모 ───
	ACA3DCharacter* NeedleUser = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("니들 사용자 캐릭터 스폰"), NeedleUser))
	{
		World->DestroyWorld(false);
		return false;
	}
	NeedleUser->TryApplyMovementTuning();
	UStatusComponent* NeedleStatus = NeedleUser->GetStatus();

	AItemPickup* NeedleItem = SpawnItemAt(EItemType::Needle, FIntVector(3, 1, 1));
	if (TestNotNull(TEXT("⑪ 니들 아이템 스폰"), NeedleItem))
	{
		NeedleItem->OnOverlap(NeedleItem, NeedleUser);
	}
	TestTrue(TEXT("⑪ 니들 획득 → bHasNeedle"), NeedleStatus->bHasNeedle);

	// 갇히기 전 사용은 무시된다 — 자동 사용이 아니므로 "아껴 두는" 것이 성립해야 한다.
	NeedleUser->TryUseNeedle();
	TestTrue(TEXT("⑪ Alive 에서 사용: 니들 유지 (헛소모 없음)"), NeedleStatus->bHasNeedle);

	NeedleStatus->ServerTrap();
	TestEqual(TEXT("⑪ 갇힘"), NeedleStatus->LifeState, ELifeState::Trapped);
	NeedleUser->TryUseNeedle();
	TestEqual(TEXT("⑪ 수동 사용 → 탈출 (Alive)"), NeedleStatus->LifeState, ELifeState::Alive);
	TestFalse(TEXT("⑪ 1회 소모 — 니들 소진"), NeedleStatus->bHasNeedle);
	TestFalse(TEXT("⑪ 갇힘 만료 타이머 해제"), World->GetTimerManager().IsTimerActive(NeedleStatus->TrappedTimer));

	NeedleStatus->ServerTrap();
	NeedleUser->TryUseNeedle();
	TestEqual(TEXT("⑪ 두 번째 사용은 무시 (니들 없음 → Trapped 유지)"),
		NeedleStatus->LifeState, ELifeState::Trapped);

	// ─── ⑫ 킥: bHasKick 만 서고 다른 스탯 불변 (차기 동작 자체는 이번 범위 밖) ───
	ACA3DCharacter* Kicker = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (TestNotNull(TEXT("킥 획득자 캐릭터 스폰"), Kicker))
	{
		Kicker->TryApplyMovementTuning();
		UStatusComponent* KickStatus = Kicker->GetStatus();
		const FItemStatSnapshot BeforeKick(KickStatus);

		AItemPickup* KickItem = SpawnItemAt(EItemType::Kick, FIntVector(4, 1, 1));
		if (TestNotNull(TEXT("⑫ 킥 아이템 스폰"), KickItem))
		{
			KickItem->OnOverlap(KickItem, Kicker);
		}
		TestTrue(TEXT("⑫ bHasKick 획득"), KickStatus->bHasKick);
		TestEqual(TEXT("⑫ MaxBombCount 불변"), KickStatus->MaxBombCount, BeforeKick.MaxBombCount);
		TestEqual(TEXT("⑫ BombRange 불변"), KickStatus->BombRange, BeforeKick.BombRange);
		TestTrue(TEXT("⑫ MoveSpeedMul 불변"),
			FMath::IsNearlyEqual(KickStatus->MoveSpeedMul, BeforeKick.MoveSpeedMul, 0.0001f));
		TestFalse(TEXT("⑫ 니들은 안 생긴다"), KickStatus->bHasNeedle);
	}

	// ─── ⑬ 권한 가드 (불변식 5) — 비권한 액터의 Server* 는 아무 일도 하지 않는다 ───
	AItemPickup* GuardItem = SpawnItemAt(EItemType::Roller, FIntVector(5, 1, 1));
	if (TestNotNull(TEXT("⑬ 가드 검증용 아이템 스폰"), GuardItem))
	{
		GuardItem->SetRole(ROLE_SimulatedProxy);
		const FItemStatSnapshot BeforeGuard(PickerStatus);
		GuardItem->OnOverlap(GuardItem, Picker);
		GuardItem->ServerBurn();
		TestTrue(TEXT("⑬ 비권한: 획득 판정 무시 (스탯 불변)"), BeforeGuard.Equals(PickerStatus));
		TestTrue(TEXT("⑬ 비권한: 소멸도 무시 (아이템 유지)"), IsValid(GuardItem));
		GuardItem->SetRole(ROLE_Authority);
	}

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
