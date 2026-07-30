#include "Framework/CA3DGameMode.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"        // Framework→Gameplay 허용 (Framework→전부)
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Voxel/VoxelWorld.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

ACA3DGameMode::ACA3DGameMode()
{
	GameStateClass = ACA3DGameState::StaticClass();
	PlayerStateClass = ACA3DPlayerState::StaticClass(); // 승패 판정·결과 화면의 데이터 출처 (Task 18)

	// C++ 베이스 지정 (Task 10/11). 메시·애님·입력 에셋을 얹은 BP 서브클래스
	// (BP_CA3DCharacter / BP_CA3DPlayerController)로의 교체는 에디터에서
	// BP_CA3DGameMode 의 클래스 오버라이드로 한다 (BP 에 로직 금지 — 에셋 지정만).
	DefaultPawnClass = ACA3DCharacter::StaticClass();
	PlayerControllerClass = ACA3DPlayerController::StaticClass();
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
	// AliveCount 는 PostLogin 이 입장할 때마다 +1, 사망 해소가 -N 한다 (아래 Task 18 절).

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

void ACA3DGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 참가 등록을 Super 보다 "먼저" 한다 — Super::PostLogin 이 HandleStartingNewPlayer 로 폰을
	// 스폰하므로, 폰·컨트롤러가 색 인덱스를 읽는 후속 Task(20 봇 · 26 결과 화면)에서
	// 한 프레임 늦은 값을 보지 않게 한다. PlayerState 는 Login 단계에서 이미 만들어져 있다.
	if (HasAuthority()) // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다
	{
		ACA3DPlayerState* NewState = NewPlayer ? NewPlayer->GetPlayerState<ACA3DPlayerState>() : nullptr;
		if (NewState)
		{
			NewState->ColorIndex = MatchParticipantCount; // 접속 순서 = 색 (GDD 5장, 1종 캐릭터 + 색 구분)
			NewState->FinalRank = 0;
			NewState->bAlive = true;
			++MatchParticipantCount;

			// AliveCount 는 GameState 의 값이지만 갱신 주체는 서버(GameMode) 단독이다 —
			// 클라·PlayerState 가 각자 세면 동시 사망에서 값이 갈린다.
			if (ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>())
			{
				++CA3DGameState->AliveCount;
			}

			UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 참가자 입장 — 총 %d명, ColorIndex %d"),
				MatchParticipantCount, NewState->ColorIndex);
		}
		else
		{
			UE_LOG(LogCA3D, Warning,
				TEXT("ACA3DGameMode::PostLogin: ACA3DPlayerState 없음 — PlayerStateClass 오버라이드를 확인할 것 (승패 판정에서 제외된다)"));
		}
	}

	Super::PostLogin(NewPlayer);

	// TODO(중도 이탈): Logout 에서 MatchParticipantCount·AliveCount 정리 — 이번 Task 범위 밖.
	// GDD 6.2 는 재접속이 없으므로 "이탈 = 그 자리에서 탈락(순위 부여)" 인지 "참가 인원에서 제외"
	// 인지 규칙부터 확정해야 한다. 확정 전에 구현하면 승패 판정이 조용히 어긋난다.
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

// ─── 승패 판정 (Task 18, 서버 전용) ──────────────────────────────────────────

void ACA3DGameMode::NotifyPlayerDeath(ACA3DPlayerState* DeadState)
{
	if (!HasAuthority()) return; // 불변식 5

	if (!DeadState)
	{
		return; // 봇(Task 20 이전)·PlayerState 없는 폰 — 사망 자체는 StatusComponent 에 이미 반영됐다
	}

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || CA3DGameState->bMatchEnded)
	{
		return; // 종료된 매치에는 순위를 더 매기지 않는다 (중복 종료 방지)
	}

	if (!DeadState->bAlive || DeadState->FinalRank != 0)
	{
		return; // 이미 탈락 처리됨 — 중복 통지 무시
	}

	PendingDeaths.AddUnique(DeadState);

	// ⚠️ 여기서 즉시 순위를 매기지 않는 이유(사용자 확정 규칙):
	// 한 폭발의 물줄기에 갇힌 여러 명이 **같은 프레임에** 익사 타이머가 만료되는 상황이
	// 실제로 발생한다. 통지 순서대로 바로 등수를 주면 델리게이트 실행 순서(= 사실상 임의)가
	// 등수를 결정해 버린다. 한 프레임 분을 모아 다음 틱에 한 번에 해소해야 "동시 사망"을
	// 동시로 인정할 수 있다. 예약은 프레임당 1회 (bDeathResolveScheduled).
	if (!bDeathResolveScheduled)
	{
		bDeathResolveScheduled = true;
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &ACA3DGameMode::ResolvePendingDeaths));
	}
}

