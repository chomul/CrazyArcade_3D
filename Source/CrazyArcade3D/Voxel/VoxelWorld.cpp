#include "Voxel/VoxelWorld.h"

#include "CrazyArcade3D.h"
#include "MapGen/FallbackMapGenerator.h"   // Voxel→MapGen 참조는 설계서 2.2가 확정 — .cpp 에서만 include
#include "MapGen/ProcMapGenerator.h"       // 절차 생성기 (Task 22) — 실패 시 위 폴백으로 전환
#include "Framework/CA3DRuleSet.h"         // Voxel→Framework 참조도 동일 — .cpp 에서만 include
#include "Framework/CA3DGameState.h"       // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만 include
#include "Voxel/HISMVoxelRenderer.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/Package.h"   // 🏁 해시 로그의 PIE 인스턴스 식별 (GetPIEInstanceID)
#include "EngineUtils.h"   // ca3d.GridHash 의 TActorIterator
#include "Engine/Engine.h" // ca3d.GridHash 의 GEngine->GetWorldContexts

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
	// ⚠️ 데디 서버에서도 살려둔다 — 이 컴포넌트가 지형의 유일한 컬리전이다 (BeginPlay 주석).
	HISMRendererComponent = CreateDefaultSubobject<UHISMVoxelRenderer>(TEXT("HISMRenderer"));
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();

	// 그리드 초기화는 여기서 하지 않는다 — 정식 흐름은 ACA3DGameMode(서버)가 시드를 정해
	// ServerInitFromSeed 를 호출하는 것 (Task 09). GameMode BeginPlay 가 이 액터의
	// BeginPlay 보다 먼저 돌아도 문제없다 (아래 catch-up 렌더 빌드가 처리).

	// ⚠️ 데디 서버에서도 이 컴포넌트를 **파괴하면 안 된다.** 이름은 "렌더러"지만
	// 지형의 **유일한 컬리전**이기도 하다 — HISM 인스턴스가 곧 블록의 물리 형상이다.
	//
	// 예전 구현은 "시각 전용이니 데디에서는 파괴한다"(불변식 5)고 판단해 지웠는데,
	// 그 결과 **데디 서버에 바닥이 없어 모든 캐릭터가 MOVE_Falling 으로 지형을 통과**했다
	// (2026-08-02 실측: 봇 5대가 지상 판정 3.6%, Z 가 바닥면 아래로 내려감).
	// 서버가 이동 권한을 가지므로 이건 게임 전체가 성립하지 않는 버그다.
	//
	// **PIE 로는 절대 안 잡힌다**: PIE 의 "Play as Dedicated Server" 는 에디터 프로세스라
	// IsRunningDedicatedServer() 가 false 다 → HISM 이 살아 있어 멀쩡히 걸어다닌다.
	// 진짜 서버 exe(또는 에디터 `-server`)에서만 재현된다.
	//
	// 그래서 데디에서도 Renderer 로 배선한다. 인스턴스 추가·제거(RemoveBlock)를 계속
	// 태워야 **부순 블록의 컬리전이 남지 않는다** — 파괴가 서버에서만 안 반영되면
	// 서버는 없는 벽에 막히고 클라는 지나간다.
	// 렌더링 비용은 어차피 0이다: 데디는 씬이 없어 프리미티브가 렌더 상태를 만들지 않는다.
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
	DOREPLIFETIME(AVoxelWorld, GridSize);
	DOREPLIFETIME(AVoxelWorld, DestroyedCells);
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

FVector AVoxelWorld::WorldToCellFloat(const FVector& W) const
{
	// CellToWorld 의 역변환을 반올림 없이. (CellToWorld(C) 를 넣으면 C + 0.5 가 나온다)
	return (W - GetActorLocation()) / CellSize;
}

bool AVoxelWorld::SetCellFade(const FIntVector& Cell, float Value)
{
	if (!Renderer)
	{
		return false; // 데디 서버 등 렌더러 없음 — 가림 페이드는 클라 시각 전용
	}

	return Renderer->SetCellFade(Cell, Value);
}

