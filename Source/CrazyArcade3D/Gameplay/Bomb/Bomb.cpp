#include "Gameplay/Bomb/Bomb.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Bomb/DangerDecal.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Gameplay/SpinVisual.h"
#include "Core/PoolSubsystem.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"             // 막힘 승격 — 겹친 액터가 폰인지 판별
#include "GameFramework/PlayerController.h" // 예측 비주얼 반납 — 로컬 폰 탐색 (Task 17)
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	// 룰셋 해석 — 퓨즈 시간·바닥 파괴 규칙·데칼 클래스의 출처. GameState 복제 포인터 → CDO 폴백
	// (StatusComponent::ResolveRules 와 동일 관례).
	const UCA3DRuleSet* ResolveBombRules(const UWorld* World)
	{
		if (World)
		{
			if (const ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>())
			{
				if (GameState->Rules)
				{
					return GameState->Rules;
				}
			}
		}
		return GetDefault<UCA3DRuleSet>();
	}
}

ABomb::ABomb()
{
	// 틱은 메시 제자리 회전 **전용** (2026-08-06). 판정·타이머는 전부 이벤트/타이머다 —
	// 여기에 상태 변경 로직을 얹지 말 것. 데디 서버는 BeginPlay 에서 틱을 끈다.
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	// 맵 전체가 한 화면 규모(21×21) — 컬링으로 폭탄 액터를 놓치면 클라 프리뷰가 어긋난다.
	bAlwaysRelevant = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 표시 전용 — 막힘 판정은 아래 BlockingBox 가 따로 진다. 메시에 컬리전을 얹으면
	// 회전(SpinVisual)이 판정 형상까지 돌려 막히는 방향이 시간에 따라 달라진다.
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetReceivesDecals(false); // 위험 데칼이 폭탄 구체에 빨갛게 입혀지는 것 방지

	// ── 막힘 판정 (데디 서버에서도 살아 있어야 한다 — 헤더 주석) ──
	// QueryOnly 로 충분하다: CharacterMovement 는 스윕(쿼리)으로 이동을 막는다.
	BlockingBox = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockingBox"));
	BlockingBox->SetupAttachment(RootComponent);
	BlockingBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BlockingBox->SetCollisionObjectType(ECC_WorldDynamic);
	BlockingBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 시작은 Overlap — 설치자가 빠져나간 뒤에 Block 으로 승격한다 (PromoteToBlockingIfClear).
	BlockingBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	BlockingBox->SetGenerateOverlapEvents(true);
	// ⚠️ 임시값 — BeginPlay 의 ApplyBlockingScale 이 "룰셋 계수 × CellSize" 로 덮어쓴다.
	// 초기값을 파생 결과(0.45 × 100)와 맞춰두면 룰셋이 늦게 도착해도 크기가 튀지 않는다.
	BlockingBox->SetBoxExtent(FVector(45.f, 45.f, 45.f));
}

void ABomb::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABomb, Cell);
	DOREPLIFETIME(ABomb, Range);
}

