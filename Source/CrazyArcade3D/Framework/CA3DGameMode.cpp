#include "Framework/CA3DGameMode.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"        // Framework→Gameplay 허용 (Framework→전부)
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Gameplay/SuddenDeath/SuddenDeathSubsystem.h"
#include "Gameplay/CA3DFeedback.h"                   // 매치 종료 큐 (Framework→Gameplay 허용)
#include "AI/BotController.h"                        // Framework→AI 허용 (Framework→전부)
#include "UI/CA3DHUD.h"                              // Framework→UI 허용 (Framework→전부)
#include "Voxel/VoxelWorld.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h" // 티어 판정의 -ExecCmds 타이밍 폴백 (GetBotFillTargetPlayers 주석)
#include "Misc/Parse.h"
#include "TimerManager.h"

// 봇 채우기 스위치 (Task 20). -1 = 룰셋(bFillWithBots·BotFillTargetPlayers)을 따른다.
// 콘솔 변수를 따로 둔 이유: 룰셋 기본이 false 라 헤드리스 데디 검증에서 에셋을 고치지 않고
// 켤 수단이 필요하다 — `-ExecCmds="ca3d.BotFill 4"` 한 줄로 4인 매치가 된다.
// (룰셋 에셋을 바꾸면 그 변경이 다른 모든 실행에 따라붙는다.)
static TAutoConsoleVariable<int32> CVarCA3DBotFill(
	TEXT("ca3d.BotFill"),
	-1,
	TEXT("봇으로 맞출 총 인원. -1 = 룰셋 따름(기본), 0 = 봇 없음, N = N명이 되도록 부족분을 채움"));

ACA3DGameMode::ACA3DGameMode()
{
	GameStateClass = ACA3DGameState::StaticClass();
	PlayerStateClass = ACA3DPlayerState::StaticClass(); // 승패 판정·결과 화면의 데이터 출처 (Task 18)

	// C++ 베이스 지정 (Task 10/11). 메시·애님·입력 에셋을 얹은 BP 서브클래스
	// (BP_CA3DCharacter / BP_CA3DPlayerController)로의 교체는 에디터에서
	// BP_CA3DGameMode 의 클래스 오버라이드로 한다 (BP 에 로직 금지 — 에셋 지정만).
	DefaultPawnClass = ACA3DCharacter::StaticClass();
	PlayerControllerClass = ACA3DPlayerController::StaticClass();

	// HUD 배선 (Task 25) — C++ 기본값으로 두면 **에디터 작업 0 으로도** 화면에 값이 뜬다.
	// BP_CA3DGameMode 는 이 값을 오버라이드하지 않으므로 그대로 상속받는다.
	// (WBP_Match 를 만든 뒤에는 BP_CA3DHUD 를 만들어 MatchWidgetClass 만 지정하면 된다.)
	HUDClass = ACA3DHUD::StaticClass();
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

	// 맵 크기 티어 (Task 22): 예상 총 인원 = max(현재 접속 인원, 봇 충원 목표).
	// 봇 목표는 SpawnFillBots 와 같은 헬퍼(GetBotFillTargetPlayers)를 공유한다 — 복붙하면
	// 티어 판정과 실제 충원 인원이 조용히 갈라진다. 접속 인원은 BeginPlay 시점 값이다:
	// 크기는 매치 시작에 한 번만 정한다 (이후 접속자는 정해진 맵에 들어온다 — 중간에 크기를
	// 바꾸면 이미 만든 지형·복제 값과 어긋난다).
	bool bTargetFromCVar = false;
	const int32 BotTargetPlayers = GetBotFillTargetPlayers(bTargetFromCVar);
	const int32 ExpectedPlayers = FMath::Max(GetNumPlayers(), BotTargetPlayers);
	const FIntVector TierSize = (ExpectedPlayers <= Rules->SmallMatchMaxPlayers)
		? Rules->MapSizeSmall
		: Rules->MapSizeLarge;
	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DGameMode: 맵 크기 티어 — 예상 %d명(접속 %d, 봇 목표 %d%s) → %s (%d,%d,%d), 소형 기준 %d명 이하"),
		ExpectedPlayers, GetNumPlayers(), BotTargetPlayers,
		bTargetFromCVar ? TEXT(" (ca3d.BotFill)") : TEXT(""),
		(ExpectedPlayers <= Rules->SmallMatchMaxPlayers) ? TEXT("소형") : TEXT("대형"),
		TierSize.X, TierSize.Y, TierSize.Z, Rules->SmallMatchMaxPlayers);

	VoxelWorld->ServerInitFromSeed(Seed, TierSize);
	SpawnCells = VoxelWorld->GetSpawnCells();
	if (SpawnCells.Num() == 0)
	{
		UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: 스폰 셀 0개 — 맵 생성 실패 여부를 확인할 것"));
	}

	// ── 5. 봇 채우기 예약 (Task 20) ──────────────────────────
	// 지금 세지 않고 미루는 이유 두 가지: (a) 사람이 들어와야 "부족분"이 정해진다,
	// (b) -ExecCmds 의 ca3d.BotFill 이 반영된 뒤에 판단해야 한다.
	// 지형 초기화가 끝난 뒤에 예약한다 — 그리드가 없으면 봇은 경로도 위험도 계산할 수 없다.
	GetWorldTimerManager().SetTimer(BotFillTimer,
		FTimerDelegate::CreateUObject(this, &ACA3DGameMode::SpawnFillBots),
		FMath::Max(Rules->BotFillDelaySeconds, KINDA_SMALL_NUMBER), false);

	// ── 6. 서든데스 예약 (Task 24) ───────────────────────────
	// 매치 시작 시각 기준 SuddenDeathStart 초 뒤. 지형 초기화 이후에 예약하는 이유는 봇과 같다 —
	// 그리드가 없으면 낙하 지점을 고를 수 없다.
	GetWorldTimerManager().SetTimer(SuddenDeathTimer,
		FTimerDelegate::CreateUObject(this, &ACA3DGameMode::StartSuddenDeath),
		FMath::Max(Rules->SuddenDeathStart, KINDA_SMALL_NUMBER), false);
}