float AVoxelWorld::GetCellFade(const FIntVector& Cell) const
{
	return Renderer ? Renderer->GetCellFade(Cell) : -1.f;
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

	// 레거시 단일 인자 경로 — 크기는 룰셋 MapSize (기존 테스트·GameMode 미경유 실행의 기준).
	// 룰셋 출처는 InitGridFromSeed 와 동일하게 GameState 를 먼저 본다 (없으면 기본 룰셋).
	const ACA3DGameState* CA3DGameState =
		GetWorld() ? Cast<ACA3DGameState>(GetWorld()->GetGameState()) : nullptr;
	const UCA3DRuleSet* SizeRules =
		(CA3DGameState && CA3DGameState->Rules) ? CA3DGameState->Rules.Get() : GetDefault<UCA3DRuleSet>();
	ServerInitFromSeed(InSeed, SizeRules->MapSize);
}

void AVoxelWorld::ServerInitFromSeed(uint32 InSeed, const FIntVector& InSize)
{
	if (!HasAuthority()) return; // 불변식 5

	Seed = InSeed;     // 복제 프로퍼티 기록 — 클라는 OnRep_Seed로 동일 맵을 재생성한다
	GridSize = InSize; // 크기도 복제 — 클라는 서버가 정한 이 값만 믿는다 (헤더 주석, Task 22)
	InitGridFromSeed();
}

void AVoxelWorld::ServerDestroyBlocks(const TArray<FIntVector>& Cells)
{
	if (!HasAuthority()) return; // 불변식 5

	ApplyDestruction(Cells);

	// 이력 누적이 먼저다 — 이 배열이 중간 접속자가 보는 "지금까지 무슨 일이 있었나" 전부다.
	DestroyedCells.Append(Cells);

	MulticastOnBlocksDestroyed(Cells);
}

bool AVoxelWorld::ConsumeItemPlacement(const FIntVector& Cell, EItemType& OutType)
{
	if (!HasAuthority()) return false; // 불변식 5 — 목록을 바꾸는 함수

	// 배치 수는 파괴 블록 수의 일부(수십 개)라 선형 탐색으로 충분하다. TMap 을 쓰지 않는 이유는
	// 생성기 출력이 TArray 이고(불변식 4), 여기서 자료구조를 바꾸면 순서 근거가 하나 더 늘기 때문.
	for (int32 Index = 0; Index < ItemPlacements.Num(); ++Index)
	{
		if (ItemPlacements[Index].Cell == Cell)
		{
			OutType = ItemPlacements[Index].Type;
			ItemPlacements.RemoveAt(Index); // 노출은 셀당 1회 — 남겨두면 재파괴 시 복제된다
			return true;
		}
	}
	return false;
}

// ─── 리플리케이션 수신 ─────────────────────────────────────

void AVoxelWorld::OnRep_Seed()
{
	// 클라: 서버와 동일한 결정론 생성 경로로 맵 구성.
	// InitGridFromSeed 내부에서 렌더 빌드 + PendingDestroyQueue flush까지 수행한다
	// (알려진 함정: 파괴 Multicast가 이 OnRep보다 먼저 도착할 수 있다).
	InitGridFromSeed();
}

void AVoxelWorld::OnRep_GridSize()
{
	// 보통 Seed 와 같은 초기 번들로 도착하지만 순서 계약은 없다 — 어느 쪽이 늦어도
	// InitGridFromSeed 의 재진입 가드·미도착 지연 재시도가 한 번만 생성되게 맞춘다 (Task 22).
	InitGridFromSeed();
}

void AVoxelWorld::OnRep_DestroyedCells()
{
	// 중간 접속자의 따라잡기 경로. 접속 시점에 이 배열의 **전체 값**이 한 번에 도착하므로,
	// 여기서 아직 적용 안 된 셀만 골라 넣으면 그 클라의 지형이 서버와 같아진다.
	// 이미 접속해 있던 클라에게는 보통 할 일이 없다 — Multicast 가 먼저 처리했기 때문이다.
	if (!bGridInitialized)
	{
		// 그리드 생성 전 도착 — Seed 와 이 배열은 같은 초기 번들에 실려 순서가 보장되지 않는다.
		PendingDestroyQueue.Append(DestroyedCells);
		return;
	}

	const TArray<FIntVector> Missing = FilterUnapplied(DestroyedCells);
	if (Missing.Num() > 0)
	{
		UE_LOG(LogCA3D, Log, TEXT("AVoxelWorld: 파괴 이력 따라잡기 %d칸 (중간 접속 보정)"), Missing.Num());
		ApplyDestruction(Missing);
	}
}

