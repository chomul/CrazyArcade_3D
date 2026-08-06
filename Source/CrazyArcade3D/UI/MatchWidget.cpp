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

	// 현재 설치 중 / 최대. ActiveBombCount 는 서버 전용 값이라 클라에서는 항상 0 이다
	// (복제하지 않는다 — StatusComponent 주석). 그래도 리슨 호스트·봇 화면에서는 맞는다.
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

FText UMatchWidget::FormatResultRow(const FMatchResultRow& Row)
{
	if (Row.Rank <= 0)
	{
		return FText::FromString(FString::Printf(TEXT("-등   %s"), *Row.PlayerName));
	}
	return FText::FromString(FString::Printf(TEXT("%s%d등   %s"),
		Row.bTied ? TEXT("공동 ") : TEXT(""), Row.Rank, *Row.PlayerName));
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

TArray<FMatchResultRow> UMatchWidget::CollectResultRows(const ACA3DGameState* GameState)
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
	//    없다 — 첫 프레임에 굳히면 우승자 랭크가 늦게 도착한 클라가 우승 매치를 무승부로
	//    그린 채 멈춘다 (LastResultBody 헤더 주석, 2026-08-06 실측). ShowResult 내부가
	//    본문 비교로 재작업을 걸러 비용은 전이 프레임에만 든다.
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

	bResultShown = true;

	const TArray<FMatchResultRow> Rows = CollectResultRows(GameState);

	TArray<FString> Lines;
	Lines.Reserve(Rows.Num() + 1);
	Lines.Add(IsDrawResult(Rows, true) ? TEXT("무승부") : TEXT("매치 종료"));
	for (const FMatchResultRow& Row : Rows)
	{
		Lines.Add(FormatResultRow(Row).ToString());
	}

	// 매 틱 불리므로 본문이 같으면 여기서 끝 — SetText·로그는 실제로 달라진 프레임에만.
	// (우승자 랭크가 늦게 도착하면 본문이 "무승부 → N등 우승"으로 바뀌며 이 게이트를 통과한다.)
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
		Rows.Num(), IsDrawResult(Rows, true) ? TEXT("무승부") : TEXT("우승자 있음"));
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
