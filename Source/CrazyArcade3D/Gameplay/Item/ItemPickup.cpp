#include "Gameplay/Item/ItemPickup.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h" // 셀→아이템 레지스트리 (ActiveBombs 관례와 동일)
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"

bool AItemPickup::bWarnedMissingMesh = false;

namespace
{
	// 룰셋 해석 — 획득 판정 반지름의 출처. GameState 복제 포인터 → CDO 폴백
	// (StatusComponent::ResolveRules 와 동일 관례).
	const UCA3DRuleSet* ResolveItemRules(const UWorld* World)
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

AItemPickup::AItemPickup()
{
	PrimaryActorTick.bCanEverTick = false; // 상태 변경은 전부 이벤트(오버랩·폭발) — 틱 불필요

	bReplicates = true;
	// 맵 전체가 한 화면 규모(21×21) — 거리 컬링으로 아이템이 사라지면 "저기 있던 게 없어졌다"가
	// 클라마다 달라진다. 복제 대상은 종류·셀 두 값뿐이라 항상 relevant 비용이 작다 (ABomb 관례).
	bAlwaysRelevant = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// ── 획득 판정 볼륨 (데디 서버에서도 살아 있어야 한다 — 헤더 주석) ──
	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(RootComponent);
	// QueryOnly: 아이템이 캐릭터를 물리적으로 막으면 안 된다 (칸 위에 서서 밟고 지나가는 것이 획득).
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupSphere->SetCollisionObjectType(ECC_WorldDynamic);
	PickupSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupSphere->SetGenerateOverlapEvents(true);
	// ⚠️ 임시값 — BeginPlay 의 ApplyCellScale 이 "룰셋 계수 × CellSize" 로 덮어쓴다.
	// 초기값을 파생 결과(0.4 × 100)와 맞춰두면 룰셋이 늦게 도착해도 판정 크기가 튀지 않는다.
	PickupSphere->SetSphereRadius(40.f);

	// ── 표시 전용 (데디 가드로 꺼도 되는 건 이쪽뿐) ──
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetReceivesDecals(false); // 위험 데칼이 아이템에 빨갛게 입혀지는 것 방지 (ABomb 관례)
}

void AItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AItemPickup, Type);
	DOREPLIFETIME(AItemPickup, Cell);
}

void AItemPickup::BeginPlay()
{
	Super::BeginPlay();

	// 판정 크기는 서버·클라 공통 계산 — 클라에서 판정하지는 않지만 계산 경로를 하나로 둔다.
	ApplyCellScale();

	if (HasAuthority())
	{
		// 획득 판정은 서버 단독 (불변식 5) — 클라에서는 아예 구독하지 않는다.
		// OnOverlap 최상단에도 권한 가드가 서 있다 (이중 안전망).
		OnActorBeginOverlap.AddDynamic(this, &AItemPickup::OnOverlap);
	}

	if (IsRunningDedicatedServer())
	{
		// 불변식 5 — **메시만** 파괴한다. PickupSphere 를 같이 지우면 데디에서 아무도
		// 아이템을 못 줍게 되고, 그 버그는 PIE 로 재현되지 않는다 (헤더 주석의 함정).
		if (MeshComponent)
		{
			MeshComponent->DestroyComponent();
			MeshComponent = nullptr;
		}
		return;
	}

	// 클라 스폰 시점에 Type 이 이미 실려 있으면 OnRep 이 불리지 않는다 — 여기서도 한 번 반영한다.
	RefreshVisual();
}

void AItemPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레지스트리에서 제거 — 권한 가드 없이 제거만 (없으면 no-op, UnregisterBomb 관례).
	// 이걸 빼먹으면 소멸한 아이템 셀이 계속 조회돼 다음 폭발이 유령을 태운다.
	if (UWorld* World = GetWorld())
	{
		if (UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>())
		{
			Explosion->UnregisterItem(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

// ─── 서버 전용 ────────────────────────────────────────────────────────────────

void AItemPickup::ServerInit(EItemType InType, const FIntVector& InCell)
{
	if (!HasAuthority()) return; // 불변식 5

	Type = InType;
	Cell = InCell;

	// 셀 조회용 레지스트리 (서버 전용) — 물줄기 소멸 판정이 TActorIterator 대신 이걸 쓴다.
	// 액터 이터레이션 순서는 실행마다 달라질 수 있어 판정 입력으로 쓰지 않는다 (불변식 4 준용).
	if (UWorld* World = GetWorld())
	{
		if (UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>())
		{
			Explosion->RegisterItem(this);
		}
	}

	UE_LOG(LogCA3D, Log, TEXT("AItemPickup %s: 노출 — 셀 (%d, %d, %d) / 종류 %s"),
		*GetName(), Cell.X, Cell.Y, Cell.Z, *UEnum::GetValueAsString(Type));
}

void AItemPickup::ServerBurn()
{
	if (!HasAuthority()) return; // 불변식 5

	if (bConsumed)
	{
		return; // 이미 획득·소멸 처리됨 (Destroy 반영은 프레임 말미라 같은 프레임 재진입이 가능하다)
	}
	bConsumed = true;

	// 획득과 **경로가 다르다**: 여기서는 ServerApplyItem 을 부르지 않는다.
	// 효과 없이 사라지는 것이 규칙이다 — "적이 못 먹게 태우기" (GDD 3장).
	UE_LOG(LogCA3D, Log, TEXT("AItemPickup %s: 물줄기에 소멸 — 셀 (%d, %d, %d) / 종류 %s (효과 없음)"),
		*GetName(), Cell.X, Cell.Y, Cell.Z, *UEnum::GetValueAsString(Type));

	Destroy();
}

void AItemPickup::OnOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	if (!HasAuthority()) return; // 불변식 5 — 획득 판정은 서버 단독

	if (bConsumed)
	{
		return; // 같은 프레임 중복 오버랩(둘이 동시에 밟기 포함) — 먼저 온 쪽만 먹는다
	}

	ACA3DCharacter* Character = Cast<ACA3DCharacter>(OtherActor);
	UStatusComponent* Status = Character ? Character->GetStatus() : nullptr;
	if (!Status)
	{
		return; // 캐릭터가 아닌 액터(폭탄·투사체 등) — 아이템과 무관
	}

	// 2026-08-02 사용자 확정: **Alive 일 때만** 획득한다. 갇힘(Trapped) 중 획득을 허용하면
	// 물방울에 갇힌 것이 위험이 아니라 이득이 되고, 사망(Dead) 중 획득은 시체가 아이템을
	// 먹어 치우는 꼴이 된다 (StatusComponent 도 Dead 를 무시하지만 여기서 먼저 끊어야
	// 아이템이 헛되이 사라지지 않는다).
	if (Status->LifeState != ELifeState::Alive)
	{
		UE_LOG(LogCA3D, Verbose, TEXT("AItemPickup %s: 획득 거부 — %s 는 %s 상태"),
			*GetName(), *Character->GetName(), *UEnum::GetValueAsString(Status->LifeState));
		return;
	}

	bConsumed = true;

	// 효과 적용은 전부 StatusComponent 소관 — 아이템은 "무엇을" 만 알고 "어떻게" 는 모른다
	// (Cap 클램프·속도 재계산이 한 곳에 있어야 봇·플레이어·치트가 같은 규칙을 탄다).
	Status->ServerApplyItem(Type);

	UE_LOG(LogCA3D, Log, TEXT("AItemPickup %s: 획득 — %s 가 셀 (%d, %d, %d) 의 %s 취득"),
		*GetName(), *Character->GetName(), Cell.X, Cell.Y, Cell.Z, *UEnum::GetValueAsString(Type));

	Destroy();
}

// ─── 클라 시각 ────────────────────────────────────────────────────────────────

void AItemPickup::OnRep_Type()
{
	RefreshVisual();
}

void AItemPickup::RefreshVisual()
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 이하 시각 전용

	if (!MeshComponent)
	{
		return;
	}

	if (const TObjectPtr<UStaticMesh>* Found = ItemMeshes.Find(Type))
	{
		MeshComponent->SetStaticMesh(Found->Get());
		return;
	}

	// 에셋 없이도 판정·동작은 정상이어야 한다 (ADangerDecal 관례) — 경고만 1회.
	if (!bWarnedMissingMesh)
	{
		bWarnedMissingMesh = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("AItemPickup: 종류 %s 메시 미지정 — BP_ItemPickup 의 ItemMeshes 에 5종을 지정하고 룰셋 ItemPickupClass 에 연결할 것 (미지정이어도 획득 판정은 정상)"),
			*UEnum::GetValueAsString(Type));
	}
}

// ─── 내부 ─────────────────────────────────────────────────────────────────────

void AItemPickup::ApplyCellScale()
{
	if (!PickupSphere)
	{
		return;
	}

	// CellSize 의 유일한 출처는 AVoxelWorld — 셀 크기를 바꿔도 "한 칸 밟으면 먹는다" 가 유지된다.
	// 못 찾으면 생성자 임시값을 그대로 쓴다 (VoxelWorld 없는 테스트 월드 — 판정은 여전히 동작).
	const AVoxelWorld* VoxelWorld = nullptr;
	for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
	{
		VoxelWorld = *It;
		break; // 레벨에 1개 배치가 계약
	}
	if (!VoxelWorld)
	{
		return;
	}

	const UCA3DRuleSet* Rules = ResolveItemRules(GetWorld());
	PickupSphere->SetSphereRadius(Rules->ItemPickupRadiusCells * VoxelWorld->CellSize);
}