void ACA3DGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 참가 등록을 Super 보다 "먼저" 한다 — Super::PostLogin 이 HandleStartingNewPlayer 로 폰을
	// 스폰하므로, 폰·컨트롤러가 색 인덱스를 읽는 후속 Task(20 봇 · 26 결과 화면)에서
	// 한 프레임 늦은 값을 보지 않게 한다. PlayerState 는 Login 단계에서 이미 만들어져 있다.
	RegisterParticipant(NewPlayer);

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

// ─── 참가 등록·봇 채우기 (Task 20, 서버 전용) ────────────────────────────────

void ACA3DGameMode::RegisterParticipant(AController* NewController)
{
	if (!HasAuthority()) return; // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다

	ACA3DPlayerState* NewState = NewController ? NewController->GetPlayerState<ACA3DPlayerState>() : nullptr;
	if (!NewState)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("ACA3DGameMode::RegisterParticipant: ACA3DPlayerState 없음 — PlayerStateClass 오버라이드(봇은 bWantsPlayerState)를 확인할 것 (승패 판정에서 제외된다)"));
		return;
	}

	NewState->ColorIndex = MatchParticipantCount; // 참가 순서 = 색 (GDD 5장, 1종 캐릭터 + 색 구분)
	NewState->FinalRank = 0;
	NewState->bAlive = true;
	++MatchParticipantCount;

	// AliveCount 는 GameState 의 값이지만 갱신 주체는 서버(GameMode) 단독이다 —
	// 클라·PlayerState 가 각자 세면 동시 사망에서 값이 갈린다.
	if (ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>())
	{
		++CA3DGameState->AliveCount;
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 참가자 입장 — 총 %d명, ColorIndex %d%s"),
		MatchParticipantCount, NewState->ColorIndex, NewState->IsABot() ? TEXT(" (봇)") : TEXT(""));
}

