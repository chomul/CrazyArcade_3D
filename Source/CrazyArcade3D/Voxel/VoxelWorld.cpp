#include "Voxel/VoxelWorld.h"

#include "CrazyArcade3D.h"
#include "MapGen/FallbackMapGenerator.h"   // Voxel→MapGen 참조는 설계서 2.2가 확정 — .cpp 에서만 include
#include "Framework/CA3DRuleSet.h"         // Voxel→Framework 참조도 동일 — .cpp 에서만 include
#include "Framework/CA3DGameState.h"       // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만 include
#include "Voxel/HISMVoxelRenderer.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "EngineUtils.h"   // ⚠️ 임시 (Task 16에서 제거) — 디버그 콘솔 명령의 TActorIterator
#include "Engine/Engine.h" // ⚠️ 임시 (Task 16에서 제거) — GEngine->GetWorldContexts

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	// 지형은 모든 클라에 항상 필요 — 거리 기반 relevancy 컬링으로 액터가 사라지면
	// 파괴 Multicast를 놓쳐 그리드가 어긋난다. 항상 relevant 로 고정한다.
	bAlwaysRelevant = true;

	// HISM 부착 기준점 겸 액터 트랜스폼 기준 — 루트 없이 스폰되면 GetActorLocation이 무의미해진다.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 렌더러는 생성자 CreateDefaultSubobject (Task 07) — BP_VoxelWorld 서브클래스에서
	// BlockMeshes 디폴트를 편집할 수 있어야 하기 때문 (BeginPlay NewObject는 BP 디폴트를 못 받는다).
	// 데디 서버에서는 BeginPlay에서 파괴한다.
	HISMRendererComponent = CreateDefaultSubobject<UHISMVoxelRenderer>(TEXT("HISMRenderer"));
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();

	// 그리드 초기화는 여기서 하지 않는다 — 정식 흐름은 ACA3DGameMode(서버)가 시드를 정해
	// ServerInitFromSeed 를 호출하는 것 (Task 09). GameMode BeginPlay 가 이 액터의
	// BeginPlay 보다 먼저 돌아도 문제없다 (아래 catch-up 렌더 빌드가 처리).

	if (IsRunningDedicatedServer())
	{
		// 데디 서버는 시각이 필요 없다 (불변식 5) — 렌더러 컴포넌트를 파괴해 메모리 절약.
		// Renderer 는 nullptr 유지 (호출부는 전부 null 가드).
		if (HISMRendererComponent)
		{
			HISMRendererComponent->DestroyComponent();
			HISMRendererComponent = nullptr;
		}
		return;
	}

	Renderer = HISMRendererComponent.Get();

	// 순서 함정: 클라에서 OnRep_Seed 가 BeginPlay 보다 먼저 도착할 수 있다.
	// 그 경우 InitGridFromSeed 시점엔 Renderer 가 아직 없어 렌더 빌드를 건너뛰었으므로
	// 여기서 따라잡기 빌드를 수행한다.
	if (bGridInitialized && Renderer)
	{
		Renderer->BuildFromGrid(Grid);
	}
}

void AVoxelWorld::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AVoxelWorld, Seed);
}

// ─── 좌표 변환 ─────────────────────────────────────────────
// 셀 크기를 아는 유일한 곳. 전부 액터 위치 기준.

FIntVector AVoxelWorld::WorldToCell(const FVector& W) const
{
	// FVector는 double — FloorToInt(double)는 int64를 반환하므로 FloorToInt32로 명시.
	const FVector Local = (W - GetActorLocation()) / CellSize;
	return FIntVector(
		FMath::FloorToInt32(Local.X),
		FMath::FloorToInt32(Local.Y),
		FMath::FloorToInt32(Local.Z));
}

FVector AVoxelWorld::CellToWorld(const FIntVector& C) const
{
	// 셀 중심: 각 축 (C + 0.5) * CellSize. 왕복 보장: WorldToCell(CellToWorld(C)) == C.
	return GetActorLocation() + FVector(
		(C.X + 0.5f) * CellSize,
		(C.Y + 0.5f) * CellSize,
		(C.Z + 0.5f) * CellSize);
}

