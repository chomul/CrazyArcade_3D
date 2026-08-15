#include "Framework/CA3DGameMode.h"

#include "CrazyArcade3D.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "Gameplay/Character/CA3DCharacter.h"        // Framework→Gameplay 허용 (Framework→전부)
#include "Gameplay/Character/CA3DPlayerController.h"
#include "Gameplay/Character/StatusComponent.h"      // 이탈자 사망 처리의 기존 단일 경로 (ServerKill)
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
		// 진짜 비정상이다 — 여기서도 게이트는 열어야 한다. 안 열면 보류된 컨트롤러가 폰을
		// 영영 못 받는다. 열어 두면 StartPlay 의 해소가 ChoosePlayerStart 의 엔진 기본 폴백으로
		// 내보낸다 (= 이 결함 수정 전과 같은 동작이지만, "진짜 비정상" 일 때만 그렇다).
		bMatchStartResolved = true;
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

	// 자동 배정 스트림 — 맵 시드 파생 (헤더 주석: 고정 시드 모드에서 배정까지 재현).
	// 페이즈가 로비 뒤로 밀려도 시드 파생은 여기서 한 번뿐이다 — 로비 대기 시간이 배정에
	// 영향을 주면 고정 시드 재현이 깨진다 (불변식 4 계열).
	CharacterAssignStream.Initialize(static_cast<int32>(Seed * 7919u + 1u));

	// ── 5. 로비 페이즈 판가름 (Task 41) ───────────────────────
	// 룰셋 bUseLobby → 로비를 띄우고 **아무 타이머도 걸지 않는다**: 로비는 시간 제한이 없고
	// (방장이 시작을 누를 때까지 무한 대기) 캐릭터 선택·봇 채우기·서든데스는 전부 그 뒤다.
	// bUseLobby == false 면 **기존 동작 그대로** 곧바로 StartCharacterSelect() — C++ 기본값이
	// 곧 기존 흐름이라 룰셋을 안 만진 실행·기존 자동화 테스트는 한 줄도 달라지지 않는다
	// (룰셋 bUseLobby 주석의 무회귀 근거. Task 36 의 CharacterSelectDuration 과 같은 전략).
	if (Rules->bUseLobby)
	{
		// 갱신 주체는 GameMode 단독 (bCharacterSelectActive 와 같은 관례).
		CA3DGameState->bLobbyActive = true;

		UE_LOG(LogCA3D, Log,
			TEXT("ACA3DGameMode: 로비 페이즈 시작 — 방장의 시작 요청까지 대기 (시간 제한 없음)"));

		// ⚠️ 여기서 봇·서든데스·선택 페이즈 타이머를 **하나도** 예약하지 않는다.
		// 하나라도 걸면 로비에서 기다리는 동안 그 시계가 흘러 "로비를 오래 끌면 시작하자마자
		// 서든데스" 가 된다. 매치의 모든 시계는 EndLobby → StartCharacterSelect 부터 흐른다.
	}
	else
	{
		StartCharacterSelect();
	}

	// ── 6. 스폰 게이트 열기 ──────────────────────────────────────
	// 여기서부터 SpawnCells 가 확정이다. 실제 스폰(보류분 해소)은 StartPlay 가 한다.
	bMatchStartResolved = true;
}

void ACA3DGameMode::StartPlay()
{
	// Super 안에서 액터 BeginPlay 가 일괄 실행된다 (GameState->HandleBeginPlay →
	// AWorldSettings::NotifyBeginPlay). 우리 BeginPlay 도 그 안에서 돌아 게이트가 열린다.
	Super::StartPlay();

	// 해소를 **BeginPlay 안이 아니라 여기서** 하는 이유: NotifyBeginPlay 는 액터 목록을
	// 순회하는 중이고 `World->SetBegunPlay(true)` 는 순회가 끝난 뒤에 세워진다. 그 안에서
	// 폰을 스폰하면 AActor::PostActorConstruction 이 "월드가 아직 BeginPlay 전"으로 보고
	// BeginPlay 를 미루는데, 순회는 이미 그 액터를 지나쳤을 수 있다 — 폰의 BeginPlay 가
	// 통째로 누락될 수 있는 자리다. Super 가 끝난 뒤면 월드가 이미 BegunPlay 라 안전하다.
	FlushPendingSpawns();
}

void ACA3DGameMode::PostLogin(APlayerController* NewPlayer)
{
	// 참가 등록을 Super 보다 "먼저" 한다 — Super::PostLogin 이 HandleStartingNewPlayer 로 폰을
	// 스폰하므로, 폰·컨트롤러가 색 인덱스를 읽는 후속 Task(20 봇 · 26 결과 화면)에서
	// 한 프레임 늦은 값을 보지 않게 한다. PlayerState 는 Login 단계에서 이미 만들어져 있다.
	RegisterParticipant(NewPlayer);

	Super::PostLogin(NewPlayer);

	// 중도 이탈은 Logout → HandleParticipantLeft 가 처리한다 (2026-08-10 규칙 확정).
}

void ACA3DGameMode::Logout(AController* Exiting)
{
	// 우리 정리를 **Super 보다 먼저** 한다 — Super::Logout 은 GameModeLogoutEvent 브로드캐스트
	// 한 줄뿐이고(AGameModeBase), 그 리스너가 생기면 이미 갱신된 상태를 보는 편이 맞다.
	HandleParticipantLeft(Exiting);

	Super::Logout(Exiting);
}

