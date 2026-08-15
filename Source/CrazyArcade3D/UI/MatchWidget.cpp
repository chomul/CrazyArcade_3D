#include "UI/MatchWidget.h"

#include "CrazyArcade3D.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "GameFramework/Pawn.h"
// UI→Gameplay 는 표시 항목(GDD 5장 ① 내 아이템 상태)의 출처가 여기뿐이라 불가피하다.
// **읽기만 한다** — 값 변경은 전부 UStatusComponent 의 Server* 진입점 소관 (불변식 5).
#include "Gameplay/Character/StatusComponent.h"

bool FMatchStatSnapshot::operator==(const FMatchStatSnapshot& Other) const
{
	// 배율은 부동소수 — 표시 단위(소수 둘째 자리)보다 작은 차이는 같다고 본다.
	// 안 그러면 복제 오차 때문에 매 틱 문자열을 새로 만든다.
	return bValid == Other.bValid
		&& ActiveBombCount == Other.ActiveBombCount
		&& MaxBombCount == Other.MaxBombCount
		&& BombRange == Other.BombRange
		&& bHasNeedle == Other.bHasNeedle
		&& bHasKick == Other.bHasKick
		&& MaxBombCountCap == Other.MaxBombCountCap
		&& MaxBombRangeCap == Other.MaxBombRangeCap
		&& FMath::IsNearlyEqual(MoveSpeedMul, Other.MoveSpeedMul, 0.005f)
		&& FMath::IsNearlyEqual(MoveSpeedMulCap, Other.MoveSpeedMulCap, 0.005f);
}

// ─── 순수 함수 (표시 가공) ───────────────────────────────────────────────────

FText UMatchWidget::FormatElapsedTime(float ElapsedSeconds)
{
	// 서버 시각 복제가 늦으면 음수가 나올 수 있다 — 0 으로 눌러 "-1:59" 같은 표시를 막는다.
	const int32 Total = FMath::Max(0, FMath::FloorToInt(ElapsedSeconds));
	const int32 Hours = Total / 3600;
	const int32 Minutes = (Total % 3600) / 60;
	const int32 Seconds = Total % 60;

	// 한 시간을 넘기면 분이 60 을 넘어 읽기 어렵다 — 그때만 시를 앞에 붙인다.
	return FText::FromString(Hours > 0
		? FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Seconds)
		: FString::Printf(TEXT("%d:%02d"), Minutes, Seconds));
}

FText UMatchWidget::FormatAliveCount(int32 AliveCount)
{
	return FText::FromString(FString::Printf(TEXT("생존 %d"), FMath::Max(0, AliveCount)));
}

FText UMatchWidget::FormatBombCount(const FMatchStatSnapshot& Stats)
{
	if (!Stats.bValid)
	{
		return FText::FromString(TEXT("폭탄 -"));
	}

	// 현재 설치 중 / 최대. ActiveBombCount 는 소유자에게만 복제된다 (Task 39 — 예전엔
	// 비복제라 원격 클라에서 항상 0 이었다). HUD 는 내 폰의 값만 보므로 이제 어디서든 맞는다.
	const bool bAtCap = Stats.MaxBombCountCap > 0 && Stats.MaxBombCount >= Stats.MaxBombCountCap;
	return FText::FromString(FString::Printf(TEXT("폭탄 %d/%d%s"),
		Stats.ActiveBombCount, Stats.MaxBombCount, bAtCap ? TEXT(" (MAX)") : TEXT("")));
}

FText UMatchWidget::FormatBombRange(const FMatchStatSnapshot& Stats)
{
	if (!Stats.bValid)
	{
		return FText::FromString(TEXT("범위 -"));
	}

	const bool bAtCap = Stats.MaxBombRangeCap > 0 && Stats.BombRange >= Stats.MaxBombRangeCap;
	return FText::FromString(FString::Printf(TEXT("범위 %d%s"),
		Stats.BombRange, bAtCap ? TEXT(" (MAX)") : TEXT("")));
}

FText UMatchWidget::FormatMoveSpeed(const FMatchStatSnapshot& Stats)
{
	if (!Stats.bValid)
	{
		return FText::FromString(TEXT("속도 -"));
	}

	const bool bAtCap = Stats.MoveSpeedMulCap > 0.f && Stats.MoveSpeedMul >= Stats.MoveSpeedMulCap - KINDA_SMALL_NUMBER;
	return FText::FromString(FString::Printf(TEXT("속도 x%.2f%s"),
		Stats.MoveSpeedMul, bAtCap ? TEXT(" (MAX)") : TEXT("")));
}