void AVoxelWorld::MulticastOnBlocksDestroyed_Implementation(const TArray<FIntVector>& Cells)
{
	// 리슨 서버/서버 로컬 실행 중복 방지 — 서버는 ServerDestroyBlocks에서
	// 이미 ApplyDestruction을 수행했으므로 여기서 또 하면 이중 파괴가 된다.
	if (HasAuthority()) return;

	if (bGridInitialized)
	{
		// 복제 이력(OnRep_DestroyedCells)이 먼저 처리했을 수 있으므로 남은 것만 적용한다.
		const TArray<FIntVector> Fresh = FilterUnapplied(Cells);
		if (Fresh.Num() > 0)
		{
			ApplyDestruction(Fresh);
		}
	}
	else
	{
		// 그리드 초기화 전 선도착 — 큐에 쌓아두고 OnRep_Seed 직후 flush.
		PendingDestroyQueue.Append(Cells);
	}
}

TArray<FIntVector> AVoxelWorld::FilterUnapplied(const TArray<FIntVector>& Cells) const
{
	TArray<FIntVector> Result;
	Result.Reserve(Cells.Num());
	for (const FIntVector& Cell : Cells)
	{
		// 아직 솔리드로 남아 있다 = 이 파괴가 아직 반영되지 않았다.
		// AddUnique 인 이유: 입력 자체에 같은 셀이 두 번 들어올 수 있다(Multicast + 복제 이력이
		// 한 큐에 쌓인 경우). 그대로 두면 "이번 파괴 N칸" 집계가 부풀어 해시 로그 판독이 어긋난다.
		if (Grid.Get(Cell) != EBlockType::Empty)
		{
			Result.AddUnique(Cell);
		}
	}
	return Result;
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

	// 크기 확인 (Task 22): 정식 경로에서는 ServerInitFromSeed 가 이미 GridSize 를 채웠다.
	if (GridSize == FIntVector::ZeroValue)
	{
		if (HasAuthority())
		{
			// 서버 권한인데 미설정 — 테스트가 OnRep_Seed 를 직접 부르는 경로 등.
			// 레거시와 같은 기준(룰셋 MapSize)으로 즉시 채운다 (서버는 기다릴 대상이 없다).
			GridSize = Rules->MapSize;
		}
		else
		{
			// 클라: 크기 미도착 — 위 룰셋 미도착과 같은 next-tick 재시도 패턴.
			// 여기서 클라가 로컬 인원수로 크기를 유추하면 **중간 접속자가 다른 맵을 만든다**
			// (접속 시점 인원 ≠ 매치 시작 시점 인원) — 크기는 서버 결정·복제만 믿는다 (헤더 주석).
			UE_LOG(LogCA3D, Verbose, TEXT("AVoxelWorld: 클라 GridSize 미도착 — 그리드 생성을 다음 틱으로 지연"));
			GetWorldTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &AVoxelWorld::InitGridFromSeed));
			return;
		}
	}

	// 생성기 선택 (Task 22): 절차 생성기 우선, 실패 시 폴백 (GDD 4.2 안전장치 3).
	// **생성기 실패도 결정론적이다** — 같은 Seed·Size·Rules 면 어느 머신에서든 똑같이
	// 실패하므로 서버와 클라가 같은 폴백 경로를 탄다 (한쪽만 폴백을 타는 상황이 없다).
	//
	// 스폰 셀은 서버 GameMode 가 GetSpawnCells 로(Task 09), 아이템 배치는 폭발 처리가
	// ConsumeItemPlacement 로(Task 23) 소비한다 — 둘 다 여기서 보관만 한다.
	UProcMapGenerator* ProcGenerator = NewObject<UProcMapGenerator>();
	bool bGenerated = ProcGenerator->Generate(Seed, GridSize, Rules, Grid, SpawnCells, ItemPlacements);
	if (!bGenerated)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("AVoxelWorld: Seed %u 절차 생성 실패 — 폴백 생성기로 전환 (GDD 4.2 안전장치 3)"), Seed);
		UFallbackMapGenerator* FallbackGenerator = NewObject<UFallbackMapGenerator>();
		bGenerated = FallbackGenerator->Generate(Seed, GridSize, Rules, Grid, SpawnCells, ItemPlacements);
	}
	if (!bGenerated)
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
	// 같은 셀이 Multicast 와 복제 이력 양쪽에서 들어와 중복될 수 있어 걸러서 적용한다.
	if (PendingDestroyQueue.Num() > 0)
	{
		const TArray<FIntVector> Fresh = FilterUnapplied(PendingDestroyQueue);
		UE_LOG(LogCA3D, Log, TEXT("AVoxelWorld: 선도착 파괴 셀 %d개 flush (중복 제외 %d개 적용)"),
			PendingDestroyQueue.Num(), Fresh.Num());
		if (Fresh.Num() > 0)
		{
			ApplyDestruction(Fresh);
		}
		PendingDestroyQueue.Empty();
	}
}