void ACA3DGameMode::ResolvePendingDeaths()
{
	bDeathResolveScheduled = false;

	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || CA3DGameState->bMatchEnded)
	{
		PendingDeaths.Reset();
		return; // 중복 종료 방지 — 종료 후 도착한 통지는 버린다
	}

	// 예약 이후 무효화된 항목(파괴된 PlayerState·다른 경로로 이미 처리된 항목)을 걸러낸다.
	TArray<TObjectPtr<ACA3DPlayerState>> Deaths;
	Deaths.Reserve(PendingDeaths.Num());
	for (const TObjectPtr<ACA3DPlayerState>& Each : PendingDeaths)
	{
		if (IsValid(Each) && Each->bAlive && Each->FinalRank == 0)
		{
			Deaths.Add(Each);
		}
	}
	PendingDeaths.Reset();

	const int32 DeathCount = Deaths.Num();
	if (DeathCount == 0)
	{
		return;
	}

	const int32 AliveBefore = CA3DGameState->AliveCount;
	const int32 AliveAfter  = AliveBefore - DeathCount;

	// 공동 등수 — 경기 순위 관례: 동점자는 자기들이 차지한 자리 중 **가장 좋은 자리**를 받는다.
	// (4명 중 1명 사망 → 4등, 남은 3명 중 2명 동시 사망 → 둘 다 공동 2등 → 최종 1·2·2·4)
	//
	// 하한 2 가 무승부 장치다: 마지막 남은 전원이 함께 죽으면 AliveBefore - N + 1 이 1 이하로
	// 내려가는데, 그대로 주면 "다 죽었는데 우승자가 있다"가 된다. 1등 자리를 비워 두면
	// 무승부가 별도 플래그 없이 "FinalRank == 1 인 사람이 없음"으로 표현된다 (GameState 주석).
	const int32 SharedRank = FMath::Max(2, AliveBefore - DeathCount + 1);

	for (const TObjectPtr<ACA3DPlayerState>& Each : Deaths)
	{
		Each->bAlive = false;
		Each->FinalRank = SharedRank;
	}

	CA3DGameState->AliveCount = FMath::Max(AliveAfter, 0);

	// ── 종료 판정 ──
	// 참가 인원이 최소치 이상이었을 때만 — 1인 PIE 테스트에서 죽자마자 매치가 끝나면
	// 지형·폭탄 튜닝을 혼자 돌려볼 수가 없다.
	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();
	const bool bJudge = MatchParticipantCount >= EffectiveRules->MinPlayersForMatchEnd;

	ACA3DPlayerState* Winner = nullptr;
	if (bJudge && AliveAfter == 1)
	{
		// 남은 한 명이 우승. PlayerArray 는 GameState 가 들고 있는 전체 목록이고,
		// 아직 탈락하지 않은 항목은 정의상 하나뿐이다.
		for (APlayerState* Each : CA3DGameState->PlayerArray)
		{
			ACA3DPlayerState* Candidate = Cast<ACA3DPlayerState>(Each);
			if (Candidate && Candidate->bAlive && Candidate->FinalRank == 0)
			{
				Winner = Candidate;
				break;
			}
		}

		if (Winner)
		{
			Winner->FinalRank = 1;
		}
		else
		{
			UE_LOG(LogCA3D, Warning,
				TEXT("ACA3DGameMode: 생존 1명인데 해당 PlayerState 를 못 찾음 — AliveCount 와 PlayerArray 가 어긋났다"));
		}
		CA3DGameState->bMatchEnded = true;
	}
	else if (bJudge && AliveAfter <= 0)
	{
		// 무승부 — 우승 자리를 비워 둔 채 종료한다 (FinalRank == 1 부재가 곧 무승부).
		CA3DGameState->bMatchEnded = true;
	}

	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DGameMode: 사망 해소 — 동시 %d명에게 공동 %d등 부여, 생존 %d → %d (참가 %d명)%s"),
		DeathCount, SharedRank, AliveBefore, CA3DGameState->AliveCount, MatchParticipantCount,
		CA3DGameState->bMatchEnded
			? (Winner ? TEXT(" · 매치 종료: 우승자 확정") : TEXT(" · 매치 종료: 무승부(우승자 없음)"))
			: TEXT(""));
}