void ACA3DGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// ⚠️ **PostLogin 은 BeginPlay 보다 먼저 돌 수 있다.** 엔진의 두 진입 경로가 모두
	// `LocalPlayer->SpawnPlayActor`(→ Login → PostLogin) 를 `World->BeginPlay()` **앞에서**
	// 부른다: `UEngine::LoadMap`(스탠드얼론·패키징·리슨 호스트)과
	// `UGameInstance::StartPlayInEditorGameInstance`(PIE) 둘 다 그렇다.
	//
	// 그 시점에는 VoxelWorld 도 SpawnCells 도 비어 있어 ChoosePlayerStart 가 엔진 기본 탐색으로
	// 폴백하는데, 이 레벨에는 PlayerStart 가 없다(맵을 시드로 생성하므로). 그러면
	// `AGameModeBase::FindPlayerStart_Implementation` 이 **WorldSettings(원점)** 를 돌려주고
	// 폰은 지형 밖에 스폰돼 낙사한다 (2026-08-09 -game 실측: 입장 32ms 뒤 시드 로그,
	// 그 2.7초 뒤 KillZ 낙사).
	//
	// **PIE 에서는 이 결함이 드러나지 않는다** — 에디터가 PIE 월드에 `APlayerStartPIE` 를
	// 뷰포트 카메라 위치에 미리 스폰해 두고(`UGameInstance::InitializeForPlayInEditor` →
	// `SpawnPlayFromHereStart`, 기본 설정 `PlayLocation_CurrentCameraLocation`),
	// 엔진 폴백이 그것을 **최우선으로** 고르기 때문이다. 카메라는 보통 아레나 위쪽이라
	// 30ms 뒤 완성되는 지형 위로 떨어져 살아남는다. 즉 순서는 PIE 와 -game 이 **같고**,
	// PIE 만 우연히 그물이 하나 더 있다 — HISM·데디 가드와 같은 계열의 "PIE 로는 안 잡히는" 함정.
	//
	// 그래서 폴백에 기대지 않고 **대기**로 푼다: 지형이 판가름 나기 전에는 폰을 만들지 않는다.
	// 참가 등록(RegisterParticipant)은 PostLogin 에서 이미 끝났으므로 ColorIndex·AliveCount·
	// MatchParticipantCount·GetNumPlayers()(컨트롤러를 센다)는 대기 여부와 무관하게 정확하다.
	// 캐릭터 선택 페이즈 중 입장도 **같은 게이트**로 보류한다 (Task 36) — 페이즈 중에는 아무도
	// 폰을 받지 않으므로, 늦게 들어온 사람도 먼저 온 사람과 같은 순간(EndCharacterSelect ⑤)에
	// 함께 스폰된다. 별도 대기 목록을 만들지 않는다 — 목록이 두 개면 해소도 두 벌이 된다.
	// 로비 페이즈 중 입장도 **같은 게이트**로 보류한다 (Task 41) — 맵에 캐릭터가 미리 서 있으면
	// 안 된다. 목록도 해소 지점도 늘리지 않는다: 로비 중 늦게 들어온 사람은 먼저 온 사람과
	// 정확히 같은 순간에 함께 스폰된다.
	const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	const bool bHoldForLobby = CA3DGameState && CA3DGameState->bLobbyActive;
	const bool bHoldForCharacterSelect = CA3DGameState && CA3DGameState->bCharacterSelectActive;

	if (!bMatchStartResolved || bHoldForLobby || bHoldForCharacterSelect)
	{
		PendingSpawnControllers.AddUnique(NewPlayer);
		UE_LOG(LogCA3D, Log,
			TEXT("ACA3DGameMode: %s — 폰 스폰 보류 (대기 %d명)"),
			!bMatchStartResolved ? TEXT("지형 준비 전 입장")
				: (bHoldForLobby ? TEXT("로비 페이즈 중 입장") : TEXT("캐릭터 선택 페이즈 중 입장")),
			PendingSpawnControllers.Num());
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

// ─── 스폰 위치의 단일 출처 ───────────────────────────────────────────────────
//
// ⚠️ 게이트(위)만으로는 부족했다 — **로그인 단계가 이미 원점을 굳혀 놓기 때문**이다
// (2026-08-09 -game 실측: 게이트가 정상 동작해 "보류 → 해소" 로그까지 찍혔는데도 낙사).
//
// 엔진 흐름:
//   AGameModeBase::InitNewPlayer (Login, PostLogin 보다 **앞**)
//     └─ UpdatePlayerStartSpot                     (GameModeBase.cpp:787)
//          └─ FindPlayerStart → ChoosePlayerStart  → (맵이 아직 없으니) 엔진 폴백 = WorldSettings(원점)
//          └─ **Player->StartSpot = 그 액터**       (GameModeBase.cpp:848)
//   … 나중에 …
//   RestartPlayer → FindPlayerStart_Implementation (GameModeBase.cpp:1149)
//     └─ ShouldSpawnAtStartSpot(Player)            (GameModeBase.cpp:1142)
//          == (Player->StartSpot != nullptr) == true
//     └─ return Player->StartSpot                  ← ChoosePlayerStart 를 **쳐다보지도 않는다**
//
// 그래서 두 곳을 다 막는다. 하나만 막으면 안 되는 이유는 각 함수 주석에.

bool ACA3DGameMode::UpdatePlayerStartSpot(AController* /*Player*/, const FString& /*Portal*/, FString& OutErrorMessage)
{
	// 로그인 시점의 "미리 골라 둔 자리" 는 이 게임에서 **정의상 무의미**하다 — 맵은 시드로
	// 생성되고, 첫 사람의 로그인은 생성(BeginPlay)보다 먼저 돌 수 있다. 그때 고른 자리는
	// 항상 지형 밖이다.
	//
	// ShouldSpawnAtStartSpot 만 false 로 막아도 낙사는 사라지지만, 이 함수를 그대로 두면
	// **지형이 이미 준비된 뒤에 접속한 사람(= 데디 서버의 모든 사람)이 스폰 셀을 한 칸씩
	// 헛돌린다.** 로그인에서 ChoosePlayerStart 가 한 번(NextSpawnIndex++), 실제 스폰에서 또
	// 한 번 도니 1인당 2칸을 먹는다 — 셀 8개에 사람이 5명 이상이면 인덱스가 되감겨 **두 명이
	// 같은 칸에 겹쳐 스폰**된다(SpawnStartActors 캐시가 같은 액터를 돌려주므로 완전히 같은 좌표).
	// 스폰 배정은 폰을 만드는 그 순간에 딱 한 번만 돌아야 한다.
	//
	// true 를 돌려주는 이유: false 면 InitNewPlayer 가 "Could not find a starting spot" 경고를
	// 남긴다(GameModeBase.cpp:789). 자리를 못 찾은 게 아니라 **아직 고를 때가 아닌 것**이다.
	OutErrorMessage.Reset();
	return true;
}

bool ACA3DGameMode::ShouldSpawnAtStartSpot(AController* /*Player*/)
{
	// 스폰 위치는 **항상** ChoosePlayerStart(= 생성기가 준 스폰 셀)가 정한다.
	//
	// UE 5.8 에서 Player->StartSpot 에 값을 넣는 곳은 UpdatePlayerStartSpot 한 곳뿐이고
	// (GameModeBase.cpp:848 — 스폰 성공 후 불리는 InitStartSpot_Implementation 은 :1388 에서
	// **비어 있다**), 위에서 그 한 곳을 막았다. 그러니 지금 당장은 이 오버라이드 없이도 동작한다.
	// 그래도 두는 이유는 그 사실이 **우리가 통제하지 못하는 전제**이기 때문이다:
	//   · InitStartSpot 은 "start spawn actor 를 초기화하라" 는 용도로 열려 있는
	//     BlueprintNativeEvent 훅이다 — BP 나 후속 Task 가 채우면 그때부터 배정이 우회된다.
	//   · RestartPlayer 는 FindPlayerStart 가 실패하면 StartSpot 을 폴백으로 읽는다(:1256).
	//   · 엔진 갱신으로 :1388 이 채워지면 아무 경고 없이 되살아난다.
	// 그때 증상은 "부활할 때마다 처음 배정받은 칸(또는 원점)에 영구히 못 박힘" 이고, 이번처럼
	// 로그는 정상인데 위치만 틀린 형태로 나온다. 배정 규칙 진입점을 하나로 못 박아 둔다.
	return false;
}

void ACA3DGameMode::FlushPendingSpawns()
{
	if (!HasAuthority()) return; // 불변식 5

	// 게이트가 아직 안 열렸으면 아무것도 하지 않는다 — 여기서 스폰하면 결함 그대로다.
	if (!bMatchStartResolved || PendingSpawnControllers.Num() == 0)
	{
		return;
	}

	// 로비·캐릭터 선택 페이즈 중에는 해소하지 않는다 (Task 41 / Task 36) — StartPlay 의 호출이
	// 여기로 들어와도 보류가 유지되고, 각 페이즈 종료(EndLobby ③ / EndCharacterSelect ④ 플래그
	// 해제 → ⑤ 재호출)가 해소한다. **해소 지점이 늘어난 것이 아니라 같은 문을 두 번 두드린다.**
	if (const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
		CA3DGameState && (CA3DGameState->bLobbyActive || CA3DGameState->bCharacterSelectActive))
	{
		return;
	}

	// 목록을 **먼저** 옮겨 비운다. 두 번 스폰되면 폰이 둘 남거나 AliveCount 가 어긋나
	// 승패 판정이 조용히 깨진다 — 재진입·중복 호출 어느 쪽으로도 두 번 돌 수 없게 만든다.
	TArray<TObjectPtr<APlayerController>> Waiting = MoveTemp(PendingSpawnControllers);
	PendingSpawnControllers.Reset();

	int32 SpawnedCount = 0;
	for (const TObjectPtr<APlayerController>& Each : Waiting)
	{
		APlayerController* PC = Each.Get();
		if (!IsValid(PC) || PC->GetPawn())
		{
			continue; // 대기 중 이탈했거나 다른 경로로 이미 폰을 받았다
		}

		// 사람이 정상 순서로 들어왔을 때와 **같은 경로**로 스폰한다 (게이트는 이미 열렸으므로
		// 위 오버라이드는 그대로 Super 로 내려간다). 관전 전용·재시작 불가 판정도 엔진 그대로.
		HandleStartingNewPlayer(PC);
		++SpawnedCount;
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 보류 폰 스폰 해소 — 대기 %d명 중 %d명 스폰"),
		Waiting.Num(), SpawnedCount);
}

AActor* ACA3DGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!VoxelWorld || SpawnCells.Num() == 0)
	{
		// ⚠️ 이 폴백은 **진짜 비정상 전용**이다 (레벨에 AVoxelWorld 미배치, 맵 생성 실패).
		// 이 경고가 뜨면 파야 한다 — 정상 흐름에서는 한 번도 뜨지 않는다.
		//
		// 한때 로그인 단계(AGameModeBase::InitNewPlayer → UpdatePlayerStartSpot)가 매 접속마다
		// 여기를 두드려 "정상인데도 뜨는 경고" 였다. 그건 경고가 아니라 **결함의 증상**이었고
		// (굳은 원점 StartSpot → 낙사), 지금은 UpdatePlayerStartSpot 오버라이드가 그 호출 자체를
		// 없앴다. 그러니 이 경고를 "원래 뜨는 것" 으로 넘기지 말 것.
		//
		// 그리고 "아직 BeginPlay 가 안 돌아서 비어 있는" 정상적인 순서 문제도 여기로 오지 않는다 —
		// 그건 HandleStartingNewPlayer_Implementation 의 대기 게이트가 막는다.
		// 이 레벨에는 PlayerStart 가 없으므로(맵을 시드로 생성) 엔진 폴백은 결국
		// WorldSettings(원점)로 떨어진다 = 지형 밖.
		UE_LOG(LogCA3D, Warning,
			TEXT("ACA3DGameMode::ChoosePlayerStart: 스폰 셀 없음(VoxelWorld %s) — 엔진 기본 탐색으로 폴백. "
			     "이 레벨에는 PlayerStart 가 없어 원점 스폰이 된다"),
			VoxelWorld ? TEXT("있음") : TEXT("없음"));
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

	// ── 방장 배정 (Task 41) ──
	// 방장 = **첫 입장 사람**. 봇은 절대 방장이 되지 않는다 — 봇이 방장이면 사람이 아무도
	// 준비하지 않아도 판이 시작될 수 있고, 애초에 봇에게는 시작 버튼을 누를 입력이 없다.
	// (봇 판별을 두 가지로 보는 이유: SpawnFillBots 는 등록 **전에** SetIsABot(true) 를 하지만,
	//  다른 경로로 만들어진 ABotController 는 그 표시가 없을 수 있다. 하나라도 해당하면 봇이다.)
	// 로비를 쓰지 않는 매치에서도 값 자체는 참("첫 입장 사람")이라 조건을 걸지 않는다 —
	// 로비가 없으면 이 값을 읽는 곳(로비 UI·TryStartMatchFromLobby)이 아예 돌지 않는다.
	const bool bIsBot = NewState->IsABot() || (NewController && NewController->IsA<ABotController>());
	if (!bIsBot && !HasLobbyHost())
	{
		NewState->bIsHost = true;
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 방장 배정 — %s (첫 입장)"), *NewState->GetPlayerName());
	}

	// AliveCount 는 GameState 의 값이지만 갱신 주체는 서버(GameMode) 단독이다 —
	// 클라·PlayerState 가 각자 세면 동시 사망에서 값이 갈린다.
	if (ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>())
	{
		++CA3DGameState->AliveCount;
	}

	// 페이즈 종료 후 늦은 입장 (Task 36): 미배정이면 남은 풀에서 즉시 자동 배정 — 종료 시
	// 자동 배정과 **같은 함수**를 쓴다 (경로 두 벌 금지). 페이즈 진행 중이면 본인이 고르므로
	// 여기서는 건드리지 않고, 페이즈 없는 매치(bCharacterSelectPhaseUsed false)는 배정 자체가
	// 없다 (기존 흐름 무회귀). 봇(EndCharacterSelect 의 SpawnFillBots 경유)은 그 시점에 아직
	// 페이즈 플래그가 서 있어 여기를 지나가고, EndCharacterSelect ② 가 배정한다.
	if (bCharacterSelectPhaseUsed && NewState->CharacterIndex == INDEX_NONE)
	{
		if (const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
			CA3DGameState && !CA3DGameState->bCharacterSelectActive)
		{
			AutoAssignUnassignedCharacters();
		}
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 참가자 입장 — 총 %d명, ColorIndex %d%s"),
		MatchParticipantCount, NewState->ColorIndex, NewState->IsABot() ? TEXT(" (봇)") : TEXT(""));
}