// ─── 🏁 그리드 동기화 해시 (설계서 5장 11번 — 1주차 마감 게이트 / 2주차 데디 검증) ───────
#if !UE_BUILD_SHIPPING
namespace CA3DGridHash
{
	// "데디 서버 / PIE 0" 처럼 인스턴스를 식별한다 — 클라가 2개 이상이면 넷모드만으로 구분이 안 된다.
	static FString InstanceLabel(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("월드 없음");
		}

		const TCHAR* NetModeName;
		switch (World->GetNetMode())
		{
		case NM_ListenServer:    NetModeName = TEXT("리슨 서버");   break;
		case NM_DedicatedServer: NetModeName = TEXT("데디 서버");   break;
		case NM_Client:          NetModeName = TEXT("클라이언트"); break;
		default:                 NetModeName = TEXT("스탠드얼론"); break;
		}

		const int32 PIEInstance = World->GetPackage() ? World->GetPackage()->GetPIEInstanceID() : INDEX_NONE;
		return PIEInstance == INDEX_NONE
			? FString(NetModeName)
			: FString::Printf(TEXT("%s / PIE %d"), NetModeName, PIEInstance);
	}

	// Blocks 는 X→Y→Z 평탄화 고정 순서의 uint8 배열이라 CRC 가 플랫폼 무관하게 결정론적이다.
	static uint32 Compute(const FVoxelGrid& Grid, int32& OutSolidCount)
	{
		OutSolidCount = 0;
		for (const uint8 Block : Grid.Blocks)
		{
			if (Block != static_cast<uint8>(EBlockType::Empty))
			{
				++OutSolidCount;
			}
		}
		return FCrc::MemCrc32(Grid.Blocks.GetData(), Grid.Blocks.Num());
	}
}
#endif // !UE_BUILD_SHIPPING

void AVoxelWorld::ApplyDestruction(const TArray<FIntVector>& Cells)
{
	// 불변식 1 — 파괴의 단일 경로. 이 함수 밖에서 파괴 목적의 Grid.Set 금지.
	//
	// **알리는 것은 요청받은 칸이 아니라 실제로 비워진 칸이다.** 같은 셀이 두 번 들어오는 것은
	// 정상 경로다 — 중간 접속 클라의 DestroyedCells 따라잡기, 연쇄 폭발의 범위 겹침, 서든데스와
	// 폭탄이 같은 칸을 치는 경우. 그때 요청 목록을 그대로 알리면 **아무것도 안 부서졌는데
	// "부서졌다" 는 알림**이 나가고, 구독자(프리뷰 갱신·파괴음)가 헛돈다.
	TArray<FIntVector> ChangedCells;
	ChangedCells.Reserve(Cells.Num());
	for (const FIntVector& Cell : Cells)
	{
		if (Grid.Get(Cell) == EBlockType::Empty)
		{
			continue; // 이미 비어 있다 — 이번 호출로 바뀐 것이 없다
		}

		Grid.Set(Cell, EBlockType::Empty);
		ChangedCells.Add(Cell);

		if (Renderer)
		{
			Renderer->RemoveBlock(Cell, Grid);
		}
	}

	if (ChangedCells.Num() > 0)
	{
		OnGridChanged.Broadcast(ChangedCells);

#if !UE_BUILD_SHIPPING
		// 🏁 게이트 근거를 자동으로 남긴다 — 서버·클라가 **각자 같은 이 함수**를 통과하므로
		// (불변식 1) 같은 순번(#N)의 해시를 대조하면 지형 동기화가 증명된다. 콘솔 명령을
		// 잊어도, 파괴 전에 재는 실수를 해도 로그에 남는다는 게 요점.
		++DestructionApplyCount;
		int32 SolidCount = 0;
		const uint32 Hash = CA3DGridHash::Compute(Grid, SolidCount);
		UE_LOG(LogCA3D, Display, TEXT("그리드 해시 #%d [%s]: %08X (솔리드 %d칸 / 이번 파괴 %d칸)"),
			DestructionApplyCount, *CA3DGridHash::InstanceLabel(GetWorld()), Hash, SolidCount, ChangedCells.Num());
#endif
	}
}