void ABomb::BeginPlay()
{
	Super::BeginPlay();

	// ── 막힘 판정: 데디 가드보다 **위**에 있어야 한다 (헤더 주석) ──
	// 서버·클라 양쪽에서 동일하게 돈다. 여기 아래로 내리면 데디 서버만 폭탄을 통과한다.
	ApplyBlockingScale();

	if (BlockingBox)
	{
		BlockingBox->OnComponentBeginOverlap.AddDynamic(this, &ABomb::OnBlockingBeginOverlap);
		BlockingBox->OnComponentEndOverlap.AddDynamic(this, &ABomb::OnBlockingEndOverlap);

		// 컴포넌트 등록(BeginPlay 이전)에 이미 계산된 겹침은 위 델리게이트로 오지 않는다 —
		// 여기서 직접 긁어 초기 집합을 만든다. 안 하면 설치자가 이미 겹쳐 있는데도 집합이 비어
		// 곧바로 Block 으로 승격돼 설치자가 자기 폭탄에 갇힌다.
		TArray<AActor*> InitialOverlaps;
		BlockingBox->GetOverlappingActors(InitialOverlaps, APawn::StaticClass());
		for (AActor* Pawn : InitialOverlaps)
		{
			OverlappingPawns.Add(Pawn);
		}

		// 아무도 안 겹쳐 있으면(예: 봇이 이동 중 설치해 이미 칸을 벗어난 경우) 즉시 막는다.
		PromoteToBlockingIfClear();
	}

	if (IsRunningDedicatedServer())
	{
		// 데디 서버는 시각이 필요 없다 (불변식 5) — 메시 파괴 (VoxelWorld 렌더러와 동일 관례).
		if (MeshComponent)
		{
			MeshComponent->DestroyComponent();
			MeshComponent = nullptr;
		}
		SetActorTickEnabled(false); // 틱은 회전 전용 — 데디에서는 돌 메시가 없다
		return;
	}

	// 회전 속도는 여기서 1회만 조회한다 — 틱마다 GameState→룰셋을 타면 낭비다.
	SpinDegreesPerSecond = CA3DSpin::ResolveDegreesPerSecond(GetWorld());

	// 서버 확정 도착 — 같은 셀의 예측 비주얼(APredictedBombVisual)을 진짜 폭탄으로 교체하는
	// 지점 (데이터 흐름 3.1). OwnerChar 는 복제되지 않으므로 로컬 플레이어의 폰 기준이 맞다 —
	// 예측 목록은 설치자 로컬에만 있고, 남의 폭탄이면 셀 매칭이 안 돼 no-op 이다.
	if (APlayerController* LocalPC = GetWorld()->GetFirstPlayerController())
	{
		if (ACA3DCharacter* LocalChar = Cast<ACA3DCharacter>(LocalPC->GetPawn()))
		{
			LocalChar->ReleasePredictedVisualAt(Cell);
		}
	}

	TryShowDangerPreview();
}

void ABomb::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		// 정식 경로는 ProcessChainStep 이 이미 반환했다 — 여기는 비정상 파괴(매치 정리 등) 안전망.
		ServerReleaseSlot();
	}

	// 그리드 변경 구독 해제 — 파괴 후 죽은 폭탄으로 프리뷰 갱신이 들어오지 않게.
	if (CachedVoxelWorld && GridChangedHandle.IsValid())
	{
		CachedVoxelWorld->OnGridChanged.Remove(GridChangedHandle);
		GridChangedHandle.Reset();
	}

	// 클라(+리슨): 프리뷰 데칼 반납. 월드 정리 중에는 풀 조작을 건너뛴다 (죽어가는 액터 반납 금지).
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		ReleasePreviewDecals();
	}
	else
	{
		PreviewDecals.Empty();
	}

	Super::EndPlay(EndPlayReason);
}

void ABomb::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 회전 **말고는 아무것도 하지 않는다.** 판정·타이머·연쇄를 여기에 얹으면 프레임레이트가
	// 게임 규칙에 새어 들어간다 (서버 틱레이트와 클라 틱레이트가 다르다).
	CA3DSpin::ApplyYaw(MeshComponent, DeltaSeconds, SpinDegreesPerSecond);
}

// ─── 막힘 판정 (서버·클라 공통) ───────────────────────────────────────────────

void ABomb::ApplyBlockingScale()
{
	if (!BlockingBox)
	{
		return;
	}

	// CellSize 의 유일한 출처는 AVoxelWorld — 셀 크기를 바꿔도 "폭탄 한 칸이 막힌다" 가 유지된다.
	// 못 찾으면 생성자 임시값을 그대로 쓴다 (VoxelWorld 없는 테스트 월드 — 막힘은 여전히 동작).
	if (!CachedVoxelWorld)
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약
		}
	}
	if (!CachedVoxelWorld)
	{
		return;
	}

	const UCA3DRuleSet* Rules = ResolveBombRules(GetWorld());
	const float Extent = Rules->BombBlockExtentCells * CachedVoxelWorld->CellSize;
	BlockingBox->SetBoxExtent(FVector(Extent, Extent, Extent));
}