// ─── 중도 이탈 (2026-08-10 사용자 확정 규칙, 서버 전용) ──────────────────────
//
// 규칙: **나간 사람은 그 자리에서 사망 처리해 순위를 부여하고, 결과 화면에 "탈주" 로 표시한다.**
// 이 함수는 AController::Destroyed → AGameModeBase::Logout 경로로만 불린다 (Controller.cpp:595).
// 그 시점에는 아직 UnPossess 전이라 폰도, PlayerState 도 그대로 붙어 있다 — 그래서 여기서
// 기존 사망 경로를 그대로 태울 수 있다.
void ACA3DGameMode::HandleParticipantLeft(AController* Exiting)
{
	if (!HasAuthority()) return; // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다

	// ── ① 보류 스폰 목록에서 제거 — PlayerState 유무보다 **먼저** ──
	// 지형이 준비되기 전에 들어왔다가 폰을 받기 전에 나간 사람이 목록에 남아 있으면,
	// FlushPendingSpawns 가 파괴 중인 컨트롤러에 폰을 붙이려 든다. AddUnique 로 넣었으므로
	// 같은 항목은 최대 하나지만, 그 김에 이미 죽은 항목(TObjectPtr 가 null 로 남긴 것)도 걷어낸다.
	const int32 RemovedPending = PendingSpawnControllers.RemoveAll(
		[Exiting](const TObjectPtr<APlayerController>& Each)
		{
			return !IsValid(Each) || Each.Get() == Exiting;
		});

	ACA3DPlayerState* LeavingState = Exiting ? Exiting->GetPlayerState<ACA3DPlayerState>() : nullptr;
	if (!LeavingState)
	{
		// PlayerState 가 없는 컨트롤러는 애초에 참가 등록도 되지 않았다(RegisterParticipant 경고).
		// 순위를 줄 대상이 없으므로 보류 목록 정리까지만 하고 끝낸다.
		UE_LOG(LogCA3D, Log,
			TEXT("ACA3DGameMode: ACA3DPlayerState 없는 컨트롤러 %s 이탈 — 순위 부여 없음 (보류 목록 %d건 정리)"),
			*GetNameSafe(Exiting), RemovedPending);
		return;
	}

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();

	// ── ② 매치가 이미 끝난 뒤의 이탈은 **탈주가 아니다** ──
	// 그 사람은 매치를 끝까지 뛰었고 결과 화면을 닫았을 뿐이다. 여기서 표시를 세우면
	// 결과 화면이 떠 있는 동안(그리고 레벨 전환에서 컨트롤러가 일괄 파괴될 때) 완주자 전원이
	// 차례로 "탈주" 로 바뀐다 — bLeftMatch 가 뜻하기로 한 "이 매치를 끝까지 안 뛰었다" 와 정반대다.
	if (!CA3DGameState || CA3DGameState->bMatchEnded)
	{
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: %s 이탈 — 매치 종료 후라 탈주 표시·사망 처리 없음"),
			*LeavingState->GetPlayerName());
		return;
	}

	LeavingState->bLeftMatch = true;

	// ── ②-b 방장 승계 (Task 41) ──
	// bLeftMatch 를 세운 **뒤**에 해야 한다 — 승계 후보 탐색이 이탈자를 그 플래그로 거른다.
	// 나가는 사람의 방장 표시를 먼저 내려야 HasLobbyHost() 가 유령 방장을 보지 않는다.
	// ⚠️ 승계 직후 남은 전원이 이미 준비 상태면 시작 조건이 자동 충족될 수 있지만
	// **자동으로 시작하지 않는다** — 시작은 언제나 새 방장의 명시적 요청뿐이다
	// (자동 시작을 넣으면 "누가 나가는 순간 판이 시작되는" 통제 불가능한 시작 경로가 생긴다).
	if (LeavingState->bIsHost)
	{
		LeavingState->bIsHost = false;
		ReassignLobbyHost(LeavingState);
	}

	// ── ③ 아직 살아 있었다면 사망 처리 ──
	// 조건은 ResolvePendingDeaths 의 필터(bAlive && FinalRank == 0)와 **같은 식**이다 —
	// 이미 탈락한 사람에게 중복 통지를 보내지 않는다. (해소 쪽도 거르지만, 애초에 안 보내는
	// 편이 로그가 읽히고 "이탈이 등수를 두 번 만졌나" 를 의심할 여지가 없다.)
	const bool bWasAlive = LeavingState->bAlive && LeavingState->FinalRank == 0;
	if (bWasAlive)
	{
		APawn* LeavingPawn = Exiting->GetPawn();
		UStatusComponent* Status = LeavingPawn ? LeavingPawn->FindComponentByClass<UStatusComponent>() : nullptr;
		if (Status)
		{
			// **기존 사망 경로를 그대로 탄다.** 폰 숨김·컬리전 off·LifeState·사인 기록·
			// NotifyPlayerDeath 가 전부 이 한 함수 안에 이미 있다 — 여기서 다시 쓰면 두 벌이 되고,
			// 나중에 한쪽만 고쳐지는 순간 "이탈로 죽으면 시체가 안 사라진다" 같은 결함이 생긴다.
			Status->ServerKill(EDeathCause::Left);
		}
		else
		{
			// 폰이 없는 이탈(지형 준비 전 입장 → 대기 중 이탈, 또는 관전 전용 컨트롤러).
			// 태울 폰이 없으므로 통지만 직접 넣는다. **이 경로가 이 Task 의 핵심**이다 —
			// 빠뜨리면 AliveCount 가 안 줄어 남은 사람이 아무리 죽어도 매치가 끝나지 않는다.
			NotifyPlayerDeath(LeavingState);
		}
	}

	// ⚠️ 여기서 `LeavingState->bAlive = false` 를 **미리 세우지 않는다.**
	// ResolvePendingDeaths 는 `bAlive && FinalRank == 0` 인 항목만 해소하므로, 미리 내리면
	// 다음 틱에 그 항목이 통째로 걸러져 AliveCount 가 영영 줄지 않는다 — 이 Task 가 없애려던
	// 바로 그 증상이 다른 얼굴로 되살아난다. bAlive·FinalRank 를 내리는 곳은 해소 한 곳뿐이다.
	//
	// ⚠️ 그리고 `MatchParticipantCount` 를 **줄이지 않는다.**
	// 이 값은 "이번 매치에 몇 명이 들어왔는가" 이고, MinPlayersForMatchEnd 게이트의 입력이다.
	// 줄이면 그 게이트가 소급 적용돼 "3명이 시작했는데 1명 나가니 승패 판정이 꺼지는" 상태가
	// 된다 — 남은 두 명이 끝까지 싸워도 매치가 끝나지 않는다. 참가는 되돌릴 수 없는 사실이다.

	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DGameMode: 참가자 이탈 — %s(ColorIndex %d%s) %s, 생존 %d명(해소는 다음 틱), 참가 %d명 유지%s"),
		*LeavingState->GetPlayerName(), LeavingState->ColorIndex,
		LeavingState->IsABot() ? TEXT(", 봇") : TEXT(""),
		bWasAlive
			? (Exiting->GetPawn() ? TEXT("→ 사망 처리(폰 경로)") : TEXT("→ 사망 처리(폰 없음 — 통지만)"))
			: TEXT("→ 이미 탈락, 탈주 표시만"),
		CA3DGameState->AliveCount, MatchParticipantCount,
		RemovedPending > 0 ? TEXT(" · 스폰 대기 목록에서 제거") : TEXT(""));
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

