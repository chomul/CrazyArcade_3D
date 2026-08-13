#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CharacterSelectWidget.generated.h"

class UButton;
class UTextBlock;
class ACA3DGameState;
class UCA3DRuleSet;

// 캐릭터 선택 위젯의 C++ 베이스 (Task 36 후속 UI). 레이아웃·비주얼은 전부 BP(WBP_CharacterSelect),
// 여기는 바인딩 필드와 갱신 로직만 — 카운트다운 · 8종 버튼 · 점유/내 선택 표시.
//
// 데이터 흐름은 MatchWidget 과 같은 **한 방향 폴링**이다: GameState / PlayerArray → 여기서 표시.
// OnRep 을 UI 가 직접 잡으면 Framework→UI 역방향 의존이 생기므로 폴링하고, 스냅샷 비교로
// 값이 바뀐 프레임에만 위젯을 만진다. 점유 현황은 별도 복제 상태가 아니라
// **PlayerArray 의 ACA3DPlayerState::CharacterIndex 를 순회해 유도한다** (출처 단일화 — Task 36).
//
// 버튼 클릭만은 예외적으로 "쓰기"처럼 보이지만, 위젯이 직접 상태를 바꾸는 것이 아니라
// **컨트롤러의 공개 진입점(ACA3DPlayerController::ServerSelectCharacter)을 부를 뿐**이다.
// 입력 전달은 컨트롤러 소관이고(HUD 는 RPC 를 부르지 않는다는 원칙 그대로), 검증·배정·거부는
// 전부 서버 GameMode 의 단일 경로(TryAssignCharacter)가 한다 — 응답 RPC 도 없다:
// 성공이면 복제된 CharacterIndex 가 바뀌고, 거부면 안 바뀐다. 상태 복제가 곧 응답이다.
//
// 표시 가공(남은 초·점유 마스크·점유 현황 문자열)은 전부 static 순수 함수로 분리했다 —
// 캔버스 폴백 HUD(ACA3DHUD::DrawHUD)와 위젯이 **같은 함수**를 통과해야 표시가 어긋나지 않고,
// 위젯 인스턴스 없이 테스트된다 (MatchWidget 과 같은 관례).
UCLASS(Abstract)
class CRAZYARCADE3D_API UCharacterSelectWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 버튼·라벨 슬롯 수 — 룰셋 수치가 아니라 **바인딩 필드 개수**라는 구조 상수다
	// (CharButton_0~7 이 컴파일 타임에 8개 존재한다). 최대 8인 매치(GDD)와 일치.
	// Characters.Num() 이 이보다 작으면 남는 슬롯은 Collapsed 로 접는다.
	static constexpr int32 MaxCharacterSlots = 8;

	virtual void NativeConstruct() override;

	// 폴링만 (MatchWidget 관례). 매 프레임 액터 탐색·캐스팅은 하지 않는다 — 포인터는 캐시.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ─── 순수 함수 (표시 가공) — 위젯 인스턴스 없이 테스트된다 ───

	// 남은 초 표시값 — **올림**이다 (0.2초 남아도 "1초", 0 이 되는 순간이 곧 종료).
	// 내림이면 마지막 1초 내내 "0초"가 떠 "끝났는데 왜 안 넘어가지"로 보인다. 음수는 0.
	static int32 CountdownWholeSeconds(float RemainingSeconds);

	// 남은 시간 → "N초". CountdownText 와 캔버스 폴백이 같은 함수를 쓴다.
	static FText FormatCountdown(float RemainingSeconds);

	// CharacterIndex 목록 → 점유 비트마스크 (비트 i = 캐릭터 i 점유됨).
	// INDEX_NONE(미선택)·범위 밖 인덱스는 무시하고, 중복 인덱스는 같은 비트에 겹친다
	// (서버가 중복을 막지만, 복제 도착 순서에 따라 한 프레임 겹쳐 보일 수 있다 — 표시는 관대하게).
	static uint32 BuildOccupiedMask(const TArray<int32>& CharacterIndices);

	// 캔버스 폴백 헤드라인 한 줄 — 남은 초 + 폴백 선택 경로 안내.
	static FString FormatSelectHeadline(float RemainingSeconds);

	// 캔버스 폴백 점유 현황 한 줄 — "[1]이름(점유)  [2]이름(내 선택)  [3]이름 …".
	// 내 선택 표기가 점유 표기보다 우선한다 (내 것도 점유 마스크에 들어 있다).
	// Names 가 비어 있으면 빈 문자열 (룰셋 미지정 — GameMode 가 페이즈 자체를 스킵하는 상태).
	static FString FormatOccupancyLine(const TArray<FText>& Names, uint32 OccupiedMask, int32 LocalIndex);

	// ─── 읽기 헬퍼 (출처 해석) — 캔버스 폴백과 공유한다 ───

	// PlayerArray → CharacterIndex 목록 (봇 포함 — 봇 자동 배정분도 점유다). GameState 없으면 빈 배열.
	static TArray<int32> CollectCharacterIndices(const ACA3DGameState* GameState);

	// 룰셋 → 표시 이름 목록. DisplayName 이 비어 있으면 "캐릭터 N" 으로 채운다 —
	// 에셋 연결 전에도 버튼이 무명으로 남지 않게 (위젯·폴백이 같은 함수를 써 표기가 같다).
	static TArray<FText> CollectCharacterNames(const UCA3DRuleSet* Rules);