FVector AVoxelWorld::CellToWorldFloor(const FIntVector& C) const
{
	// X/Y는 셀 중심, Z만 셀 바닥면.
	return GetActorLocation() + FVector(
		(C.X + 0.5f) * CellSize,
		(C.Y + 0.5f) * CellSize,
		C.Z * CellSize);
}

// ─── 서버 전용 ─────────────────────────────────────────────

void AVoxelWorld::ServerInitFromSeed(uint32 InSeed)
{
	if (!HasAuthority()) return; // 불변식 5 — 상태를 바꾸는 함수는 서버 전용

	Seed = InSeed; // 복제 프로퍼티 기록 — 클라는 OnRep_Seed로 동일 맵을 재생성한다
	InitGridFromSeed();
}

void AVoxelWorld::ServerDestroyBlocks(const TArray<FIntVector>& Cells)
{
	if (!HasAuthority()) return; // 불변식 5

	ApplyDestruction(Cells);
	MulticastOnBlocksDestroyed(Cells);
}

// ─── 리플리케이션 수신 ─────────────────────────────────────

void AVoxelWorld::OnRep_Seed()
{
	// 클라: 서버와 동일한 결정론 생성 경로로 맵 구성.
	// InitGridFromSeed 내부에서 렌더 빌드 + PendingDestroyQueue flush까지 수행한다
	// (알려진 함정: 파괴 Multicast가 이 OnRep보다 먼저 도착할 수 있다).
	InitGridFromSeed();
}

void AVoxelWorld::MulticastOnBlocksDestroyed_Implementation(const TArray<FIntVector>& Cells)
{
	// 리슨 서버/서버 로컬 실행 중복 방지 — 서버는 ServerDestroyBlocks에서
	// 이미 ApplyDestruction을 수행했으므로 여기서 또 하면 이중 파괴가 된다.
	if (HasAuthority()) return;

	if (bGridInitialized)
	{
		ApplyDestruction(Cells);
	}
	else
	{
		// 그리드 초기화 전 선도착 — 큐에 쌓아두고 OnRep_Seed 직후 flush.
		PendingDestroyQueue.Append(Cells);
	}
}

// ─── 내부 공통 경로 ───────────────────────────────────────