// ─── 디버그 콘솔 명령 ──────────────────────────────────────
#if !UE_BUILD_SHIPPING

// 🏁 그리드 동기화 검증 (설계서 5장 11번, 체크리스트 17 게이트) — 임시 아님, 2주차 데디 검증에도 쓴다.
//
// **한 번 실행으로 프로세스 안의 모든 인스턴스를 찍고 스스로 비교한다.** 창마다 따로 실행하는
// 방식은 못 쓴다 — 콘솔 명령의 World 인자는 포커스한 PIE 창과 무관하게 한 월드로 해석될 수 있고
// (2026-07-30 실측: 호스트 창·클라 창에서 각각 실행했는데 양쪽 다 클라 월드로 잡혔다),
// 그러면 "같은 창을 두 번 읽은 값"을 동기화 근거로 착각하게 된다.
// Blocks 는 X→Y→Z 평탄화 고정 순서라 CRC 가 결정론적이다.
static FAutoConsoleCommandWithWorldAndArgs GCA3DGridHashCmd(
	TEXT("ca3d.GridHash"),
	TEXT("프로세스 내 모든 인스턴스(서버·클라)의 복셀 그리드 CRC 를 찍고 일치 여부를 판정 (설계서 5장 11번)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			int32 Reported = 0;
			uint32 FirstHash = 0;
			bool bAllMatch = true;

			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UWorld* W = Ctx.World();
				if (!W || (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game))
				{
					continue;
				}

				AVoxelWorld* VoxelWorld = nullptr;
				for (TActorIterator<AVoxelWorld> It(W); It; ++It)
				{
					VoxelWorld = *It;
					break; // 레벨에 1개 배치가 계약
				}
				if (!VoxelWorld)
				{
					continue;
				}

				const FString Label = CA3DGridHash::InstanceLabel(W); // 자동 로그와 같은 라벨 — 대조 편의
				if (!VoxelWorld->IsGridInitialized())
				{
					UE_LOG(LogCA3D, Warning,
						TEXT("ca3d.GridHash [%s]: 그리드 미초기화 — OnRep_Seed 이전이거나 서버 미초기화"), *Label);
					continue;
				}

				const FVoxelGrid& Grid = VoxelWorld->GetGrid();
				int32 SolidCount = 0;
				const uint32 Hash = CA3DGridHash::Compute(Grid, SolidCount);

				UE_LOG(LogCA3D, Display, TEXT("ca3d.GridHash [%s]: %08X (크기 %d×%d×%d, 솔리드 %d칸)"),
					*Label, Hash, Grid.Size.X, Grid.Size.Y, Grid.Size.Z, SolidCount);

				if (Reported == 0)
				{
					FirstHash = Hash;
				}
				else if (Hash != FirstHash)
				{
					bAllMatch = false;
				}
				++Reported;
			}

			// 판정 — 로그 한 줄로 게이트 통과 여부가 결정된다 ("잘 되는 것 같음" 금지).
			if (Reported == 0)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.GridHash: 그리드를 가진 인스턴스 없음 — PIE 중에 실행할 것"));
			}
			else if (Reported == 1)
			{
				UE_LOG(LogCA3D, Warning,
					TEXT("ca3d.GridHash: 인스턴스 1개만 발견 — 비교 불가. 2인 PIE(Listen Server + 클라 1)로 실행할 것 "
						 "(별도 프로세스 데디 서버면 서버 로그를 따로 확인)"));
			}
			else if (bAllMatch)
			{
				UE_LOG(LogCA3D, Display,
					TEXT("ca3d.GridHash: ✅ 일치 — 인스턴스 %d개 전부 %08X (지형 동기화 통과, 설계서 5장 11번)"),
					Reported, FirstHash);
			}
			else
			{
				UE_LOG(LogCA3D, Error,
					TEXT("ca3d.GridHash: ❌ 불일치 — 인스턴스 %d개의 해시가 다르다. 지형 비동기 버그 (게이트 실패, 2주차 진행 금지)"),
					Reported);
			}
		}));

#endif // !UE_BUILD_SHIPPING