// ─── 로비 페이즈 (Task 41, 서버 전용) ────────────────────────────────────────
//
// 2026-08-16 사용자 확정: "사람이 모이고 게임 시작 버튼이 눌린 다음에 캐릭터 선택창이
// 진행되어야 한다. 처음 들어온 사람이 방장이고 나머지는 게임 준비 버튼으로, 모두 준비가
// 끝나야 방장이 게임 시작 버튼을 누를 수 있도록."
//
// 구조는 캐릭터 선택 페이즈(Task 36)를 한 칸 앞에 복제한 것이다 — 페이즈 플래그는 GameState
// 복제 1필드, 갱신 주체는 이 클래스 단독, 상태 변경은 아래 Try~ 두 함수가 단일 경로,
// 스폰 게이트는 같은 PendingSpawnControllers 로 연장한다.

bool ACA3DGameMode::HasLobbyHost() const
{
	const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState)
	{
		return false;
	}

	for (APlayerState* Each : CA3DGameState->PlayerArray)
	{
		const ACA3DPlayerState* Player = Cast<ACA3DPlayerState>(Each);
		if (Player && Player->bIsHost && !Player->bLeftMatch)
		{
			return true;
		}
	}
	return false;
}

void ACA3DGameMode::ReassignLobbyHost(const ACA3DPlayerState* LeavingState)
{
	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState)
	{
		return;
	}

	// 남은 사람 중 ColorIndex 최소 = 가장 먼저 들어온 사람. PlayerArray 를 **고정 순서로** 훑되
	// 순서에 기대지 않고 값으로 고른다 — 배열 순서는 이탈·재등록으로 바뀔 수 있다.
	ACA3DPlayerState* Successor = nullptr;
	for (APlayerState* Each : CA3DGameState->PlayerArray)
	{
		ACA3DPlayerState* Player = Cast<ACA3DPlayerState>(Each);
		if (!Player || Player == LeavingState)
		{
			continue;
		}
		if (Player->IsABot() || Player->bLeftMatch)
		{
			continue; // 봇은 방장이 되지 않는다 · 이미 나간 사람도 후보가 아니다
		}
		if (!Successor || Player->ColorIndex < Successor->ColorIndex)
		{
			Successor = Player;
		}
	}

	if (!Successor)
	{
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 방장 이탈 — 남은 사람이 없어 방장 없음 (다음 입장자가 방장)"));
		return;
	}

	Successor->bIsHost = true;

	// 승계받은 사람의 준비 표시는 내린다 — 방장은 준비 대상이 아니라 그 값이 남아 있으면
	// "준비 완료인 방장" 이라는 표현 불가능한 상태가 화면에 남는다 (bReady 주석).
	Successor->bReady = false;

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 방장 승계 — %s (ColorIndex %d)"),
		*Successor->GetPlayerName(), Successor->ColorIndex);
}