void AVoxelWorld::InitGridFromSeed()
{
	// 서버(ServerInitFromSeed)·클라(OnRep_Seed) 공통 경로 — 결정론 생성기이므로
	// 같은 Seed + 같은 룰셋이면 양쪽 그리드가 비트 단위로 동일하다.

	if (bGridInitialized)
	{
		return; // 재진입 가드 — 아래 지연 재시도 타이머와 OnRep 중복 호출 대비
	}

	// 룰셋 해석: 생성기가 Rules(MapSize)를 실제로 소비하므로 서버·클라가 "같은 에셋"을
	// 봐야 결정론이 성립한다. 출처는 GameState 에 복제된 에셋 포인터 하나뿐이다 (Task 08/09).
	const UCA3DRuleSet* Rules = nullptr;
	const AGameStateBase* GameStateBase = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (const ACA3DGameState* CA3DGameState = Cast<ACA3DGameState>(GameStateBase))
	{
		Rules = CA3DGameState->Rules;
	}

	if (!Rules)
	{
		if (HasAuthority())
		{
			// 정식 흐름(ACA3DGameMode)은 GameState->Rules 세팅 "후" ServerInitFromSeed 를
			// 호출하므로, 서버에서 여기 도달은 GameState 없는 자동화 테스트 월드나
			// GameMode 미설정 맵뿐이다. 서버는 기다릴 대상이 없으니 기본 룰셋으로 즉시
			// 진행한다 (기존 테스트들의 기준 경로와 동일한 기본값).
			UE_LOG(LogCA3D, Log,
				TEXT("AVoxelWorld: GameState 룰셋 없음(서버) — 기본 룰셋으로 생성 (정식 흐름은 ACA3DGameMode 경유)"));
			Rules = NewObject<UCA3DRuleSet>(this);
		}
		else if (GameStateBase && !GameStateBase->IsA<ACA3DGameState>())
		{
			// GameState 가 이미 도착했는데 우리 타입이 아니다 — Rules 는 영원히 오지 않는다.
			// 이 구성이면 서버도 위의 기본 룰셋 폴백을 탔을 것이므로 같은 기본값으로 맞춘다.
			UE_LOG(LogCA3D, Warning,
				TEXT("AVoxelWorld: GameState 가 ACA3DGameState 아님 — 기본 룰셋으로 생성"));
			Rules = NewObject<UCA3DRuleSet>(this);
		}
		else
		{
			// 알려진 함정(리플리케이션 순서): OnRep_Seed 가 GameState 액터/Rules 복제보다 먼저
			// 도착할 수 있다. 서버와 다른 룰셋으로 생성하면 결정론이 깨지므로 생성하지 않고
			// 다음 틱에 재시도한다 (에셋 참조는 경로로 복제 — GameState 만 도착하면 즉시 유효.
			// 고정 지연값이 없는 next-tick 재시도가 가장 단순한 안전책이다).
			UE_LOG(LogCA3D, Verbose, TEXT("AVoxelWorld: 클라 룰셋 미도착 — 그리드 생성을 다음 틱으로 지연"));
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &AVoxelWorld::InitGridFromSeed));
			return;
		}
	}

	// Task 22에서 이 한 줄만 절차 생성기(UProceduralMapGenerator)로 바꾼다.
	UFallbackMapGenerator* Generator = NewObject<UFallbackMapGenerator>();

	// 스폰 셀은 보관 — 서버 GameMode 가 GetSpawnCells 로 소비한다 (Task 09).
	// 아이템 배치(OutItems)는 아직 소비처가 없어(Task 23) 버린다.
	TArray<FItemPlacement> OutItems;

	if (!Generator->Generate(Seed, Rules, Grid, SpawnCells, OutItems))
	{
		UE_LOG(LogCA3D, Error, TEXT("AVoxelWorld: Seed %u 맵 생성 실패"), Seed);
		return;
	}

	bGridInitialized = true;

	// 렌더러는 클라에서만 존재 (데디는 nullptr — null 가드).
	if (Renderer)
	{
		Renderer->BuildFromGrid(Grid);
	}

	// 선도착 파괴 큐 flush — 서버는 실제로 안 쌓이지만 공통 경로를 유지한다.
	if (PendingDestroyQueue.Num() > 0)
	{
		UE_LOG(LogCA3D, Log, TEXT("AVoxelWorld: 선도착 파괴 셀 %d개 flush"), PendingDestroyQueue.Num());
		ApplyDestruction(PendingDestroyQueue);
		PendingDestroyQueue.Empty();
	}
}

void AVoxelWorld::ApplyDestruction(const TArray<FIntVector>& Cells)
{
	// 불변식 1 — 파괴의 단일 경로. 이 함수 밖에서 파괴 목적의 Grid.Set 금지.
	for (const FIntVector& Cell : Cells)
	{
		Grid.Set(Cell, EBlockType::Empty);

		if (Renderer)
		{
			Renderer->RemoveBlock(Cell, Grid);
		}
	}
}

// ─── 디버그 콘솔 명령 ──────────────────────────────────────
// ⚠️ 임시 (Task 16 ABomb 전까지) — 아직 파괴 수단이 없어 체크리스트 07 PIE 검증용.
// 정식 경로(ServerDestroyBlocks → ApplyDestruction + Multicast)를 그대로 태운다 (불변식 1).
#if !UE_BUILD_SHIPPING