FText UMatchWidget::FormatNeedle(const FMatchStatSnapshot& Stats)
{
	return FText::FromString(Stats.bValid && Stats.bHasNeedle ? TEXT("니들 O") : TEXT("니들 X"));
}

FText UMatchWidget::FormatKick(const FMatchStatSnapshot& Stats)
{
	return FText::FromString(Stats.bValid && Stats.bHasKick ? TEXT("킥 O") : TEXT("킥 X"));
}

FString UMatchWidget::FormatStatLine(const FMatchStatSnapshot& Stats)
{
	return FString::Printf(TEXT("%s   %s   %s   %s   %s"),
		*FormatBombCount(Stats).ToString(),
		*FormatBombRange(Stats).ToString(),
		*FormatMoveSpeed(Stats).ToString(),
		*FormatNeedle(Stats).ToString(),
		*FormatKick(Stats).ToString());
}

TArray<FMatchResultRow> UMatchWidget::BuildResultRows(TArray<FMatchResultRow> RawRows, bool bMatchEnded)
{
	// 종료 전에는 결과가 없다 — 순위가 새어 나가면 "누가 곧 진다"가 미리 보인다.
	if (!bMatchEnded)
	{
		return TArray<FMatchResultRow>();
	}

	// 등수 오름차순. Rank <= 0 은 아직 순위가 안 매겨진 값(무승부 판정 중 등)이라 뒤로 민다.
	// StableSort 라 같은 등수의 표시 순서는 입력 순서(= 접속 순서)를 유지한다 — 결정론.
	RawRows.StableSort([](const FMatchResultRow& A, const FMatchResultRow& B)
	{
		const int32 KeyA = A.Rank > 0 ? A.Rank : MAX_int32;
		const int32 KeyB = B.Rank > 0 ? B.Rank : MAX_int32;
		return KeyA < KeyB;
	});

	// 공동 등수 표시 — 같은 Rank 가 둘 이상이면 양쪽 다 묶어서 표시한다 (동시 사망 규약).
	for (int32 Index = 0; Index < RawRows.Num(); ++Index)
	{
		if (RawRows[Index].Rank <= 0)
		{
			continue;
		}
		const bool bPrevSame = Index > 0 && RawRows[Index - 1].Rank == RawRows[Index].Rank;
		const bool bNextSame = Index + 1 < RawRows.Num() && RawRows[Index + 1].Rank == RawRows[Index].Rank;
		RawRows[Index].bTied = bPrevSame || bNextSame;
	}

	return RawRows;
}

bool UMatchWidget::IsDrawResult(const TArray<FMatchResultRow>& Rows, bool bMatchEnded)
{
	// ACA3DGameState 규약: 종료됐는데 1등이 하나도 없으면 무승부. 별도 플래그를 만들지 않는다.
	if (!bMatchEnded)
	{
		return false;
	}
	for (const FMatchResultRow& Row : Rows)
	{
		if (Row.Rank == 1)
		{
			return false;
		}
	}
	return true;
}

bool UMatchWidget::IsResultDataComplete(const TArray<FMatchResultRow>& Rows, bool bMatchEnded)
{
	// bMatchEnded 와 FinalRank 는 다른 액터라 도착 순서 보장이 없다 — 랭크 0 이 섞인
	// 중간 상태를 화면에 내보내지 않는 게이트 (Task 40). 서버는 종료 시점에 전원의 랭크를
	// 이미 확정했으므로(우승 1 / 사망·탈주 ≥ 2 / 무승부 전원 ≥ 2), 전원 Rank > 0 = 완성본.
	// 빈 배열도 미완성이다 — PlayerState 가 하나도 안 온 클라가 빈 결과 창을 띄우면 안 된다.
	if (!bMatchEnded || Rows.Num() == 0)
	{
		return false;
	}
	for (const FMatchResultRow& Row : Rows)
	{
		if (Row.Rank <= 0)
		{
			return false;
		}
	}
	return true;
}