bool ACA3DGameMode::CanStartFromLobby(int32 NonHostCount, int32 ReadyNonHostCount)
{
	// 방장 혼자면(비방장 0명) 조건이 정의상 충족이다 — 혼자 있는 사람이 판을 못 시작하면 안 된다.
	if (NonHostCount <= 0)
	{
		return true;
	}
	// >= 인 이유(== 이 아니라): 세는 쪽과 이 함수가 어긋나 준비 수가 더 크게 들어와도
	// "시작 못 함" 으로 굳지 않는다. 부족한 쪽만 막으면 된다.
	return ReadyNonHostCount >= NonHostCount;
}

void ACA3DGameMode::CountLobbyReadiness(const ACA3DGameState* GameState, int32& OutNonHostCount, int32& OutReadyNonHostCount)
{
	OutNonHostCount = 0;
	OutReadyNonHostCount = 0;
	if (!GameState)
	{
		return; // 클라 접속 직후 폴링 — 다음 프레임에 잡힌다 (UI 도 이 함수를 부른다)
	}

	// PlayerArray **고정 순서** 순회 — 별도 카운터를 들고 있지 않는 이유는 헤더 주석 참조.
	for (APlayerState* Each : GameState->PlayerArray)
	{
		const ACA3DPlayerState* Player = Cast<ACA3DPlayerState>(Each);
		if (!Player)
		{
			continue;
		}
		if (Player->IsABot())
		{
			continue; // 봇은 로비 인원이 아니다 — 매치가 시작돼야 투입된다 (SpawnFillBots)
		}
		if (Player->bLeftMatch)
		{
			continue; // 나간 사람 — PlayerState 는 결과 화면 때문에 남지만 그를 기다리면 로비가 안 끝난다
		}
		if (Player->bIsHost)
		{
			continue; // 방장은 준비 대상이 아니다 (PlayerState::bReady 주석)
		}

		++OutNonHostCount;
		if (Player->bReady)
		{
			++OutReadyNonHostCount;
		}
	}
}

