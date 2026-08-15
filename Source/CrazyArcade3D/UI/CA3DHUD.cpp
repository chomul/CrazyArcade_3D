#include "UI/CA3DHUD.h"

#include "CrazyArcade3D.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/CA3DGameState.h"
#include "Framework/CA3DPlayerState.h"
#include "Framework/CA3DRuleSet.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UI/CharacterSelectWidget.h"
#include "UI/MatchWidget.h"

#if !UE_BUILD_SHIPPING
// 캔버스 텍스트 폴백 스위치.
// 왜 필요한가: WBP_Match 를 손으로 만들기 전에는 화면에 아무 숫자도 안 보인다.
// 폭탄 개수·범위가 안 보이면 아이템 효과를 확인할 방법이 없어 게임 제작이 막힌다.
// 그래서 위젯이 없으면 자동으로 텍스트를 그린다 — 에디터 작업 0 으로도 값이 보인다.
static TAutoConsoleVariable<int32> CVarCA3DDebugHUD(
	TEXT("ca3d.DebugHUD"),
	-1,
	TEXT("캔버스 텍스트 HUD. -1 = 자동(위젯이 없을 때만, 기본), 0 = 끄기, 1 = 강제 표시"));
#endif

void ACA3DHUD::BeginPlay()
{
	Super::BeginPlay();

	// 방어적 가드 (GDD 7.4) — 데디에는 HUD 가 애초에 생성되지 않지만 명시한다.
	// HUD 는 컬리전·판정·복제를 하나도 갖지 않는 순수 시각이라 꺼도 서버 동작에 영향이 없다.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	if (!MatchWidgetClass)
	{
		// 에러가 아니다 — 폴백이 대신 그린다. WBP 제작 전 정상 경로.
		UE_LOG(LogCA3D, Log,
			TEXT("ACA3DHUD: MatchWidgetClass 미지정 — 캔버스 텍스트 폴백으로 표시 (ca3d.DebugHUD 로 제어)"));
		return;
	}

	MatchWidget = CreateWidget<UMatchWidget>(GetOwningPlayerController(), MatchWidgetClass);
	if (!MatchWidget)
	{
		UE_LOG(LogCA3D, Warning, TEXT("ACA3DHUD: 매치 위젯 생성 실패 — %s"), *MatchWidgetClass->GetName());
		return;
	}

	MatchWidget->AddToViewport();
	UE_LOG(LogCA3D, Log, TEXT("ACA3DHUD: 매치 위젯 표시 — %s"), *MatchWidgetClass->GetName());
}

void ACA3DHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 방어적 가드 (BeginPlay 와 같은 근거) — 데디에는 HUD 가 애초에 생성되지 않지만 명시한다.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	UpdateCharacterSelectLifetime();
}

void ACA3DHUD::UpdateCharacterSelectLifetime()
{
	// 페이즈 전이는 GameState 복제 플래그로만 안다 — 전용 이벤트가 없으므로 폴링한다
	// (ACA3DPlayerController 의 커서 전이와 같은 관례). "활성 ↔ 위젯 존재"를 상태 기반으로
	// 맞추므로 에지 플래그가 필요 없고, 페이즈 도중 들어온 늦은 접속자도 첫 틱에 위젯이 뜬다.
	const UWorld* World = GetWorld();
	const ACA3DGameState* GameState = World ? World->GetGameState<ACA3DGameState>() : nullptr;
	const bool bSelectActive = GameState && GameState->bCharacterSelectActive;

	if (bSelectActive && !CharacterSelectWidget && CharacterSelectWidgetClass)
	{
		CharacterSelectWidget = CreateWidget<UCharacterSelectWidget>(GetOwningPlayerController(), CharacterSelectWidgetClass);
		if (!CharacterSelectWidget)
		{
			UE_LOG(LogCA3D, Warning, TEXT("ACA3DHUD: 캐릭터 선택 위젯 생성 실패 — %s"), *CharacterSelectWidgetClass->GetName());
			return;
		}
		CharacterSelectWidget->AddToViewport();
		UE_LOG(LogCA3D, Log, TEXT("ACA3DHUD: 캐릭터 선택 위젯 표시 — %s"), *CharacterSelectWidgetClass->GetName());
	}
	else if (!bSelectActive && CharacterSelectWidget)
	{
		// 페이즈 종료 전이 — 매치 화면으로 넘어가므로 위젯째 내린다 (MatchWidget 수명 관리 패턴).
		CharacterSelectWidget->RemoveFromParent();
		CharacterSelectWidget = nullptr;
		UE_LOG(LogCA3D, Log, TEXT("ACA3DHUD: 캐릭터 선택 위젯 종료 (페이즈 종료 전이)"));
	}
}

void ACA3DHUD::ShowResult()
{
	if (MatchWidget)
	{
		MatchWidget->ShowResult();
	}
}