namespace CA3DVoxelDebug
{
	static AVoxelWorld* FindVoxelWorld(UWorld* World)
	{
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			return *It;
		}
		UE_LOG(LogCA3D, Warning, TEXT("ca3d.Destroy*: 월드에 AVoxelWorld 없음"));
		return nullptr;
	}

	// PIE 멀티 인스턴스에서 명령은 클라 월드에서 실행될 수 있다. 파괴는 서버에서
	// 시작해야 하므로(불변식 1) 같은 프로세스의 권한 있는 VoxelWorld를 찾는다.
	// 셀 좌표는 결정론 생성이라 서버·클라 동일 — 클라에서 계산한 셀을 그대로 써도 된다.
	static AVoxelWorld* FindAuthoritativeVoxelWorld()
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* W = Ctx.World();
			if (!W || (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game))
			{
				continue;
			}
			for (TActorIterator<AVoxelWorld> It(W); It; ++It)
			{
				if (It->HasAuthority())
				{
					return *It;
				}
			}
		}
		return nullptr;
	}

	static void DestroyCell(const FIntVector& Cell)
	{
		AVoxelWorld* VoxelWorld = FindAuthoritativeVoxelWorld();
		if (!VoxelWorld)
		{
			// 별도 프로세스 데디 서버로 PIE 중이면 이 프로세스엔 서버 월드가 없다.
			UE_LOG(LogCA3D, Warning,
				TEXT("ca3d.Destroy*: 이 프로세스에 서버 권한 VoxelWorld 없음 — "
					 "별도 프로세스 데디 서버 PIE에선 사용 불가. 리슨 서버/스탠드얼론으로 실행할 것"));
			return;
		}
		if (!VoxelWorld->IsSolid(Cell))
		{
			UE_LOG(LogCA3D, Warning, TEXT("ca3d.Destroy*: (%d, %d, %d) 는 솔리드가 아님"),
				Cell.X, Cell.Y, Cell.Z);
			return;
		}

		UE_LOG(LogCA3D, Log, TEXT("ca3d.Destroy*: (%d, %d, %d) 타입 %d 파괴"),
			Cell.X, Cell.Y, Cell.Z, static_cast<int32>(VoxelWorld->GetBlock(Cell)));
		VoxelWorld->ServerDestroyBlocks({ Cell });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCA3DDestroyBlockCmd(
	TEXT("ca3d.DestroyBlock"),
	TEXT("셀 좌표의 블록 1개를 정식 파괴 경로로 제거. 사용: ca3d.DestroyBlock X Y Z"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 3)
			{
				UE_LOG(LogCA3D, Warning, TEXT("사용법: ca3d.DestroyBlock X Y Z"));
				return;
			}
			const FIntVector Cell(
				FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
			CA3DVoxelDebug::DestroyCell(Cell);
		}));

static FAutoConsoleCommandWithWorldAndArgs GCA3DDestroyAimCmd(
	TEXT("ca3d.DestroyAim"),
	TEXT("카메라 시선의 블록 1개를 정식 파괴 경로로 제거 (블록 메시에 콜리전 필요)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			// 트레이스·좌표 변환은 명령이 실행된 로컬 월드(클라 가능)에서, 파괴는
			// DestroyCell이 찾은 서버 월드에서 — 셀 좌표는 양쪽이 동일하다.
			AVoxelWorld* VoxelWorld = CA3DVoxelDebug::FindVoxelWorld(World);
			if (!VoxelWorld)
			{
				return;
			}
			APlayerController* PC = World->GetFirstPlayerController();
			if (!PC)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.DestroyAim: PlayerController 없음"));
				return;
			}

			FVector ViewLoc;
			FRotator ViewRot;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);

			FHitResult Hit;
			const FVector TraceEnd = ViewLoc + ViewRot.Vector() * 100000.f;
			if (!World->LineTraceSingleByChannel(Hit, ViewLoc, TraceEnd, ECC_Visibility))
			{
				UE_LOG(LogCA3D, Warning,
					TEXT("ca3d.DestroyAim: 시선에 히트 없음 (블록 메시에 콜리전이 있는지 확인)"));
				return;
			}

			// ImpactPoint는 셀 경계면 위 — 노멀 반대로 반 셀 밀어 넣어 셀 내부 좌표로 만든다.
			const FVector Inside = Hit.ImpactPoint - Hit.ImpactNormal * (VoxelWorld->CellSize * 0.5f);
			CA3DVoxelDebug::DestroyCell(VoxelWorld->WorldToCell(Inside));
		}));

#endif // !UE_BUILD_SHIPPING