bool ACA3DGameMode::TrySetReady(ACA3DPlayerState* PlayerState, bool bInReady)
{
	if (!HasAuthority()) return false; // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다

	if (!PlayerState)
	{
		return false;
	}

	const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || !CA3DGameState->bLobbyActive)
	{
		return false; // 로비 밖의 요청은 전면 거부 — 악성 클라 RPC 로도 복제 값이 안 바뀐다
	}

	if (PlayerState->bIsHost)
	{
		return false; // 방장은 준비 대상이 아니다 (시작 조건에서 제외되므로 값 자체가 무의미하다)
	}

	if (PlayerState->bReady == bInReady)
	{
		return false; // 변화 없음 — 복제 갱신과 로그를 만들지 않는다
	}

	PlayerState->bReady = bInReady;

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 로비 준비 %s — %s"),
		bInReady ? TEXT("완료") : TEXT("해제"), *PlayerState->GetPlayerName());
	return true;
}

bool ACA3DGameMode::TryStartMatchFromLobby(ACA3DPlayerState* Requester)
{
	if (!HasAuthority()) return false; // 불변식 5

	if (!Requester)
	{
		return false;
	}

	const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || !CA3DGameState->bLobbyActive)
	{
		return false; // 이미 시작됐거나 로비를 쓰지 않는 매치 — 중복 시작 방지
	}

	if (!Requester->bIsHost)
	{
		// 클라 입력은 신뢰하지 않는다 (TryAssignCharacter 의 범위 검증과 같은 원칙).
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 방장이 아닌 %s 의 시작 요청 거부"),
			*Requester->GetPlayerName());
		return false;
	}

	int32 NonHostCount = 0;
	int32 ReadyNonHostCount = 0;
	CountLobbyReadiness(CA3DGameState, NonHostCount, ReadyNonHostCount);
	if (!CanStartFromLobby(NonHostCount, ReadyNonHostCount))
	{
		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 시작 조건 미충족 — 준비 %d/%d명"),
			ReadyNonHostCount, NonHostCount);
		return false; // 상태 불변 — 되돌릴 것이 없다
	}

	UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 방장 %s 가 매치 시작 — 준비 %d/%d명"),
		*Requester->GetPlayerName(), ReadyNonHostCount, NonHostCount);

	EndLobby();
	return true;
}

void ACA3DGameMode::EndLobby()
{
	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || !CA3DGameState->bLobbyActive)
	{
		return; // 중복 호출 무시 (EndCharacterSelect·StopSuddenDeath 와 같은 방어)
	}

	// ① 로비 종료 — 이후 준비·시작 요청은 위 두 함수가 거부한다.
	CA3DGameState->bLobbyActive = false;

	// ② 캐릭터 선택 페이즈로 (또는 그 페이즈를 쓰지 않는 설정이면 곧바로 매치 시작 예약).
	//    분기는 StartCharacterSelect 안 **한 곳**뿐이다 — 여기에 사본을 두면 "로비를 켰을
	//    때만 봇이 안 들어오는" 식으로 두 경로가 조용히 갈라진다.
	StartCharacterSelect();

	// ③ 보류 스폰 해소를 한 번 두드린다.
	//    선택 페이즈를 **쓰는** 설정이면 FlushPendingSpawns 가 페이즈 플래그를 보고 그대로
	//    되돌아가고(해소는 EndCharacterSelect ⑤ 가 한다 — 해소 지점은 여전히 한 곳이다),
	//    **쓰지 않는** 설정이면 여기서 해소된다: 그 경우 StartPlay 의 해소는 로비 중이라
	//    이미 건너뛰어졌고 EndCharacterSelect 는 영영 불리지 않으므로, 두드리지 않으면
	//    로비를 기다린 사람들이 폰을 영영 못 받는다.
	FlushPendingSpawns();
}

// ─── 캐릭터 선택 페이즈 (Task 36, 서버 전용) ─────────────────────────────────

