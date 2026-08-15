#include "UI/LobbyWidget.h"

#include "CrazyArcade3D.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
// 시작 가능 판정·준비 인원 집계의 **단일 공식**은 서버(GameMode)에 있다 — UI 는 그 static
// 순수 함수를 호출만 한다 (헤더 클래스 주석). 읽기 전용이라 UI→Framework 방향 규칙 그대로다.
#include "Framework/CA3DGameMode.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
// UI→Gameplay 는 준비·시작 요청의 진입점이 여기뿐이라 불가피하다.
// **호출만 한다** — 검증·판정·시작은 전부 서버 소관이며, 입력 전달은 컨트롤러 소관이라는
// 원칙(HUD 는 RPC 를 부르지 않는다)도 그대로다: 위젯은 컨트롤러의 공개 진입점을 부를 뿐이다.
#include "Gameplay/Character/CA3DPlayerController.h"

#if !UE_BUILD_SHIPPING
// 폴백 진행 경로 — WBP_Lobby 없이(캔버스 폴백만으로) 로비를 검증하기 위한 개발용 콘솔 명령.
// ca3d.SelectCharacter 와 같은 관례이며, 여기서도 **전달만** 한다 (판정은 서버).
static FAutoConsoleCommandWithWorldAndArgs CCmdCA3DReady(
	TEXT("ca3d.Ready"),
	TEXT("로비 준비 상태 전환 (인자 없으면 토글, 0/1 로 직접 지정 가능). WBP_Lobby 없이 로비를 검증하는 개발용."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(World->GetFirstPlayerController());
			if (!Controller)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.Ready: 로컬 ACA3DPlayerController 가 없다"));
				return;
			}
			// 인자가 없으면 복제된 현재 값의 반대를 요청한다 (버튼 클릭과 같은 토글 의미).
			const ACA3DPlayerState* MyState = Cast<ACA3DPlayerState>(Controller->PlayerState);
			const bool bNext = Args.Num() > 0
				? (FCString::Atoi(*Args[0]) != 0)
				: !(MyState && MyState->bReady);
			Controller->ServerSetReady(bNext);
		}));

static FAutoConsoleCommandWithWorld CCmdCA3DStartMatch(
	TEXT("ca3d.StartMatch"),
	TEXT("로비에서 매치 시작 요청 (방장만 유효 — 판정은 서버). WBP_Lobby 없이 로비를 검증하는 개발용."),
	FConsoleCommandWithWorldDelegate::CreateLambda(
		[](UWorld* World)
		{
			if (!World)
			{
				return;
			}
			ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(World->GetFirstPlayerController());
			if (!Controller)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.StartMatch: 로컬 ACA3DPlayerController 가 없다"));
				return;
			}
			// 방장 여부·전원 준비 여부는 서버가 판정한다 — 여기서 미리 거르지 않는다
			// (거름 규칙이 두 벌이 되는 순간 "눌리는데 안 되는" 어긋남이 생긴다).
			Controller->ServerStartMatch();
		}));
#endif

// ─── 순수 함수 (표시 가공) ───────────────────────────────────────────────────

FString ULobbyWidget::FormatRosterLine(const FString& PlayerName, bool bIsHost, bool bReady, bool bIsLocal)
{
	// 앞 3칸은 본인 표식 자리 — 남의 행은 공백으로 들여쓴다 (FormatResultRow 와 같은 열 정렬).
	const TCHAR* Marker = bIsLocal ? TEXT("▶ ") : TEXT("   ");

	// 방장은 준비 대상이 아니다 — 준비/대기 표기 대신 방장 표기가 자리를 차지한다 (헤더 주석).
	const TCHAR* State = bIsHost
		? TEXT("★ 방장")
		: (bReady ? TEXT("[준비]") : TEXT("[대기]"));

	return FString::Printf(TEXT("%s%s  %s"), Marker, State, *PlayerName);
}

FText ULobbyWidget::FormatLobbyStatus(int32 NonHostCount, int32 ReadyNonHostCount, bool bIsHost)
{
	// ⚠️ 시작 가능 판정은 **서버와 같은 함수**를 통과한다 — 여기서 부등호를 다시 쓰면
	// "안내 문구는 시작 가능인데 서버는 거부"가 가능해진다 (헤더 클래스 주석).
	const bool bCanStart = ACA3DGameMode::CanStartFromLobby(NonHostCount, ReadyNonHostCount);

	if (bIsHost)
	{
		return FText::FromString(FString::Printf(TEXT("%d/%d 명 준비 완료 — %s"),
			ReadyNonHostCount, NonHostCount,
			bCanStart ? TEXT("시작할 수 있습니다") : TEXT("준비를 기다리는 중")));
	}

	// 비방장에게도 인원은 보여 준다 — "나만 누르면 되는지" 를 알 수 있어야 기다림이 납득된다.
	return FText::FromString(FString::Printf(TEXT("%d/%d 명 준비 완료 — 방장이 시작하기를 기다리는 중"),
		ReadyNonHostCount, NonHostCount));
}