bool ACA3DHUD::ShouldDrawDebugFallback() const
{
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarCA3DDebugHUD.GetValueOnGameThread();
	if (Mode == 0)
	{
		return false;
	}
	if (Mode > 0)
	{
		return true;
	}
	return MatchWidget == nullptr; // 자동 — 위젯이 화면을 담당하면 겹쳐 그리지 않는다
#else
	return false;
#endif
}

bool ACA3DHUD::ShouldDrawSelectFallback() const
{
#if !UE_BUILD_SHIPPING
	const int32 Mode = CVarCA3DDebugHUD.GetValueOnGameThread();
	if (Mode == 0)
	{
		return false;
	}
	if (Mode > 0)
	{
		return true;
	}
	// 자동 — 선택 페이즈의 담당 위젯은 CharacterSelectWidget 이므로 그것의 유무를 본다
	// (WBP_Match 만 있고 WBP_CharacterSelect 가 없는 조합에서도 선택 정보가 보여야 한다).
	return CharacterSelectWidget == nullptr;
#else
	return false;
#endif
}

void ACA3DHUD::DrawHUD()
{
	Super::DrawHUD();

#if !UE_BUILD_SHIPPING
	const bool bMatchFallback = ShouldDrawDebugFallback();
	const bool bSelectFallback = ShouldDrawSelectFallback();
	if (!Canvas || (!bMatchFallback && !bSelectFallback))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const ACA3DGameState* GameState = World ? World->GetGameState<ACA3DGameState>() : nullptr;
	if (!GameState)
	{
		return; // 접속 직후 — 다음 프레임에 잡힌다
	}

	// 표시 가공은 전부 위젯의 static 순수 함수를 통과한다 —
	// 폴백과 위젯이 다른 공식을 쓰면 "디버그 표시와 실제 HUD 가 다른" 최악의 상황이 된다.
	const UCA3DRuleSet* Rules = UMatchWidget::ResolveRules(World);

	TArray<FString> Lines;

	// 선택 페이즈 정보 — WBP_CharacterSelect 없이도 페이즈가 보이고(카운트다운·점유),
	// 개발용 콘솔 명령(ca3d.SelectCharacter)으로 실제 선택까지 검증할 수 있다.
	if (bSelectFallback && GameState->bCharacterSelectActive)
	{
		const float Remaining = GameState->CharacterSelectEndServerTime - GameState->GetServerWorldTimeSeconds();
		Lines.Add(UCharacterSelectWidget::FormatSelectHeadline(Remaining));

		const APlayerController* OwningController = GetOwningPlayerController();
		const ACA3DPlayerState* MyState = OwningController
			? Cast<ACA3DPlayerState>(OwningController->PlayerState) : nullptr;
		const FString Occupancy = UCharacterSelectWidget::FormatOccupancyLine(
			UCharacterSelectWidget::CollectCharacterNames(Rules),
			UCharacterSelectWidget::BuildOccupiedMask(UCharacterSelectWidget::CollectCharacterIndices(GameState)),
			MyState ? MyState->CharacterIndex : static_cast<int32>(INDEX_NONE));
		if (!Occupancy.IsEmpty())
		{
			Lines.Add(Occupancy);
		}
	}

	if (bMatchFallback)
	{
		const FMatchStatSnapshot Stats = UMatchWidget::CaptureStats(
			UMatchWidget::ResolveStatus(GetOwningPawn()), Rules);

		const float Elapsed = GameState->GetServerWorldTimeSeconds() - GameState->MatchStartServerTime;

		Lines.Add(FString::Printf(TEXT("%s   %s"),
			*UMatchWidget::FormatAliveCount(GameState->AliveCount).ToString(),
			*UMatchWidget::FormatElapsedTime(Elapsed).ToString()));
		Lines.Add(UMatchWidget::FormatStatLine(Stats));

		if (GameState->bMatchEnded)
		{
			// 결과 확정 게이트 (Task 40) — 위젯(ShowResult)과 **같은 함수**를 통과한다
			// (폴백과 위젯이 다른 공식을 쓰면 표시가 어긋난다). 랭크 0 이 섞인 중간 상태면
			// 결과 줄을 아예 추가하지 않는다 — "집계 중" 문구도 없다 ("안 보이도록"이 요구).
			const TArray<FMatchResultRow> Rows = UMatchWidget::CollectResultRows(GameState);
			if (UMatchWidget::IsResultDataComplete(Rows, GameState->bMatchEnded))
			{
				Lines.Add(UMatchWidget::IsDrawResult(Rows, true) ? TEXT("── 무승부 ──") : TEXT("── 매치 종료 ──"));
				for (const FMatchResultRow& Row : Rows)
				{
					Lines.Add(UMatchWidget::FormatResultRow(Row).ToString());
				}
			}
		}
	}

	// 좌상단 고정. 해상도별 배치는 WBP 가 할 일이고 여기는 값 확인용이다.
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	const float LineHeight = 18.f;
	float DrawY = 12.f;
	for (const FString& Line : Lines)
	{
		DrawText(Line, FLinearColor::White, 12.f, DrawY, Font, 1.f, false);
		DrawY += LineHeight;
	}
#endif
}