bool ACA3DGameMode::TryAssignCharacter(ACA3DPlayerState* PS, int32 Index)
{
	if (!HasAuthority()) return false; // 불변식 5 — GameMode 는 서버에만 존재하지만 명시한다

	if (!PS)
	{
		return false;
	}

	// 페이즈 없는 매치(Duration 0 또는 Characters 비었음)는 배정 자체가 없다 — 클라가 임의로
	// RPC 를 보내도 복제 값이 바뀌지 않아 기존 흐름이 그대로다 (무회귀. 헤더 주석 참조).
	if (!bCharacterSelectPhaseUsed)
	{
		return false;
	}

	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();
	if (!EffectiveRules->Characters.IsValidIndex(Index))
	{
		return false; // 인덱스 범위 — 클라 입력은 신뢰하지 않는다 (ServerSetCamYawIndex 와 같은 원칙)
	}

	const ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState)
	{
		return false;
	}

	// 페이즈 종료 후에는 **변경만** 거부한다 — 이미 값을 가진 사람의 요청(재선택)은 여기서
	// 걸리고, 미배정(INDEX_NONE)을 채우는 것은 늦은 입장자 자동 배정이라 허용된다.
	if (!CA3DGameState->bCharacterSelectActive && PS->CharacterIndex != INDEX_NONE)
	{
		return false;
	}

	// 선착순 점유 — 별도 점유 테이블 없이 PlayerArray 의 CharacterIndex 가 **단일 출처**다.
	// (테이블을 두면 이탈·파괴 때마다 두 곳을 맞춰야 하고, 어긋난 순간 유령 점유가 생긴다.)
	for (APlayerState* Each : CA3DGameState->PlayerArray)
	{
		const ACA3DPlayerState* Other = Cast<ACA3DPlayerState>(Each);
		if (Other && Other != PS && Other->CharacterIndex == Index)
		{
			return false; // 다른 참가자가 이미 점유 — 선착순
		}
	}

	// 재선택은 자기 값 덮어쓰기 — 이전 선택은 이 대입으로 자연 해제된다 (해제 코드 없음:
	// "해제 후 배정" 두 단계로 나누면 그 사이 상태가 존재하게 되고, 실패 시 되돌릴 것이 생긴다).
	PS->CharacterIndex = Index;
	return true;
}

void ACA3DGameMode::AutoAssignUnassignedCharacters()
{
	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();
	if (!CA3DGameState || EffectiveRules->Characters.Num() == 0)
	{
		return;
	}

	// PlayerArray **고정 순서** 순회 — 순서가 실행마다 다르면(TMap 순회·액터 이터레이션 등)
	// 같은 시드에서도 배정 결과가 달라져 고정 시드 재현이 깨진다 (불변식 4 와 같은 근거).
	for (APlayerState* Each : CA3DGameState->PlayerArray)
	{
		ACA3DPlayerState* Target = Cast<ACA3DPlayerState>(Each);
		if (!Target || Target->CharacterIndex != INDEX_NONE)
		{
			continue; // 이미 본인이 골랐다(또는 앞 순회에서 배정됐다) — 수동 선택을 덮지 않는다
		}

		// 남은 풀 수집 — 점유 조회는 TryAssignCharacter 와 같은 단일 출처(PlayerArray)를 훑는다.
		TArray<int32> Remaining;
		Remaining.Reserve(EffectiveRules->Characters.Num());
		for (int32 Index = 0; Index < EffectiveRules->Characters.Num(); ++Index)
		{
			bool bTaken = false;
			for (APlayerState* Other : CA3DGameState->PlayerArray)
			{
				const ACA3DPlayerState* OtherState = Cast<ACA3DPlayerState>(Other);
				if (OtherState && OtherState->CharacterIndex == Index)
				{
					bTaken = true;
					break;
				}
			}
			if (!bTaken)
			{
				Remaining.Add(Index);
			}
		}

		if (Remaining.Num() == 0)
		{
			// 참가자가 캐릭터 종수보다 많다 — 8종·최대 8인이라 정상 흐름에서는 없지만,
			// 정원 초과 접속(스폰 셀 재순환과 같은 상황)에서 크래시 없이 미배정으로 남긴다.
			UE_LOG(LogCA3D, Warning,
				TEXT("ACA3DGameMode: 남은 캐릭터 풀 없음 — %s 미배정 (Characters %d종 < 참가자 수)"),
				*Target->GetPlayerName(), EffectiveRules->Characters.Num());
			continue;
		}

		const int32 Pick = Remaining[CharacterAssignStream.RandRange(0, Remaining.Num() - 1)];
		if (TryAssignCharacter(Target, Pick)) // 자동 배정도 같은 단일 경로를 통과한다 (헤더 주석)
		{
			UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 캐릭터 자동 배정 — %s → %d%s"),
				*Target->GetPlayerName(), Pick, Target->IsABot() ? TEXT(" (봇)") : TEXT(""));
		}
		else
		{
			// 남은 풀에서 뽑았으니 정의상 실패할 수 없다 — 뜨면 점유 스캔과 단일 경로 검증이
			// 어긋난 것이다 (두 곳이 다른 출처를 보기 시작했다는 신호).
			UE_LOG(LogCA3D, Warning,
				TEXT("ACA3DGameMode: 자동 배정 거부됨 — %s → %d (점유 스캔과 TryAssignCharacter 검증 불일치)"),
				*Target->GetPlayerName(), Pick);
		}
	}
}