protected:
	// WBP_CharacterSelect 의 같은 이름 위젯과 자동 바인딩.
	// BindWidget(필수)이 아니라 **BindWidgetOptional** — MatchWidget 과 같은 근거로, 필수면
	// 이름이 전부 맞을 때까지 WBP 가 컴파일되지 않아 첫 제작이 막힌다. 미바인딩 이름은
	// NativeConstruct 가 한 줄로 경고해 진단 가치를 유지한다.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_0;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_1;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_2;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_3;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_4;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_5;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_6;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> CharButton_7;

	// 각 버튼의 이름 라벨 (WBP 에서는 보통 버튼의 자식으로 두지만, 바인딩은 이름만 맞으면 된다).
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_0;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_1;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_2;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_3;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_4;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_5;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_6;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CharName_7;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CountdownText;

	// UButton::OnClicked 는 무인자 다이나믹 델리게이트라 인덱스를 실어 보낼 수 없다 —
	// 슬롯별 핸들러 8개가 HandleSelectClicked(i) 로 모인다 (로직은 한 곳).
	UFUNCTION() void OnCharButton0Clicked();
	UFUNCTION() void OnCharButton1Clicked();
	UFUNCTION() void OnCharButton2Clicked();
	UFUNCTION() void OnCharButton3Clicked();
	UFUNCTION() void OnCharButton4Clicked();
	UFUNCTION() void OnCharButton5Clicked();
	UFUNCTION() void OnCharButton6Clicked();
	UFUNCTION() void OnCharButton7Clicked();

private:
	// 선택 요청의 유일한 출구 — 컨트롤러의 공개 진입점에 전달만 한다 (클래스 주석).
	void HandleSelectClicked(int32 Index);

	// 점유·내 선택·이름을 8개 슬롯에 반영한다 — 스냅샷이 바뀐 프레임에만 불린다.
	void RefreshSlots(const TArray<FText>& Names, uint32 OccupiedMask, int32 MyIndex);

	// 바인딩 필드를 인덱스로 다루기 위한 캐시 — NativeConstruct 에서 한 번 채운다.
	// UPROPERTY 원본(CharButton_N)이 GC 를 지키므로 여기는 raw 포인터여도 안전하다.
	TArray<UButton*> Buttons;
	TArray<UTextBlock*> NameLabels;

	// 캐시 — 매 틱 GetGameState/캐스팅을 반복하지 않기 위한 것 (MatchWidget 관례).
	TWeakObjectPtr<ACA3DGameState> CachedGameState;

	// 마지막으로 화면에 반영한 스냅샷 — 달라진 프레임에만 위젯을 만진다.
	// 룰셋 포인터를 함께 비교하는 이유: 복제 도착 전에는 CDO 로 그리다가 에셋이 오면
	// 이름을 다시 채워야 한다 (MatchWidget ResolveRules 관례의 대가).
	int32 LastCountdownWholeSeconds = MIN_int32;
	uint32 LastOccupiedMask = MAX_uint32;
	int32 LastMyIndex = MIN_int32;
	int32 LastCharacterCount = MIN_int32;
	TWeakObjectPtr<const UCA3DRuleSet> LastRules;

	friend class FCharacterSelectWidgetTest; // 자동화 테스트가 기본값·캐시 상태를 확인하기 위한 접근
};