void ABomb::OnBlockingBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (Cast<APawn>(OtherActor))
	{
		OverlappingPawns.Add(OtherActor);
	}
}

void ABomb::OnBlockingEndOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor,
	UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/)
{
	OverlappingPawns.Remove(OtherActor);
	PromoteToBlockingIfClear();
}

void ABomb::PromoteToBlockingIfClear()
{
	if (bBlocking || !BlockingBox)
	{
		return; // 승격은 1회 — 되돌리지 않는다 (막힌 뒤에는 애초에 겹칠 수 없다)
	}

	// 폰이 파괴되면(폭발 사망) EndOverlap 이 오지만, 월드 정리 중 등 못 받는 경로도 있다 —
	// 죽은 약참조를 걷어내지 않으면 그 폭탄은 영영 안 막힌다.
	for (auto It = OverlappingPawns.CreateIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			It.RemoveCurrent();
		}
	}

	if (OverlappingPawns.Num() > 0)
	{
		return; // 아직 누군가 폭탄 안에 서 있다
	}

	bBlocking = true;
	BlockingBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

	UE_LOG(LogCA3D, Verbose, TEXT("ABomb %s: 막힘 승격 — 셀 (%d, %d, %d) 에서 모두 빠져나감"),
		*GetName(), Cell.X, Cell.Y, Cell.Z);
}

// ─── 서버 전용 ────────────────────────────────────────────────────────────────

void ABomb::ServerArm(ACA3DCharacter* InOwner, int32 InRange, const FIntVector& InCell)
{
	if (!HasAuthority()) return; // 불변식 5

	OwnerChar = InOwner;
	Range = FMath::Max(InRange, 1);
	Cell = InCell;

	// 소유자 슬롯 점유 — 반환은 ServerReleaseSlot (폭발 처리 또는 EndPlay 안전망, 정확히 1회).
	if (OwnerChar)
	{
		if (UStatusComponent* Status = OwnerChar->GetStatus())
		{
			++Status->ActiveBombCount;
		}
	}

	// 연쇄 판정·"그 셀에 폭탄 있음" 조회용 레지스트리 (서버 전용).
	if (UExplosionSubsystem* Explosion = GetWorld()->GetSubsystem<UExplosionSubsystem>())
	{
		Explosion->RegisterBomb(this);
	}

	// 퓨즈 — 서버 전용 타이머 (불변식 3). 시간은 룰셋에서 (매직 넘버 금지).
	const UCA3DRuleSet* Rules = ResolveBombRules(GetWorld());
	GetWorldTimerManager().SetTimer(FuseTimer, this, &ABomb::OnFuseExpired, Rules->BombFuseTime, false);

	UE_LOG(LogCA3D, Log, TEXT("ABomb %s: 장전 — 셀 (%d, %d, %d) / Range %d / 퓨즈 %.1fs / 소유 %s"),
		*GetName(), Cell.X, Cell.Y, Cell.Z, Range, Rules->BombFuseTime, *GetNameSafe(OwnerChar));
}

void ABomb::ServerForceDetonate()
{
	if (!HasAuthority()) return; // 불변식 5

	if (bDetonated)
	{
		return; // 중복 폭발 방지 — 같은 폭탄이 두 연쇄 단계에 들어갈 수 없다
	}
	bDetonated = true;

	GetWorldTimerManager().ClearTimer(FuseTimer); // 연쇄 유발 시 남은 퓨즈 무시

	if (UExplosionSubsystem* Explosion = GetWorld()->GetSubsystem<UExplosionSubsystem>())
	{
		Explosion->RequestDetonate(this);
	}
}

void ABomb::ServerReleaseSlot()
{
	if (!HasAuthority()) return; // 불변식 5

	if (bSlotReturned)
	{
		return; // 정확히 1회 — 폭발 처리와 EndPlay 안전망의 중복 반환 방지
	}
	bSlotReturned = true;

	// 소유자가 죽어 파괴됐을 수 있다 — null/유효성 가드 (명세).
	if (IsValid(OwnerChar))
	{
		if (UStatusComponent* Status = OwnerChar->GetStatus())
		{
			Status->ActiveBombCount = FMath::Max(Status->ActiveBombCount - 1, 0);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>())
		{
			Explosion->UnregisterBomb(this);
		}
	}
}