int32 ACA3DGameMode::GetBotFillTargetPlayers(bool& bOutFromCVar) const
{
	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();

	// 콘솔 변수가 룰셋을 덮어쓴다 (-1 = 룰셋 따름). 룰셋 기본이 false 인 이유는 룰셋 주석 참조 —
	// 봇이 조용히 켜져 있으면 기존 PIE·자동화 테스트의 인원수가 바뀐다.
	int32 Override = CVarCA3DBotFill.GetValueOnGameThread();

	// ⚠️ -ExecCmds 는 GameMode BeginPlay **이후에** 실행된다 — 봇 채우기를 타이머로 미룬
	// 이유 (b)가 바로 이것이다. 그런데 맵 크기 티어는 BeginPlay 에서 정해야 하므로(지형이
	// 스폰보다 먼저 필요) cvar 만 보면 티어 판정이 항상 봇 목표 0 으로 나온다
	// (2026-08-06 실측: `-ExecCmds="ca3d.BotFill 5"` 인데 티어는 "봇 목표 0 → 소형").
	// 그래서 cvar 가 아직 기본값이면 커맨드라인에서 같은 지시를 직접 읽는다.
	// SpawnFillBots(타이머 이후)는 cvar 가 이미 적용돼 있어 이 폴백을 타지 않는다 —
	// 어느 쪽이 읽혀도 같은 값이므로 티어와 실제 충원 인원이 갈라지지 않는다.
	// ⚠️ FParse::Value 를 커맨드라인에 직접 쓰면 안 된다 — 내부 Strifind 가
	// bSkipQuotedChars=true 라 **따옴표 안을 건너뛴다.** `ca3d.BotFill 5` 는
	// `-ExecCmds="..."` 따옴표 안에 있으므로 영영 못 찾는다 (2026-08-06 실측).
	// 그래서 -ExecCmds= 의 값(따옴표 해제는 FParse 가 해 준다)을 먼저 꺼낸 뒤 그 안에서 찾는다.
	if (Override < 0)
	{
		FString ExecCmds;
		if (FParse::Value(FCommandLine::Get(), TEXT("-ExecCmds="), ExecCmds))
		{
			const int32 KeyPos = ExecCmds.Find(TEXT("ca3d.BotFill"), ESearchCase::IgnoreCase);
			if (KeyPos != INDEX_NONE)
			{
				const int32 Parsed = FCString::Atoi(*ExecCmds + KeyPos + FCString::Strlen(TEXT("ca3d.BotFill")));
				if (Parsed >= 0)
				{
					Override = Parsed;
				}
			}
		}
	}

	bOutFromCVar = (Override >= 0);
	return bOutFromCVar
		? Override
		: (EffectiveRules->bFillWithBots ? EffectiveRules->BotFillTargetPlayers : 0);
}

void ACA3DGameMode::SpawnFillBots()
{
	if (!HasAuthority()) return; // 불변식 5

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 목표 인원 공식은 BeginPlay(맵 크기 티어)와 공유하는 헬퍼 하나뿐이다 (Task 22 — 헤더 주석).
	bool bFromCVar = false;
	const int32 TargetPlayers = GetBotFillTargetPlayers(bFromCVar);

	const int32 Needed = TargetPlayers - MatchParticipantCount;
	if (Needed <= 0)
	{
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 봇 채우기 없음 — 목표 %d명 / 현재 참가 %d명%s"),
			TargetPlayers, MatchParticipantCount, bFromCVar ? TEXT(" (ca3d.BotFill)") : TEXT(""));
		return;
	}

	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < Needed; ++Index)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient; // 컨트롤러는 레벨 저장 대상이 아니다

		ABotController* Bot = World->SpawnActor<ABotController>(ABotController::StaticClass(), Params);
		if (!Bot)
		{
			UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: ABotController 스폰 실패 — 봇 채우기 중단"));
			break;
		}

		// PlayerState 는 bWantsPlayerState 덕분에 스폰 시점(PostInitializeComponents)에 이미 붙어 있다.
		// 이름을 여기서 주는 이유: 결과 화면·킬 피드가 봇을 사람과 같은 목록으로 보여줘야
		// "한 판 완주" 검증이 눈으로 확인된다.
		if (ACA3DPlayerState* BotState = Bot->GetPlayerState<ACA3DPlayerState>())
		{
			BotState->SetIsABot(true);
			BotState->SetPlayerName(FString::Printf(TEXT("Bot %d"), ++BotNameCounter));
		}

		RegisterParticipant(Bot); // 사람과 **같은** 등록 경로 (ColorIndex·AliveCount)

		// 폰 부착도 엔진 기본 파이프라인 그대로 — ChoosePlayerStart_Implementation 의
		// 스폰 셀 배정이 그대로 재사용된다 (봇 전용 스폰 경로를 만들면 그만큼 검증이 빠진다).
		RestartPlayer(Bot);
		++SpawnedCount;
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 봇 %d대 투입 — 목표 %d명 / 총 참가 %d명%s"),
		SpawnedCount, TargetPlayers, MatchParticipantCount,
		bFromCVar ? TEXT(" (ca3d.BotFill)") : TEXT(""));
}