FText ULobbyWidget::FormatReadyButtonLabel(bool bReady)
{
	// 버튼 라벨은 **누르면 일어날 일**이다 (현재 상태가 아니라).
	return FText::FromString(bReady ? TEXT("준비 해제") : TEXT("준비"));
}

FString ULobbyWidget::FormatLobbyHeadline(bool bIsHost)
{
	return bIsHost
		? FString(TEXT("── 로비 ── (전원 준비되면 ca3d.StartMatch 로 시작)"))
		: FString(TEXT("── 로비 ── (ca3d.Ready 로 준비 · 방장이 시작한다)"));
}

// ─── 읽기 헬퍼 (출처 해석) ───────────────────────────────────────────────────

TArray<FString> ULobbyWidget::CollectRosterLines(const ACA3DGameState* GameState,
                                                 const APlayerState* LocalPlayerState)
{
	TArray<FString> Lines;
	if (!GameState)
	{
		return Lines; // 접속 직후 폴링 — 다음 프레임에 잡힌다
	}

	Lines.Reserve(GameState->PlayerArray.Num());
	for (const APlayerState* Each : GameState->PlayerArray)
	{
		const ACA3DPlayerState* Player = Cast<const ACA3DPlayerState>(Each);
		if (!Player || Player->IsABot())
		{
			continue; // 봇은 로비 인원이 아니다 (헤더 주석)
		}
		Lines.Add(FormatRosterLine(Player->GetPlayerName(), Player->bIsHost, Player->bReady,
			/*bIsLocal*/ LocalPlayerState != nullptr && Each == LocalPlayerState));
	}
	return Lines;
}

TArray<FString> ULobbyWidget::BuildLobbyFallbackLines(const ACA3DGameState* GameState,
                                                      const APlayerState* LocalPlayerState)
{
	TArray<FString> Lines;
	if (!GameState)
	{
		return Lines;
	}

	const ACA3DPlayerState* Local = Cast<const ACA3DPlayerState>(LocalPlayerState);
	const bool bIsHost = Local && Local->bIsHost;

	// 인원 집계도 서버와 같은 함수 — 폴백·위젯·서버가 전부 한 공식을 본다.
	int32 NonHostCount = 0;
	int32 ReadyNonHostCount = 0;
	ACA3DGameMode::CountLobbyReadiness(GameState, NonHostCount, ReadyNonHostCount);

	Lines.Add(FormatLobbyHeadline(bIsHost));
	Lines.Add(FormatLobbyStatus(NonHostCount, ReadyNonHostCount, bIsHost).ToString());
	Lines.Append(CollectRosterLines(GameState, LocalPlayerState));
	return Lines;
}

// ─── 위젯 수명·갱신 ──────────────────────────────────────────────────────────

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 바인딩을 선택으로 둔 대가 — 비어 있는 이름을 한 줄로 알려 준다 (MatchWidget 관례).
	TArray<FString> MissingNames;
	auto NoteIfMissing = [&MissingNames](const UWidget* Widget, const TCHAR* Name)
	{
		if (!Widget)
		{
			MissingNames.Add(Name);
		}
	};
	NoteIfMissing(RosterText, TEXT("RosterText"));
	NoteIfMissing(StatusText, TEXT("StatusText"));
	NoteIfMissing(ReadyButton, TEXT("ReadyButton"));
	NoteIfMissing(ReadyButtonText, TEXT("ReadyButtonText"));
	NoteIfMissing(StartButton, TEXT("StartButton"));

	if (MissingNames.Num() > 0)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("ULobbyWidget: 미바인딩 위젯 %d개 — %s (WBP_Lobby 에 같은 이름으로 추가하면 연결된다)"),
			MissingNames.Num(), *FString::Join(MissingNames, TEXT(", ")));
	}

	if (ReadyButton)
	{
		ReadyButton->OnClicked.AddDynamic(this, &ULobbyWidget::OnReadyClicked);
	}
	if (StartButton)
	{
		StartButton->OnClicked.AddDynamic(this, &ULobbyWidget::OnStartClicked);
	}
}

void ULobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// GameState 캐시 — 접속 직후엔 아직 없다. 잡히면 다시 찾지 않는다 (매 틱 캐스팅 금지).
	if (!CachedGameState.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			CachedGameState = World->GetGameState<ACA3DGameState>();
		}
		if (!CachedGameState.IsValid())
		{
			return;
		}
	}
	const ACA3DGameState* GameState = CachedGameState.Get();

	// 내 PlayerState — 방장 여부·준비 상태·목록의 본인 표식이 전부 여기서 나온다.
	// 접속 직후엔 아직 없을 수 있다 (그 프레임은 "비방장·미준비"로 그린다 — 복제가 오면 바뀐다).
	const ACA3DPlayerState* MyState = Cast<ACA3DPlayerState>(GetOwningPlayerState());
	const bool bIsHost = MyState && MyState->bIsHost;
	const bool bReady = MyState && MyState->bReady;

	// 준비 인원 집계는 서버와 같은 함수 (헤더 클래스 주석).
	int32 NonHostCount = 0;
	int32 ReadyNonHostCount = 0;
	ACA3DGameMode::CountLobbyReadiness(GameState, NonHostCount, ReadyNonHostCount);

	// ① 참가자 목록 — 본문이 달라진 프레임에만 문자열을 만든다.
	const FString RosterBody = FString::Join(CollectRosterLines(GameState, MyState), TEXT("\n"));
	if (!bRosterApplied || RosterBody != LastRosterBody)
	{
		bRosterApplied = true;
		LastRosterBody = RosterBody;
		if (RosterText)
		{
			RosterText->SetText(FText::FromString(RosterBody));
		}
	}

	// ② 안내 문구·버튼 — 스냅샷(인원·준비 수·방장 여부·내 준비 상태)이 바뀐 프레임에만.
	const int32 HostFlag = bIsHost ? 1 : 0;
	const int32 ReadyFlag = bReady ? 1 : 0;
	if (NonHostCount != LastNonHostCount || ReadyNonHostCount != LastReadyNonHostCount
		|| HostFlag != LastHostFlag || ReadyFlag != LastReadyFlag)
	{
		LastNonHostCount = NonHostCount;
		LastReadyNonHostCount = ReadyNonHostCount;
		LastHostFlag = HostFlag;
		LastReadyFlag = ReadyFlag;
		RefreshControls(NonHostCount, ReadyNonHostCount, bIsHost, bReady);
	}
}

void ULobbyWidget::RefreshControls(int32 NonHostCount, int32 ReadyNonHostCount, bool bIsHost, bool bReady)
{
	if (StatusText)
	{
		StatusText->SetText(FormatLobbyStatus(NonHostCount, ReadyNonHostCount, bIsHost));
	}

	// 방장에게는 시작 버튼만, 비방장에게는 준비 버튼만 — 반대쪽은 접는다.
	// (둘 다 보이면 "내가 눌러도 되는 버튼"이 어느 쪽인지가 매 순간 헷갈린다.)
	const ESlateVisibility StartVisibility = bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const ESlateVisibility ReadyVisibility = bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;

	if (StartButton)
	{
		StartButton->SetVisibility(StartVisibility);
		// ⚠️ 활성 조건도 **서버와 같은 함수**다 (헤더 클래스 주석).
		StartButton->SetIsEnabled(ACA3DGameMode::CanStartFromLobby(NonHostCount, ReadyNonHostCount));
	}
	if (ReadyButton)
	{
		ReadyButton->SetVisibility(ReadyVisibility);
	}
	// 라벨은 버튼의 자식일 수도, 바깥에 있을 수도 있다 (WBP 계층은 사용자 자유) —
	// 어느 쪽이든 남지 않도록 버튼과 같은 가시성을 함께 준다.
	// 보일 때는 SelfHitTestInvisible(= 텍스트 위젯의 UMG 기본값): 버튼 안에 들어 있을 때
	// 라벨이 클릭을 먼저 집어가지 않는다.
	if (ReadyButtonText)
	{
		ReadyButtonText->SetVisibility(bIsHost ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
		ReadyButtonText->SetText(FormatReadyButtonLabel(bReady));
	}
}

void ULobbyWidget::OnReadyClicked()
{
	ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(GetOwningPlayer());
	if (!Controller)
	{
		return;
	}

	// 토글 — 복제된 현재 값의 반대를 **요청**한다. 반영은 서버가 하고, 결과는 다음 틱의
	// 스냅샷 비교가 잡는다 (여기서 로컬 값을 미리 바꾸지 않는다 — 상태 복제가 곧 응답이다).
	const ACA3DPlayerState* MyState = Cast<ACA3DPlayerState>(GetOwningPlayerState());
	Controller->ServerSetReady(!(MyState && MyState->bReady));
}

void ULobbyWidget::OnStartClicked()
{
	if (ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(GetOwningPlayer()))
	{
		// 방장 여부·전원 준비 여부의 최종 판정은 서버다 — 버튼 비활성은 안내일 뿐이다.
		Controller->ServerStartMatch();
	}
}
