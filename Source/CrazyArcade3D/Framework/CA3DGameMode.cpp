#include "Framework/CA3DGameMode.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelWorld.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

ACA3DGameMode::ACA3DGameMode()
{
	GameStateClass = ACA3DGameState::StaticClass();

	// ⚠️ 임시 — DefaultPawnClass 는 엔진 기본(ADefaultPawn) 유지. Task 10에서 ACA3DCharacter 로 교체.
	// ⚠️ 임시 — PlayerControllerClass 도 엔진 기본 유지. Task 11에서 ACA3DPlayerController 로 교체.
}

void ACA3DGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority()) return; // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다

	// ── 1. 룰셋 확보 ─────────────────────────────────────────
	// BP 미지정이면 게임을 멈추는 대신 기본값으로 진행한다 (에디터 연결 누락 방어).
	if (!Rules)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("ACA3DGameMode: Rules 미지정 — BP_CA3DGameMode 에 DA_Rules_Default 를 지정할 것. 기본값으로 진행"));
		Rules = NewObject<UCA3DRuleSet>(this);
	}

	// ── 2. GameState 에 룰셋 포인터·매치 시작 시각 세팅 ───────
	// 반드시 VoxelWorld 초기화 "이전"이어야 한다 — InitGridFromSeed 가 GameState->Rules 를 읽는다.
	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	check(CA3DGameState); // 생성자에서 GameStateClass 를 지정했으므로 계약 위반 시에만 실패
	CA3DGameState->Rules = Rules;
	CA3DGameState->MatchStartServerTime = CA3DGameState->GetServerWorldTimeSeconds();
	// AliveCount 갱신은 Task 18(승패 판정)에서.

	// ── 3. 시드 결정 ─────────────────────────────────────────
	// FMath::Rand() 가 허용되는 유일한 곳 — 시드 자체는 결정론 대상이 아니다 (불변식 4).
	// Rand() 는 15비트(0~32767)라 두 번 뽑아 상·하위를 합성해 범위를 넓힌다.
	const uint32 Seed = bUseFixedSeed
		? static_cast<uint32>(FixedSeed)
		: ((static_cast<uint32>(FMath::Rand()) << 15) | static_cast<uint32>(FMath::Rand()));
	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 매치 시드 %u%s"),
		Seed, bUseFixedSeed ? TEXT(" (고정 시드 모드)") : TEXT(""));

	// ── 4. VoxelWorld 탐색·캐시 → 초기화 → 스폰 셀 보관 ──────
	for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
	{
		VoxelWorld = *It;
		break; // 레벨에 1개 배치가 계약 — 첫 번째만 사용
	}
	if (!VoxelWorld)
	{
		UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: 레벨에 AVoxelWorld 없음 — 맵 생성·스폰 배정 불가"));
		return;
	}

	VoxelWorld->ServerInitFromSeed(Seed);
	SpawnCells = VoxelWorld->GetSpawnCells();
	if (SpawnCells.Num() == 0)
	{
		UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: 스폰 셀 0개 — 맵 생성 실패 여부를 확인할 것"));
	}
}

AActor* ACA3DGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!VoxelWorld || SpawnCells.Num() == 0)
	{
		// VoxelWorld 미배치 등 비정상 — 엔진 기본 탐색(레벨의 PlayerStart)으로 폴백.
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	// 스폰 셀을 순서대로 배정. 정원(생성기 출력 8개) 초과 접속은 처음부터 재순환.
	const int32 CellIndex = NextSpawnIndex % SpawnCells.Num();
	++NextSpawnIndex;

	// 레벨에 PlayerStart 액터가 없으므로(맵은 시드로 생성) 스폰 셀 위치에 임시 APlayerStart
	// 를 만들어 반환한다 — RestartPlayerAtPlayerStart 가 반환 액터의 트랜스폼을 그대로 쓰므로
	// 엔진 스폰 파이프라인을 건드리지 않는 가장 단순한 전달 방식이다.
	// 셀 인덱스별 1개만 만들어 캐시·재사용한다 (배정 때마다 액터가 누적되지 않게).
	if (SpawnStartActors.Num() < SpawnCells.Num())
	{
		SpawnStartActors.SetNum(SpawnCells.Num());
	}
	if (APlayerStart* Cached = SpawnStartActors[CellIndex])
	{
		return Cached;
	}

	// 폰 원점은 콜리전 중심 — 셀 바닥면 + 기본 폰 콜리전 반높이만큼 올려 바닥에 파묻히지 않게 한다.
	const APawn* PawnCDO = DefaultPawnClass ? DefaultPawnClass->GetDefaultObject<APawn>() : nullptr;
	const float HalfHeight = PawnCDO ? PawnCDO->GetDefaultHalfHeight() : 0.f;
	const FVector StartLocation =
		VoxelWorld->CellToWorldFloor(SpawnCells[CellIndex]) + FVector(0.f, 0.f, HalfHeight);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(
		APlayerStart::StaticClass(), StartLocation, FRotator::ZeroRotator, Params);
	if (!Start)
	{
		UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: 임시 PlayerStart 스폰 실패 — 엔진 기본 탐색으로 폴백"));
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	SpawnStartActors[CellIndex] = Start;
	return Start;
}