// ─── 서든데스 (Task 24, 서버 전용) ───────────────────────────────────────────

void ACA3DGameMode::StartSuddenDeath()
{
	if (!HasAuthority()) return; // 불변식 5

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || CA3DGameState->bMatchEnded)
	{
		return; // 서든데스 시각 전에 이미 승부가 났다 — 시작하지 않는다
	}

	// 플래그를 먼저 세운다 — 첫 낙하로 구멍이 나기 전에 클라 HUD 경고와
	// 낙사 원인 분기(ACA3DCharacter)가 이미 서든데스 상태를 보고 있어야 한다.
	CA3DGameState->bSuddenDeathActive = true;

	if (USuddenDeathSubsystem* SuddenDeath = World->GetSubsystem<USuddenDeathSubsystem>())
	{
		SuddenDeath->ServerStart();
	}
	else
	{
		UE_LOG(LogCA3D, Error, TEXT("ACA3DGameMode: USuddenDeathSubsystem 없음 — 서든데스 낙하 불가"));
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 서든데스 발동 (매치 경과 %.1f초)"),
		CA3DGameState->GetServerWorldTimeSeconds() - CA3DGameState->MatchStartServerTime);
}

void ACA3DGameMode::StopSuddenDeath()
{
	if (!HasAuthority()) return; // 불변식 5

	// 아직 발동 전이면 예약 자체를 취소한다 — 매치가 먼저 끝났는데 뒤늦게 시작되면 안 된다.
	GetWorldTimerManager().ClearTimer(SuddenDeathTimer);

	if (ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>())
	{
		CA3DGameState->bSuddenDeathActive = false;
	}

	if (UWorld* World = GetWorld())
	{
		if (USuddenDeathSubsystem* SuddenDeath = World->GetSubsystem<USuddenDeathSubsystem>())
		{
			SuddenDeath->ServerStop();
		}
	}
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
		// 우승자를 bMatchEnded 와 **같은 액터**에 기록한다 — 같은 번들로 원자 복제되어
		// 클라가 종료를 아는 순간 승패도 반드시 함께 안다 (GameState 헤더 주석, 무승부 오표시 수정).
		CA3DGameState->MatchWinner = Winner;
		CA3DGameState->bMatchEnded = true;
	}
	else if (bJudge && AliveAfter <= 0)
	{
		// 무승부 — 우승 자리를 비워 둔 채 종료한다 (MatchWinner == nullptr 가 곧 무승부).
		CA3DGameState->MatchWinner = nullptr;
		CA3DGameState->bMatchEnded = true;
	}

	// 매치가 끝났으면 서든데스도 끝난다 — 결과 화면이 떠 있는데 블록이 계속 떨어지면 안 된다.
	// (종료 판정이 두 갈래(우승/무승부)라 각 갈래에 넣지 않고 결과 플래그 하나로 본다.)
	if (CA3DGameState->bMatchEnded)
	{
		StopSuddenDeath();

		// ── 종료 큐: 매치당 1회 ──
		// 이 함수는 위쪽에서 bMatchEnded 를 보고 되돌아가므로(중복 종료 방지) 여기는 종료가
		// 확정된 그 한 번뿐이다. GameMode 는 **클라에 존재하지 않는** 액터라 클라 쪽 트리거가
		// 없다 — GameState 에 OnRep 을 다는 방법도 있지만 그러면 리슨 호스트가 빠져(서버에는
		// OnRep 이 오지 않는다) 결국 여기서 한 번 더 불러야 한다. 릴레이 한 발이 더 단순하다.
		// 위치는 없다 — 종료음은 감쇠 없는 2D 사운드가 전제다 (룰셋 MatchEndSound 주석).
		CA3DFeedback::ServerBroadcast(GetWorld(), ECA3DCue::MatchEnd, FVector::ZeroVector);
	}

	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DGameMode: 사망 해소 — 동시 %d명에게 공동 %d등 부여, 생존 %d → %d (참가 %d명)%s"),
		DeathCount, SharedRank, AliveBefore, CA3DGameState->AliveCount, MatchParticipantCount,
		CA3DGameState->bMatchEnded
			? (Winner ? TEXT(" · 매치 종료: 우승자 확정") : TEXT(" · 매치 종료: 무승부(우승자 없음)"))
			: TEXT(""));
}