FText UMatchWidget::FormatResultRow(const FMatchResultRow& Row)
{
	// 본인 행 표식 — 첫 줄(FormatLocalHeadline)이 등수를 알려주고, 이 표식이 목록에서
	// 그 자리를 짚어 준다. 두 정보가 같은 화면에 있어야 "몇 등이고 누구 뒤인지"가 한눈에 보인다.
	const TCHAR* Marker = Row.bIsLocal ? TEXT("▶ ") : TEXT("   ");

	// 탈주 표시는 **이름 뒤**에 붙인다 (2026-08-10). 등수 자리는 건드리지 않는다 —
	// 나간 사람도 나간 그 자리에서 등수를 받았고, 그 등수가 결과의 사실이다.
	const TCHAR* LeftSuffix = Row.bLeft ? TEXT(" (탈주)") : TEXT("");

	if (Row.Rank <= 0)
	{
		return FText::FromString(FString::Printf(TEXT("%s-등   %s%s"), Marker, *Row.PlayerName, LeftSuffix));
	}
	return FText::FromString(FString::Printf(TEXT("%s%s%d등   %s%s"),
		Marker, Row.bTied ? TEXT("공동 ") : TEXT(""), Row.Rank, *Row.PlayerName, LeftSuffix));
}

FText UMatchWidget::FormatLocalHeadline(const TArray<FMatchResultRow>& Rows, bool bDraw)
{
	const FMatchResultRow* Local = Rows.FindByPredicate(
		[](const FMatchResultRow& Row) { return Row.bIsLocal; });

	// 무승부는 등수보다 먼저다 — 전원 공동 2등이라 "공동 2등"만 보여주면 이긴 줄 안다.
	if (bDraw)
	{
		return FText::FromString(TEXT("무승부"));
	}

	// 관전자·PlayerState 미도착 등 본인 행이 없을 때. 목록은 그대로 보여준다.
	if (!Local || Local->Rank <= 0)
	{
		return FText::FromString(TEXT("매치 종료"));
	}

	if (Local->Rank == 1)
	{
		return FText::FromString(TEXT("우승!"));
	}

	return FText::FromString(FString::Printf(TEXT("내 순위  %s%d등"),
		Local->bTied ? TEXT("공동 ") : TEXT(""), Local->Rank));
}

bool UMatchWidget::ShouldShowMatchHUD(bool bLobbyActive, bool bCharacterSelectActive)
{
	// 게임 안에서만 보인다 — 로비도 선택도 아닐 때가 곧 "게임 안"이다 (헤더 주석).
	return !bLobbyActive && !bCharacterSelectActive;
}