void ABomb::OnFuseExpired()
{
	// 퓨즈 만료도 연쇄 유발과 같은 단일 진입점을 탄다 — bDetonated 기록 경로 하나 유지.
	ServerForceDetonate();
}

// ─── 클라 시각 (위험 프리뷰 데칼 — 설계서 5장 9번) ───────────────────────────

void ABomb::TryShowDangerPreview()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (리슨 호스트는 플레이어라 표시)

	// ── 재료 확보: VoxelWorld(그리드·좌표) + 룰셋 ──
	if (!CachedVoxelWorld)
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약
		}
	}

	// 알려진 함정(리플리케이션 순서): 클라에서 폭탄 스폰이 OnRep_Seed(그리드 초기화)보다
	// 먼저 도착할 수 있다 — 그리드 없이 Propagate 하면 프리뷰가 어긋나므로 next-tick 재시도
	// (VoxelWorld::InitGridFromSeed 와 동일한 안전책. 폭탄은 3초 살아 있어 충분하다).
	if (!CachedVoxelWorld || !CachedVoxelWorld->IsGridInitialized())
	{
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ABomb::TryShowDangerPreview));
		return;
	}

	// 퓨즈 중 다른 폭발이 지형을 바꾸면 재계산 — 설치 시점 스냅샷이 남으면 표시 ≠ 실폭발 (5장 9번).
	if (!GridChangedHandle.IsValid())
	{
		GridChangedHandle = CachedVoxelWorld->OnGridChanged.AddUObject(this, &ABomb::RefreshDangerPreview);
	}

	const UCA3DRuleSet* Rules = ResolveBombRules(GetWorld());
	UPoolSubsystem* Pool = GetWorld()->GetSubsystem<UPoolSubsystem>();
	if (!Pool)
	{
		return; // 테스트/정리 중 월드 — 프리뷰 생략 (판정과 무관)
	}

	// 실폭발과 **같은 함수** (불변식 2 활용, 별도 계산 로직 금지). BombCells 는 빈 배열 —
	// 그 인자는 ChainedCells(연쇄 판정)에만 영향을 주고 표시 대상인 WaterCells 는 동일하다.
	const FExplosionResult Result = UExplosionSubsystem::Propagate(
		CachedVoxelWorld->GetGrid(), Cell, Range, Rules->bFloorDestructible, TArray<FIntVector>());

	// 데칼 클래스: 룰셋 미지정이면 C++ 기본 — 에셋 없이도 동작해야 한다 (데칼이 경고 로그).
	const TSubclassOf<ADangerDecal> DecalClass =
		Rules->DangerDecalClass ? Rules->DangerDecalClass : TSubclassOf<ADangerDecal>(ADangerDecal::StaticClass());

	PreviewDecals.Reserve(Result.WaterCells.Num());
	for (const FIntVector& WaterCell : Result.WaterCells)
	{
		ADangerDecal* Decal = Pool->Acquire<ADangerDecal>(
			DecalClass, FTransform(CachedVoxelWorld->CellToWorld(WaterCell)));
		if (Decal)
		{
			Decal->SetCellSize(CachedVoxelWorld->CellSize);
			PreviewDecals.Add(Decal);
		}
	}
}

void ABomb::RefreshDangerPreview()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용

	ReleasePreviewDecals();
	TryShowDangerPreview();
}

void ABomb::ReleasePreviewDecals()
{
	if (PreviewDecals.Num() == 0)
	{
		return;
	}

	UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;
	for (const TObjectPtr<AActor>& Decal : PreviewDecals)
	{
		if (Pool && IsValid(Decal))
		{
			Pool->Release(Decal);
		}
	}
	PreviewDecals.Empty();
}
