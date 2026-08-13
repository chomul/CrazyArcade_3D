#include "UI/CharacterSelectWidget.h"

#include "CrazyArcade3D.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "HAL/IConsoleManager.h"
#include "UI/MatchWidget.h" // ResolveRules 재사용 — "GameState 복제 포인터, 없으면 CDO" 관례를 한 곳에 유지
// UI→Gameplay 는 선택 요청의 진입점(ServerSelectCharacter)이 여기뿐이라 불가피하다.
// **호출만 한다** — 검증·배정·거부는 전부 서버 GameMode 의 단일 경로(TryAssignCharacter) 소관이며,
// 입력 전달은 컨트롤러 소관이라는 원칙(HUD 는 RPC 를 부르지 않는다)도 그대로다: 위젯은
// 컨트롤러의 공개 진입점을 부를 뿐이다 (헤더 클래스 주석).
#include "Gameplay/Character/CA3DPlayerController.h"

namespace
{
	// 내 선택 하이라이트 — 이름 라벨 색만 바꾼다 (최소한의 표시. 본격 비주얼은 WBP 가 할 일).
	// 튜닝 값이 아니라 폴백 수준의 구조적 표시 상수라 룰셋에 두지 않는다 (FLinearColor::White 와 같은 급).
	const FLinearColor CharSelectMyPickColor(1.f, 0.85f, 0.2f);
	const FLinearColor CharSelectDefaultNameColor(FLinearColor::White);
}

#if !UE_BUILD_SHIPPING
// 폴백 선택 경로 — WBP_CharacterSelect 없이(캔버스 폴백만으로) 선택 페이즈를 검증하기 위한
// 개발용 콘솔 명령. 숫자 키 바인딩은 입력 소관(ACA3DPlayerController)이라 넣지 않기로 했고
// (Task 지시), 미선택자는 자동 배정으로 완주되므로 이 명령은 순수 개발 편의다.
// 검증·배정은 여기서도 서버 단일 경로다 — 이 명령은 컨트롤러의 공개 진입점에 전달만 한다.
static FAutoConsoleCommandWithWorldAndArgs CCmdCA3DSelectCharacter(
	TEXT("ca3d.SelectCharacter"),
	TEXT("캐릭터 선택 (인자: 1~8). WBP 없이 캔버스 폴백 상태에서 선택 페이즈를 검증하는 개발용."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 1)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.SelectCharacter <1~8> — 인자가 필요하다"));
				return;
			}
			ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(World->GetFirstPlayerController());
			if (!Controller)
			{
				UE_LOG(LogCA3D, Warning, TEXT("ca3d.SelectCharacter: 로컬 ACA3DPlayerController 가 없다"));
				return;
			}
			// 사람이 치는 값은 1부터 — 내부 인덱스는 0부터. 범위 검증은 서버(TryAssignCharacter)가 한다.
			Controller->ServerSelectCharacter(FCString::Atoi(*Args[0]) - 1);
		}));
#endif

// ─── 순수 함수 (표시 가공) ───────────────────────────────────────────────────

int32 UCharacterSelectWidget::CountdownWholeSeconds(float RemainingSeconds)
{
	// 올림 — 0.2초 남아도 "1초". 내림이면 마지막 1초 내내 0 이 떠 있다 (헤더 주석). 음수는 0.
	return FMath::Max(0, FMath::CeilToInt(RemainingSeconds));
}

FText UCharacterSelectWidget::FormatCountdown(float RemainingSeconds)
{
	return FText::FromString(FString::Printf(TEXT("%d초"), CountdownWholeSeconds(RemainingSeconds)));
}

uint32 UCharacterSelectWidget::BuildOccupiedMask(const TArray<int32>& CharacterIndices)
{
	uint32 Mask = 0;
	for (const int32 Index : CharacterIndices)
	{
		// INDEX_NONE(미선택)과 범위 밖은 무시 — 표시가 원본보다 엄격하면 안 된다 (헤더 주석).
		if (Index >= 0 && Index < MaxCharacterSlots)
		{
			Mask |= 1u << Index;
		}
	}
	return Mask;
}

FString UCharacterSelectWidget::FormatSelectHeadline(float RemainingSeconds)
{
	return FString::Printf(TEXT("── 캐릭터 선택 %d초 ── (ca3d.SelectCharacter 1~8 · 미선택 시 자동 배정)"),
		CountdownWholeSeconds(RemainingSeconds));
}