void UMatchWidget::ApplyPhaseVisibility(bool bLobbyActive, bool bCharacterSelectActive)
{
	const int32 VisibleFlag = ShouldShowMatchHUD(bLobbyActive, bCharacterSelectActive) ? 1 : 0;
	if (VisibleFlag == LastPhaseVisibleFlag)
	{
		return; // 대부분의 프레임 — 매 틱 SetVisibility 를 부르면 슬레이트 무효화가 낭비된다
	}
	LastPhaseVisibleFlag = VisibleFlag;

	// SelfHitTestInvisible — 루트는 클릭을 먹지 않고 자식(있다면)은 그대로 받는다.
	// Collapsed 는 레이아웃 공간까지 없애 계층과 무관하게 확실히 사라진다 (헤더 주석).
	SetVisibility(VisibleFlag != 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

// ─── 읽기 헬퍼 (출처 해석) ───────────────────────────────────────────────────

const UCA3DRuleSet* UMatchWidget::ResolveRules(const UWorld* World)
{
	// StatusComponent::ResolveRules 와 같은 관례 — GameState 의 복제 포인터, 없으면 CDO.
	// UI 는 표시에만 쓰므로 잠시 CDO 값이어도 무해하고, 복제가 도착하면 다음 틱에 반영된다.
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

const UStatusComponent* UMatchWidget::ResolveStatus(const APawn* Pawn)
{
	return Pawn ? Pawn->FindComponentByClass<UStatusComponent>() : nullptr;
}

FMatchStatSnapshot UMatchWidget::CaptureStats(const UStatusComponent* Status, const UCA3DRuleSet* Rules)
{
	FMatchStatSnapshot Snapshot;
	if (!Status)
	{
		return Snapshot; // bValid = false — 사망 후 폰 소멸·접속 직후
	}

	Snapshot.bValid = true;
	Snapshot.ActiveBombCount = Status->ActiveBombCount;
	Snapshot.MaxBombCount = Status->MaxBombCount;
	Snapshot.BombRange = Status->BombRange;
	Snapshot.MoveSpeedMul = Status->MoveSpeedMul;
	Snapshot.bHasNeedle = Status->bHasNeedle;
	Snapshot.bHasKick = Status->bHasKick;

	if (Rules)
	{
		Snapshot.MaxBombCountCap = Rules->MaxBombCountCap;
		Snapshot.MaxBombRangeCap = Rules->MaxBombRangeCap;
		Snapshot.MoveSpeedMulCap = Rules->MoveSpeedMulCap;
	}
	return Snapshot;
}

TArray<FMatchResultRow> UMatchWidget::CollectResultRows(const ACA3DGameState* GameState,
                                                        const APlayerState* LocalPlayerState)
{
	if (!GameState)
	{
		return TArray<FMatchResultRow>();
	}

	TArray<FMatchResultRow> RawRows;
	RawRows.Reserve(GameState->PlayerArray.Num());
	for (const APlayerState* Each : GameState->PlayerArray)
	{
		const ACA3DPlayerState* Player = Cast<const ACA3DPlayerState>(Each);
		if (!Player)
		{
			continue;
		}
		FMatchResultRow Row;
		Row.Rank = Player->FinalRank;
		Row.PlayerName = Player->GetPlayerName();
		// 중도 이탈 표시 — 나간 사람의 PlayerState 는 ACA3DPlayerState::OnDeactivated 오버라이드
		// 덕분에 PlayerArray 에 남아 있다. 그 오버라이드가 없으면 이 행 자체가 사라진다.
		Row.bLeft = Player->bLeftMatch;
		// 이름이 아니라 **포인터**로 대조한다 — 같은 이름이 둘일 수 있고(봇 기본 이름),
		// 그러면 엉뚱한 행에 "나" 표식이 붙는다.
		Row.bIsLocal = (LocalPlayerState != nullptr && Each == LocalPlayerState);
		RawRows.Add(MoveTemp(Row));
	}

	return BuildResultRows(MoveTemp(RawRows), GameState->bMatchEnded);
}

// ─── 위젯 수명·갱신 ──────────────────────────────────────────────────────────

void UMatchWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 바인딩을 선택으로 둔 대가 — 비어 있는 이름을 한 줄로 알려 준다.
	// (필수 바인딩이면 WBP 가 컴파일조차 안 돼 첫 제작이 막힌다. 진단 가치는 이걸로 유지.)
	TArray<FString> MissingNames;
	auto NoteIfMissing = [&MissingNames](const UWidget* Widget, const TCHAR* Name)
	{
		if (!Widget)
		{
			MissingNames.Add(Name);
		}
	};
	NoteIfMissing(AliveCountText, TEXT("AliveCountText"));
	NoteIfMissing(MatchTimeText, TEXT("MatchTimeText"));
	NoteIfMissing(ItemPanel, TEXT("ItemPanel"));
	NoteIfMissing(BombCountText, TEXT("BombCountText"));
	NoteIfMissing(BombRangeText, TEXT("BombRangeText"));
	NoteIfMissing(MoveSpeedText, TEXT("MoveSpeedText"));
	NoteIfMissing(NeedleText, TEXT("NeedleText"));
	NoteIfMissing(KickText, TEXT("KickText"));
	NoteIfMissing(SuddenDeathWarning, TEXT("SuddenDeathWarning"));
	NoteIfMissing(ResultPanel, TEXT("ResultPanel"));
	NoteIfMissing(ResultText, TEXT("ResultText"));

	if (MissingNames.Num() > 0)
	{
		UE_LOG(LogCA3D, Warning, TEXT("UMatchWidget: 미바인딩 위젯 %d개 — %s (WBP_Match 에 같은 이름으로 추가하면 연결된다)"),
			MissingNames.Num(), *FString::Join(MissingNames, TEXT(", ")));
	}

	// 평소 숨김 — 결과·경고는 조건이 성립할 때만 나타난다.
	if (ResultPanel)
	{
		ResultPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	UpdateSuddenDeathWarning(false);
}

void UMatchWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
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

	// ⓪ 페이즈 숨김 (2026-08-16 요청 ②) — 로비·캐릭터 선택 동안 매치 HUD 는 화면에서 사라진다.
	//    접는 것은 여기서도 하지만 **되돌리는 구동자는 ACA3DHUD::Tick** 이다 (접힌 위젯은
	//    Slate 가 Tick 을 부르지 않는다 — ApplyPhaseVisibility 헤더 주석).
	ApplyPhaseVisibility(GameState->bLobbyActive, GameState->bCharacterSelectActive);
	if (!ShouldShowMatchHUD(GameState->bLobbyActive, GameState->bCharacterSelectActive))
	{
		// 숨김 중에는 텍스트 갱신·결과 계산을 건너뛴다. **Last* 캐시는 건드리지 않는다** —
		// 다시 보이는 첫 틱에 스냅샷 비교가 전부 정상 복원한다 (헤더 LastPhaseVisibleFlag 주석).
		return;
	}

	// ① 경과 시간 — 매 프레임 바뀌는 유일한 값. 그래도 초가 넘어갈 때만 문자열을 만든다.
	const float Elapsed = GameState->GetServerWorldTimeSeconds() - GameState->MatchStartServerTime;
	const int32 WholeSeconds = FMath::Max(0, FMath::FloorToInt(Elapsed));
	if (WholeSeconds != LastElapsedWholeSeconds)
	{
		LastElapsedWholeSeconds = WholeSeconds;
		if (MatchTimeText)
		{
			MatchTimeText->SetText(FormatElapsedTime(Elapsed));
		}
	}

	// ② 생존자 수 — 값이 바뀔 때만.
	if (GameState->AliveCount != LastAliveCount)
	{
		LastAliveCount = GameState->AliveCount;
		if (AliveCountText)
		{
			AliveCountText->SetText(FormatAliveCount(LastAliveCount));
		}
	}

	// ③ 내 아이템 상태 — OnRep 을 UI 가 직접 잡으면 Framework→UI 역방향 의존이 생기므로
	//    폴링한다. 대신 스냅샷 비교로 **값이 바뀐 프레임에만** 문자열을 만든다.
	RefreshPawnCache();
	const UCA3DRuleSet* Rules = GameState->Rules ? GameState->Rules.Get() : GetDefault<UCA3DRuleSet>();
	const FMatchStatSnapshot Stats = CaptureStats(CachedStatus.Get(), Rules);
	if (Stats != LastStats)
	{
		LastStats = Stats;
		if (BombCountText) { BombCountText->SetText(FormatBombCount(Stats)); }
		if (BombRangeText) { BombRangeText->SetText(FormatBombRange(Stats)); }
		if (MoveSpeedText) { MoveSpeedText->SetText(FormatMoveSpeed(Stats)); }
		if (NeedleText)    { NeedleText->SetText(FormatNeedle(Stats)); }
		if (KickText)      { KickText->SetText(FormatKick(Stats)); }

		// 보여 줄 값이 없으면(사망 후 폰 소멸) 패널째 숨긴다 — 마지막 값이 남아 오해를 부르지 않게.
		if (ItemPanel)
		{
			ItemPanel->SetVisibility(Stats.bValid ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	// ④ 결과 화면 — 종료 후에도 **매 틱** 부른다 (한 번만 그리면 안 된다).
	//    bMatchEnded(GameState)와 FinalRank(PlayerState)는 다른 액터라 복제 순서 보장이
	//    없다 — ShowResult 내부의 IsResultDataComplete 게이트가 **랭크가 다 도착할 때까지
	//    패널 자체를 숨기고**, 다 도착한 틱에 완성본으로 첫 표시한다 (Task 40). 매 틱
	//    재호출이 곧 재시도라 별도 재시도 로직이 없고, 표시 후에는 본문 비교가 재작업을 걸러
	//    비용은 전이 프레임에만 든다 (LastResultBody 헤더 주석).
	if (GameState->bMatchEnded)
	{
		ShowResult();
	}

	// ⑤ 서든데스 경고 (Task 24 연결 완료) — 출처는 GameState 의 복제 플래그 하나다.
	//    서브시스템(서버 전용)을 클라에서 조회하지 않는 이유: 낙하 스케줄러는 서버에만 돌고
	//    클라에는 상태가 없다. GameMode 가 GameState 에 써 준 플래그가 클라가 아는 전부이며,
	//    UI 는 그걸 **읽기만** 한다 (UI→Framework 읽기 전용).
	UpdateSuddenDeathWarning(GameState->bSuddenDeathActive);
}

void UMatchWidget::ShowResult()
{
	const ACA3DGameState* GameState = CachedGameState.Get();
	if (!GameState)
	{
		if (const UWorld* World = GetWorld())
		{
			GameState = World->GetGameState<ACA3DGameState>();
		}
	}
	if (!GameState || !GameState->bMatchEnded)
	{
		return; // 종료 전 호출은 무시 — 결과 게이트는 BuildResultRows 와 여기 두 겹이다
	}

	// 본인 PlayerState — 결과 목록에서 "내 행"을 짚기 위한 기준.
	const TArray<FMatchResultRow> Rows = CollectResultRows(GameState, GetOwningPlayerState());

	// 결과 확정 게이트 (Task 40) — 랭크가 다 도착하기 전에는 **패널 자체를 표시하지 않는다**
	// (Collapsed 유지, "집계 중" 같은 중간 문구도 없다 — 사용자 요청이 "안 보이도록"이다).
	// NativeTick 이 매 틱 다시 부르므로 랭크가 다 도착한 틱에 자동으로 완성본이 표시된다.
	if (!IsResultDataComplete(Rows, GameState->bMatchEnded))
	{
		return;
	}

	// 게이트를 통과해 실제로 표시하는 경로에서만 세운다 (헤더의 bResultShown 주석).
	bResultShown = true;

	// 승패 판정은 **행 스캔이 아니라 GameState 의 복제된 우승자**에서 읽는다.
	// MatchWinner 는 bMatchEnded 와 같은 액터라 같은 번들로 원자 도착한다 — 종료를 아는
	// 프레임에 승패도 반드시 함께 안다 (2026-08-06 무승부 오표시 수정, GameState 헤더 주석).
	// 행(FinalRank)은 다른 액터라 늦을 수 있지만, 위 게이트가 완성 전 표시를 막으므로
	// "-등"(랭크 0) 행이 화면에 나가는 일은 없다 (Task 40).
	const bool bDraw = (GameState->MatchWinner == nullptr);

	// 첫 줄은 **내 성적**, 그 아래가 전체 순위 (2026-08-06 사용자 요청 — 목록에서 자기
	// 이름을 찾아야 등수를 아는 것이 불편하다).
	TArray<FString> Lines;
	Lines.Reserve(Rows.Num() + 1);
	Lines.Add(FormatLocalHeadline(Rows, bDraw).ToString());
	for (const FMatchResultRow& Row : Rows)
	{
		Lines.Add(FormatResultRow(Row).ToString());
	}

	// 매 틱 불리므로 본문이 같으면 여기서 끝 — SetText·로그는 실제로 달라진 프레임에만.
	// (늦게 도착한 랭크가 행을 채우면 본문이 바뀌며 이 게이트를 통과한다.)
	const FString Body = FString::Join(Lines, TEXT("\n"));
	if (Body == LastResultBody)
	{
		return;
	}
	LastResultBody = Body;

	if (ResultText)
	{
		ResultText->SetText(FText::FromString(Body));
	}
	if (ResultPanel)
	{
		ResultPanel->SetVisibility(ESlateVisibility::Visible);
	}

	UE_LOG(LogCA3D, Log, TEXT("UMatchWidget: 결과 화면 표시 — %d명, %s"),
		Rows.Num(), bDraw ? TEXT("무승부") : TEXT("우승자 있음"));
}

void UMatchWidget::UpdateSuddenDeathWarning(bool bActive)
{
	if (SuddenDeathWarning)
	{
		SuddenDeathWarning->SetVisibility(bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UMatchWidget::RefreshPawnCache()
{
	const APawn* CurrentPawn = GetOwningPlayerPawn();
	if (CurrentPawn == CachedPawn.Get())
	{
		return; // 대부분의 프레임 — 컴포넌트 탐색 없이 빠져나간다
	}

	// 폰 교체(사망·리스폰·관전 전환) — 이때만 컴포넌트 포인터를 다시 해석한다.
	CachedPawn = CurrentPawn;
	CachedStatus = ResolveStatus(CurrentPawn);
}