void ACA3DGameMode::StartCharacterSelect()
{
	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState)
	{
		return;
	}

	// Rules 는 BeginPlay 가 확보해 둔다(미지정이면 기본값 인스턴스). 그래도 CDO 폴백을 두는 것은
	// 이 함수가 BeginPlay 말고 EndLobby 에서도 불리기 때문이다 (다른 함수들과 같은 관례).
	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();

	// Duration > 0 && Characters 있음 → 페이즈를 돌리고, 봇 채우기·서든데스 예약은 페이즈
	// 종료(EndCharacterSelect)로 미룬다. 그 외에는 **아래 else 의 기존 경로 그대로** —
	// C++ 기본값(Duration 0)이 곧 기존 흐름이라, 룰셋을 안 만진 실행·기존 자동화 테스트는
	// 한 줄도 달라지지 않는다 (룰셋 CharacterSelectDuration 주석의 무회귀 근거).
	const bool bRunCharacterSelect =
		EffectiveRules->CharacterSelectDuration > 0.f && EffectiveRules->Characters.Num() > 0;
	if (EffectiveRules->CharacterSelectDuration > 0.f && EffectiveRules->Characters.Num() == 0)
	{
		// Duration 은 있는데 캐릭터 목록이 비었다 — 에셋 연결 누락. 페이즈를 스킵하고 진행한다
		// (매치당 1회 경고 — 이 함수는 매치당 한 번만 돈다).
		UE_LOG(LogCA3D, Warning,
			TEXT("ACA3DGameMode: CharacterSelectDuration %.1f초인데 Characters 가 비어 있음 — 선택 페이즈 스킵 (DA_Rules_Default 의 Characters 를 채울 것)"),
			EffectiveRules->CharacterSelectDuration);
	}

	if (bRunCharacterSelect)
	{
		bCharacterSelectPhaseUsed = true;

		// 페이즈 플래그·종료 시각 — 갱신 주체는 GameMode 단독 (bSuddenDeathActive 와 같은 관례).
		const float Now = CA3DGameState->GetServerWorldTimeSeconds();
		CA3DGameState->bCharacterSelectActive = true;
		CA3DGameState->CharacterSelectEndServerTime = Now + EffectiveRules->CharacterSelectDuration;

		// 매치 시작 시각은 일단 **예상 시작 시각**(지금 + 페이즈 길이) — 페이즈 동안 클라 HUD
		// 타이머가 음수 경과를 그리지 않게 한다. EndCharacterSelect ③ 이 실제 시각으로 재기록.
		CA3DGameState->MatchStartServerTime = Now + EffectiveRules->CharacterSelectDuration;

		GetWorldTimerManager().SetTimer(CharacterSelectTimer,
			FTimerDelegate::CreateUObject(this, &ACA3DGameMode::EndCharacterSelect),
			EffectiveRules->CharacterSelectDuration, false);

		UE_LOG(LogCA3D, Log, TEXT("ACA3DGameMode: 캐릭터 선택 페이즈 시작 — %d종, %.1f초"),
			EffectiveRules->Characters.Num(), EffectiveRules->CharacterSelectDuration);

		// ⚠️ 봇 채우기·서든데스 타이머는 여기서 예약하지 않는다 — 봇은 사람의 선택이 끝나
		// "남은 풀"이 확정된 뒤에 투입해야 같은 풀 규칙을 타고, SuddenDeathStart 는
		// "매치 시작 후 N초"라 시작이 미뤄지면 함께 미뤄져야 한다 (EndCharacterSelect ②·⑥).
	}
	else
	{
		// 매치 시작 시각을 **지금**으로 (재)기록한다. 로비를 쓰는 매치에서는 BeginPlay 가 넣어 둔
		// 값이 "로비를 열던 순간" 이라, 그대로 두면 HUD 경과 타이머가 로비 대기 시간까지 센다.
		// 로비를 쓰지 않는 매치에서는 BeginPlay 와 같은 프레임이라 값이 사실상 그대로다 (무회귀).
		CA3DGameState->MatchStartServerTime = CA3DGameState->GetServerWorldTimeSeconds();

		// ── 봇 채우기 예약 (Task 20 — 기존 경로 그대로) ──
		// 지금 세지 않고 미루는 이유 두 가지: (a) 사람이 들어와야 "부족분"이 정해진다,
		// (b) -ExecCmds 의 ca3d.BotFill 이 반영된 뒤에 판단해야 한다.
		// 지형 초기화가 끝난 뒤에 예약한다 — 그리드가 없으면 봇은 경로도 위험도 계산할 수 없다.
		GetWorldTimerManager().SetTimer(BotFillTimer,
			FTimerDelegate::CreateUObject(this, &ACA3DGameMode::SpawnFillBots),
			FMath::Max(EffectiveRules->BotFillDelaySeconds, KINDA_SMALL_NUMBER), false);

		// ── 서든데스 예약 (Task 24 — 기존 경로 그대로) ──
		// 매치 시작 시각 기준 SuddenDeathStart 초 뒤. 지형 초기화 이후에 예약하는 이유는 봇과
		// 같다 — 그리드가 없으면 낙하 지점을 고를 수 없다.
		GetWorldTimerManager().SetTimer(SuddenDeathTimer,
			FTimerDelegate::CreateUObject(this, &ACA3DGameMode::StartSuddenDeath),
			FMath::Max(EffectiveRules->SuddenDeathStart, KINDA_SMALL_NUMBER), false);
	}
}

void ACA3DGameMode::EndCharacterSelect()
{
	if (!HasAuthority()) return; // 불변식 5

	ACA3DGameState* CA3DGameState = GetGameState<ACA3DGameState>();
	if (!CA3DGameState || !CA3DGameState->bCharacterSelectActive)
	{
		return; // 이미 종료됐거나 페이즈가 없던 매치 — 중복 호출 무시 (StopSuddenDeath 와 같은 방어)
	}

	GetWorldTimerManager().ClearTimer(CharacterSelectTimer);

	// ① 미선택자(사람) 자동 배정 — 아직 플래그가 서 있어 단일 경로 검증을 그대로 통과한다.
	AutoAssignUnassignedCharacters();

	// ② 봇 즉시 투입 + 남은 풀에서 배정 (①과 같은 함수 경유 — 봇 전용 배정 경로를 만들지
	//    않는다). 페이즈 매치는 BeginPlay 의 봇 타이머가 없으므로 채우기 시점이 여기 하나뿐이고,
	//    사람의 선택이 전부 끝난 뒤라 봇이 사람의 캐릭터를 선점하는 일이 구조적으로 불가능하다.
	SpawnFillBots();
	AutoAssignUnassignedCharacters();

	// ③ 매치 시작 시각을 **실제 시각**으로 재기록 — BeginPlay 는 예상 시각(당시 + Duration)을
	//    넣어 두었다. 서든데스·HUD 타이머의 "매치 시작 후 N초" 의미가 여기서 확정된다.
	CA3DGameState->MatchStartServerTime = CA3DGameState->GetServerWorldTimeSeconds();

	// ④ 페이즈 종료 — 이후 사람의 선택 요청은 TryAssignCharacter 가 거부한다.
	CA3DGameState->bCharacterSelectActive = false;

	// ⑤ 보류된 폰 스폰 해소 — 스폰 게이트의 페이즈 연장이 여기서 풀린다 (④ 이후여야 한다:
	//    FlushPendingSpawns 는 페이즈 플래그를 보고 되돌아간다).
	FlushPendingSpawns();

	// ⑥ 서든데스 예약 — SuddenDeathStart 는 "매치 시작 후 N초"이므로 기준이 지금이다.
	const UCA3DRuleSet* EffectiveRules = Rules ? Rules : GetDefault<UCA3DRuleSet>();
	GetWorldTimerManager().SetTimer(SuddenDeathTimer,
		FTimerDelegate::CreateUObject(this, &ACA3DGameMode::StartSuddenDeath),
		FMath::Max(EffectiveRules->SuddenDeathStart, KINDA_SMALL_NUMBER), false);

	UE_LOG(LogCA3D, Log,
		TEXT("ACA3DGameMode: 캐릭터 선택 페이즈 종료 — 참가 %d명 배정 완료, 매치 시작"),
		MatchParticipantCount);
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