FString UCharacterSelectWidget::FormatOccupancyLine(const TArray<FText>& Names, uint32 OccupiedMask, int32 LocalIndex)
{
	// 룰셋 미지정(빈 목록)이면 페이즈 자체가 스킵되는 상태 — 줄을 만들지 않는다.
	if (Names.Num() == 0)
	{
		return FString();
	}

	TArray<FString> Parts;
	Parts.Reserve(Names.Num());
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		// 내 선택 표기가 점유 표기보다 우선한다 — 내 것도 점유 마스크에 들어 있다.
		const TCHAR* Suffix = TEXT("");
		if (Index == LocalIndex)
		{
			Suffix = TEXT("(내 선택)");
		}
		else if ((OccupiedMask & (1u << Index)) != 0)
		{
			Suffix = TEXT("(점유)");
		}
		Parts.Add(FString::Printf(TEXT("[%d]%s%s"), Index + 1, *Names[Index].ToString(), Suffix));
	}
	return FString::Join(Parts, TEXT("  "));
}

// ─── 읽기 헬퍼 (출처 해석) ───────────────────────────────────────────────────

TArray<int32> UCharacterSelectWidget::CollectCharacterIndices(const ACA3DGameState* GameState)
{
	TArray<int32> Indices;
	if (!GameState)
	{
		return Indices; // 접속 직후 폴링 — 다음 프레임에 잡힌다
	}

	// 봇 포함 전원 — 봇 자동 배정분도 점유다 (사람이 그 캐릭터를 못 고르는 사실이 화면에 보여야 한다).
	Indices.Reserve(GameState->PlayerArray.Num());
	for (const APlayerState* Each : GameState->PlayerArray)
	{
		if (const ACA3DPlayerState* Player = Cast<const ACA3DPlayerState>(Each))
		{
			Indices.Add(Player->CharacterIndex);
		}
	}
	return Indices;
}

TArray<FText> UCharacterSelectWidget::CollectCharacterNames(const UCA3DRuleSet* Rules)
{
	TArray<FText> Names;
	if (!Rules)
	{
		return Names;
	}

	Names.Reserve(Rules->Characters.Num());
	for (int32 Index = 0; Index < Rules->Characters.Num(); ++Index)
	{
		const FText& DisplayName = Rules->Characters[Index].DisplayName;
		// 에셋 연결 전에도 버튼이 무명으로 남지 않게 — 위젯·폴백이 이 함수를 함께 써 표기가 같다.
		Names.Add(DisplayName.IsEmpty()
			? FText::FromString(FString::Printf(TEXT("캐릭터 %d"), Index + 1))
			: DisplayName);
	}
	return Names;
}

// ─── 위젯 수명·갱신 ──────────────────────────────────────────────────────────

void UCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 바인딩 필드를 인덱스로 다루기 위한 캐시 (MaxCharacterSlots 개).
	Buttons = { CharButton_0, CharButton_1, CharButton_2, CharButton_3,
	            CharButton_4, CharButton_5, CharButton_6, CharButton_7 };
	NameLabels = { CharName_0, CharName_1, CharName_2, CharName_3,
	               CharName_4, CharName_5, CharName_6, CharName_7 };

	// 바인딩을 선택으로 둔 대가 — 비어 있는 이름을 한 줄로 알려 준다 (MatchWidget 관례).
	TArray<FString> MissingNames;
	for (int32 Index = 0; Index < MaxCharacterSlots; ++Index)
	{
		if (!Buttons[Index])
		{
			MissingNames.Add(FString::Printf(TEXT("CharButton_%d"), Index));
		}
		if (!NameLabels[Index])
		{
			MissingNames.Add(FString::Printf(TEXT("CharName_%d"), Index));
		}
	}
	if (!CountdownText)
	{
		MissingNames.Add(TEXT("CountdownText"));
	}
	if (MissingNames.Num() > 0)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("UCharacterSelectWidget: 미바인딩 위젯 %d개 — %s (WBP_CharacterSelect 에 같은 이름으로 추가하면 연결된다)"),
			MissingNames.Num(), *FString::Join(MissingNames, TEXT(", ")));
	}

	// 클릭 바인딩 — 무인자 델리게이트라 슬롯별 핸들러가 HandleSelectClicked(i) 로 모인다 (헤더 주석).
	if (CharButton_0) { CharButton_0->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton0Clicked); }
	if (CharButton_1) { CharButton_1->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton1Clicked); }
	if (CharButton_2) { CharButton_2->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton2Clicked); }
	if (CharButton_3) { CharButton_3->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton3Clicked); }
	if (CharButton_4) { CharButton_4->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton4Clicked); }
	if (CharButton_5) { CharButton_5->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton5Clicked); }
	if (CharButton_6) { CharButton_6->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton6Clicked); }
	if (CharButton_7) { CharButton_7->OnClicked.AddDynamic(this, &UCharacterSelectWidget::OnCharButton7Clicked); }
}

void UCharacterSelectWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
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

	// ① 카운트다운 — 매 프레임 바뀌는 유일한 값. 초가 넘어갈 때만 문자열을 만든다.
	//    남은 시간은 복제된 종료 "시각"에서 유도한다 (매 초 갱신 복제가 필요 없다 — GameState 주석).
	const float Remaining = GameState->CharacterSelectEndServerTime - GameState->GetServerWorldTimeSeconds();
	const int32 WholeSeconds = CountdownWholeSeconds(Remaining);
	if (WholeSeconds != LastCountdownWholeSeconds)
	{
		LastCountdownWholeSeconds = WholeSeconds;
		if (CountdownText)
		{
			CountdownText->SetText(FormatCountdown(Remaining));
		}
	}

	// ② 점유 현황·내 선택·이름 — 스냅샷(마스크 + 내 인덱스 + 룰셋)이 바뀐 프레임에만 반영한다.
	//    점유는 별도 복제 상태가 아니라 PlayerArray 의 CharacterIndex 순회로 유도한다 (Task 36).
	const UCA3DRuleSet* Rules = UMatchWidget::ResolveRules(GetWorld());
	const uint32 OccupiedMask = BuildOccupiedMask(CollectCharacterIndices(GameState));
	const ACA3DPlayerState* MyState = Cast<ACA3DPlayerState>(GetOwningPlayerState());
	const int32 MyIndex = MyState ? MyState->CharacterIndex : static_cast<int32>(INDEX_NONE);
	const int32 CharacterCount = Rules ? Rules->Characters.Num() : 0;

	if (OccupiedMask != LastOccupiedMask || MyIndex != LastMyIndex
		|| CharacterCount != LastCharacterCount || Rules != LastRules.Get())
	{
		LastOccupiedMask = OccupiedMask;
		LastMyIndex = MyIndex;
		LastCharacterCount = CharacterCount;
		LastRules = Rules;
		RefreshSlots(CollectCharacterNames(Rules), OccupiedMask, MyIndex);
	}
}

void UCharacterSelectWidget::RefreshSlots(const TArray<FText>& Names, uint32 OccupiedMask, int32 MyIndex)
{
	for (int32 Index = 0; Index < MaxCharacterSlots; ++Index)
	{
		// Characters.Num() 보다 많은 슬롯은 접는다 — 8종 미만 구성에서도 빈 버튼이 남지 않게.
		const bool bSlotUsed = Index < Names.Num();
		const ESlateVisibility SlotVisibility = bSlotUsed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
		if (Buttons[Index])
		{
			Buttons[Index]->SetVisibility(SlotVisibility);
		}
		if (NameLabels[Index])
		{
			NameLabels[Index]->SetVisibility(SlotVisibility);
		}
		if (!bSlotUsed)
		{
			continue;
		}

		// 점유된 버튼은 비활성 — **내가 점유한 것도 비활성**이다 (재클릭할 이유가 없고,
		// 내 선택은 아래 색으로 따로 보인다). 남이 잡은 것과 구분은 색이 한다.
		if (Buttons[Index])
		{
			Buttons[Index]->SetIsEnabled((OccupiedMask & (1u << Index)) == 0);
		}
		if (NameLabels[Index])
		{
			NameLabels[Index]->SetText(Names[Index]);
			NameLabels[Index]->SetColorAndOpacity(FSlateColor(
				Index == MyIndex ? CharSelectMyPickColor : CharSelectDefaultNameColor));
		}
	}
}

void UCharacterSelectWidget::HandleSelectClicked(int32 Index)
{
	// 컨트롤러의 공개 진입점에 전달만 — 검증·배정·거부는 서버 단일 경로(TryAssignCharacter).
	// 응답 RPC 는 없다: 성공이면 복제된 CharacterIndex 가 바뀌어 다음 틱 스냅샷 비교가 잡는다.
	if (ACA3DPlayerController* Controller = Cast<ACA3DPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerSelectCharacter(Index);
	}
}

void UCharacterSelectWidget::OnCharButton0Clicked() { HandleSelectClicked(0); }
void UCharacterSelectWidget::OnCharButton1Clicked() { HandleSelectClicked(1); }
void UCharacterSelectWidget::OnCharButton2Clicked() { HandleSelectClicked(2); }
void UCharacterSelectWidget::OnCharButton3Clicked() { HandleSelectClicked(3); }
void UCharacterSelectWidget::OnCharButton4Clicked() { HandleSelectClicked(4); }
void UCharacterSelectWidget::OnCharButton5Clicked() { HandleSelectClicked(5); }
void UCharacterSelectWidget::OnCharButton6Clicked() { HandleSelectClicked(6); }
void UCharacterSelectWidget::OnCharButton7Clicked() { HandleSelectClicked(7); }
