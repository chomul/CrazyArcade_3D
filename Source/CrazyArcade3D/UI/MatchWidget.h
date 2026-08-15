#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MatchWidget.generated.h"

class UTextBlock;
class UPanelWidget;
class UWidget;
class APawn;
class ACA3DGameState;
class UCA3DRuleSet;
class UStatusComponent;

// 결과 화면 한 줄 (Task 26). 정렬·공동 등수 판정을 위젯 인스턴스 없이 검증할 수 있도록
// "표시에 필요한 값만" 담은 평면 구조로 뽑아 둔다 — 순수 함수의 입출력 타입이다.
USTRUCT()
struct FMatchResultRow
{
	GENERATED_BODY()

	// ACA3DPlayerState::FinalRank 사본 (1 = 우승, 0 = 미확정).
	UPROPERTY()
	int32 Rank = 0;

	UPROPERTY()
	FString PlayerName;

	// 같은 Rank 를 가진 행이 둘 이상인가 — "공동 3등" 표기용. BuildResultRows 가 채운다.
	UPROPERTY()
	bool bTied = false;

	// 이 행이 **화면을 보고 있는 본인**인가 (2026-08-06 사용자 요청).
	// 목록만으로는 "내가 몇 등인지" 를 이름으로 찾아야 해서 한눈에 안 들어온다.
	// CollectResultRows 가 로컬 PlayerState 와 대조해 채운다.
	UPROPERTY()
	bool bIsLocal = false;

	// 이 참가자가 매치 도중 나갔는가 (ACA3DPlayerState::bLeftMatch 사본, 2026-08-10).
	// **표시 속성일 뿐 순위 규칙이 아니다** — 정렬·공동 등수 판정은 이 값을 보지 않는다.
	// 나간 사람도 나간 그 자리에서 등수를 받았으므로 등수는 그대로 보여준다.
	UPROPERTY()
	bool bLeft = false;
};

// 내 아이템 상태 스냅샷 (GDD 5장 HUD ①). 매 틱 문자열을 다시 만들지 않기 위해
// "값이 바뀌었는지"를 이 구조 하나의 비교로 판정한다 (문자열 생성이 비용이다).
// 룰셋 Cap 을 함께 담는 이유: 상한에 도달했는지는 표시에만 쓰이고 판정은 서버가 이미 했다.
USTRUCT()
struct FMatchStatSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ActiveBombCount = 0;

	UPROPERTY()
	int32 MaxBombCount = 0;

	UPROPERTY()
	int32 BombRange = 0;

	UPROPERTY()
	float MoveSpeedMul = 0.f;

	UPROPERTY()
	bool bHasNeedle = false;

	UPROPERTY()
	bool bHasKick = false;

	UPROPERTY()
	int32 MaxBombCountCap = 0;

	UPROPERTY()
	int32 MaxBombRangeCap = 0;

	UPROPERTY()
	float MoveSpeedMulCap = 0.f;

	// 유효한 폰·StatusComponent 를 찾지 못한 상태 (사망 후 폰 소멸·접속 직후).
	UPROPERTY()
	bool bValid = false;

	bool operator==(const FMatchStatSnapshot& Other) const;
	bool operator!=(const FMatchStatSnapshot& Other) const { return !(*this == Other); }
};

// 매치 HUD 의 C++ 베이스 (Task 26). 레이아웃·비주얼은 전부 BP(WBP_Match),
// 여기는 바인딩 필드와 갱신 로직만 — GDD 5장 HUD 3요소 + 결과 화면.
//
// 데이터 흐름은 **한 방향뿐이다**: GameState / PlayerState / StatusComponent → 여기서 텍스트.
// 이 클래스는 게임 상태를 절대 바꾸지 않는다 (서버 RPC·Server* 호출·스탯 쓰기 금지 —
// 입력은 ACA3DPlayerController 소관). StatusComponent(Gameplay) 를 읽는 것은
// GDD 5장이 지정한 표시 항목이라 불가피한 예외이며, **읽기 전용**임을 여기 명시해 둔다.
//
// 표시 문자열 생성과 순위 정렬은 전부 static 순수 함수로 분리했다 —
// UExplosionSubsystem::Propagate 를 프리뷰·봇·테스트가 공유하는 것과 같은 이유로,
// 캔버스 폴백 HUD(ACA3DHUD::DrawHUD)와 위젯이 **같은 함수**를 통과해야 표시가 어긋나지 않는다.
UCLASS(Abstract)
class CRAZYARCADE3D_API UMatchWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 타이머·폴링만. 매 프레임 액터 탐색·캐스팅은 하지 않는다 (포인터는 캐시, 폰 교체 시 재해석).
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 결과 화면 표시 — ACA3DHUD::ShowResult 또는 bMatchEnded 전이 감지가 호출한다.
	// Task 26 명세는 순위 배열을 인자로 받게 돼 있었으나, 그러면 순위 수집이 HUD 와 위젯
	// 두 곳에 생긴다. 출처(GameState)를 여기서 한 번만 읽는다.
	void ShowResult();

	// ─── 순수 함수 (표시 가공) — 위젯 인스턴스 없이 테스트된다 ───

	// 경과 초 → "M:SS" (1시간 이상이면 "H:MM:SS"). 음수는 0 으로 클램프.
	static FText FormatElapsedTime(float ElapsedSeconds);

	static FText FormatAliveCount(int32 AliveCount);

	// 폭탄 "n/N" — 상한 도달 시 (MAX) 를 덧붙인다.
	static FText FormatBombCount(const FMatchStatSnapshot& Stats);
	static FText FormatBombRange(const FMatchStatSnapshot& Stats);
	static FText FormatMoveSpeed(const FMatchStatSnapshot& Stats);
	static FText FormatNeedle(const FMatchStatSnapshot& Stats);
	static FText FormatKick(const FMatchStatSnapshot& Stats);

	// 캔버스 폴백 한 줄용 — 위 항목을 이어 붙인다 (같은 함수를 재사용해 표시 불일치 차단).
	static FString FormatStatLine(const FMatchStatSnapshot& Stats);

	// 순위 정렬 + 공동 등수 표시. **종료 전이면 빈 배열을 돌려준다** — 결과가 새는 것을 막는 게이트.
	// Rank <= 0(미확정)은 뒤로 보낸다. 값 전달 후 정렬해 반환하므로 입력을 건드리지 않는다.
	static TArray<FMatchResultRow> BuildResultRows(TArray<FMatchResultRow> RawRows, bool bMatchEnded);

	// 무승부 규약의 **행 데이터 판**: 종료됐는데 Rank == 1 이 하나도 없으면 무승부.
	//
	// ⚠️ 화면 표시(ShowResult)는 이 함수를 쓰지 않는다 — 랭크는 PlayerState(다른 액터)라
	// bMatchEnded 와 도착 순서 보장이 없어, 행 스캔으로 판정하면 랭크가 한 프레임 늦은 클라가
	// 우승 매치를 무승부로 그린다 (2026-08-06 실측). 표시는 GameState->MatchWinner(원자 복제)를
	// 읽는다. 이 함수는 "랭크가 다 도착한 데이터라면 규약이 성립하는가"의 검증용으로 남긴다.
	static bool IsDrawResult(const TArray<FMatchResultRow>& Rows, bool bMatchEnded);

	// 결과 확정 게이트 (Task 40): 종료 + 행 존재 + **전원 Rank > 0** 일 때만 true.
	//
	// bMatchEnded(GameState)와 FinalRank(PlayerState)는 **서로 다른 액터라 복제 도착 순서
	// 보장이 없다** — 종료 플래그가 먼저 온 클라에는 랭크 0(미확정) 행이 섞여 있고, 그 중간
	// 상태를 화면에 내보내지 않는 게이트다. 서버는 bMatchEnded=true 가 되는 시점에 모든
	// 참가자의 랭크를 이미 확정했으므로(우승 1 / 사망·탈주 공동 등수 ≥ 2 / 무승부 전원 ≥ 2,
	// CA3DGameMode::ResolvePendingDeaths), 이 조건이 성립하는 첫 틱부터가 완성본이다.
	static bool IsResultDataComplete(const TArray<FMatchResultRow>& Rows, bool bMatchEnded);

	// "1등  Player 1" / 공동이면 "공동 3등  Bot 2". 본인 행에는 앞에 표식을 붙인다.
	static FText FormatResultRow(const FMatchResultRow& Row);

	// 결과 화면 **첫 줄** — 보고 있는 본인의 성적 (2026-08-06 사용자 요청).
	// 목록에서 자기 이름을 찾아야 등수를 아는 것이 불편하다는 지적에서 나왔다.
	//
	// bDraw 는 GameState->MatchWinner 로 판정한 값을 그대로 받는다 (행 스캔 금지 —
	// 랭크는 다른 액터라 늦게 올 수 있다, IsDrawResult 주석). 그래서 랭크가 아직 없어도
	// 이 줄의 승패 문구는 처음부터 맞다.
	static FText FormatLocalHeadline(const TArray<FMatchResultRow>& Rows, bool bDraw);

	// 매치 HUD 를 지금 보여야 하는가 (2026-08-16 사용자 요청 ②: "캐릭터 선택할 때 왼쪽 아래
	// 아이템 현황 UI 가 보인다 — 게임 안에서만 보이면 된다"). **둘 다 false 일 때만** true.
	// 매치 종료 후 결과 화면은 두 플래그가 모두 false 라 자연히 통과한다 — 별도 분기가 없다.
	static bool ShouldShowMatchHUD(bool bLobbyActive, bool bCharacterSelectActive);

	// 위 판정을 **위젯 루트 하나**에 적용한다.
	//
	// 개별 자식(ItemPanel 등)을 끄지 않는 이유: 아이템 텍스트가 ItemPanel 바깥에 있는 WBP
	// 계층이면 자식 단위로 끌 때 일부가 화면에 남는다 (2026-08-16 실측 증상). 루트를 접으면
	// 계층과 무관하게 사라진다.
	//
	// ⚠️ **구동자는 ACA3DHUD::Tick(액터 틱)이다.** Slate 는 위젯 Tick 을 Paint 경로에서
	// 실행하는데(SWidget::Paint 안의 EWidgetUpdateFlags::NeedsTick), Collapsed 위젯은 부모가
	// arrange 하지 않아 Paint 되지 않는다 — 스스로 접은 위젯은 NativeTick 이 멈춰 **자기 힘으로
	// 다시 펴지 못한다.** NativeTick 도 같은 함수를 부르지만(접는 프레임), 되돌리는 쪽은 HUD 다.
	void ApplyPhaseVisibility(bool bLobbyActive, bool bCharacterSelectActive);

	// ─── 읽기 헬퍼 (출처 해석) — 캔버스 폴백과 공유한다 ───

	// 폰에서 StatusComponent 를 찾는다 — 폰이 바뀔 때만 부르고 결과를 캐시할 것
	// (컴포넌트 배열 탐색이라 매 틱 호출은 낭비다).
	static const UStatusComponent* ResolveStatus(const APawn* Pawn);

	// StatusComponent + 룰셋 Cap → 표시 스냅샷. 둘 중 하나라도 없으면 bValid=false.
	static FMatchStatSnapshot CaptureStats(const UStatusComponent* Status, const UCA3DRuleSet* Rules);

	// GameState->PlayerArray → 정렬된 결과 행. 종료 전이면 빈 배열.
	// LocalPlayerState 와 같은 항목에 bIsLocal 을 세운다 (nullptr 이면 아무 행도 표시 안 됨 —
	// 관전자·PlayerState 미도착 상황에서도 목록 자체는 정상이어야 한다).
	static TArray<FMatchResultRow> CollectResultRows(const ACA3DGameState* GameState,
	                                                 const APlayerState* LocalPlayerState = nullptr);

	// GameState 의 복제 룰셋 포인터, 없으면 CDO (StatusComponent::ResolveRules 와 같은 관례).
	static const UCA3DRuleSet* ResolveRules(const UWorld* World);

protected:
	// WBP_Match 의 같은 이름 위젯과 자동 바인딩.
	// ⚠️ 명세의 BindWidget(필수) 대신 **BindWidgetOptional** 을 쓴다 — 필수 바인딩이면
	// 이름이 전부 맞을 때까지 WBP 자체가 컴파일되지 않아 첫 제작이 막힌다.
	// 대신 NativeConstruct 가 비어 있는 이름을 한 줄로 경고해 진단 가치를 유지한다.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> AliveCountText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MatchTimeText;

	// 아이템 상태 묶음 (GDD 5장 ①). 패널은 표시/숨김만, 수치는 아래 텍스트가 담당한다.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> ItemPanel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BombCountText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BombRangeText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MoveSpeedText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> NeedleText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> KickText;

	// 서든데스 경고 (GDD 5장 ③) — **항상 Collapsed**. 상태 출처는 Task 24 가 만든다.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> SuddenDeathWarning;

	// 결과 화면 (순위) — 평소 Collapsed.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> ResultPanel;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ResultText;

private:
	// 서든데스 경고 표시/숨김 (Task 24 연결 완료).
	// 인자의 출처는 ACA3DGameState::bSuddenDeathActive — GameMode 가 서든데스 발동 시
	// 세우고 매치 종료 시 내리는 복제 플래그다. 위젯은 읽기만 한다.
	void UpdateSuddenDeathWarning(bool bActive);

	// 폰이 바뀌었으면(사망·리스폰·관전 전환) StatusComponent 포인터를 다시 해석한다.
	void RefreshPawnCache();

	// 캐시 — 매 틱 GetGameState/캐스팅/컴포넌트 탐색을 반복하지 않기 위한 것.
	// 약참조라 폰 파괴를 안전하게 감지한다 (댕글링 없이 bValid=false 로 떨어진다).
	TWeakObjectPtr<ACA3DGameState> CachedGameState;
	TWeakObjectPtr<const APawn> CachedPawn;
	TWeakObjectPtr<const UStatusComponent> CachedStatus;

	// 마지막으로 텍스트를 만든 값 — 달라진 항목만 다시 만든다.
	FMatchStatSnapshot LastStats;
	int32 LastAliveCount = MIN_int32;
	int32 LastElapsedWholeSeconds = MIN_int32;

	// 마지막으로 적용한 페이즈 가시성 (1 = 표시, 0 = 접힘). bool 이 아니라 int32 인 이유는
	// MIN_int32 초기값이 "아직 한 번도 적용 안 됨"을 뜻해 첫 적용이 반드시 통과하기 때문이다.
	// 위 Last* 캐시들과 **독립**이다 — 접힌 동안 그 캐시들은 손대지 않으므로(NativeTick 조기
	// 반환), 다시 보이는 첫 틱에 스냅샷 비교가 전부 정상 복원한다.
	int32 LastPhaseVisibleFlag = MIN_int32;

	// 결과 패널을 **게이트를 통과해 실제로 표시했는가** (Task 40 — 게이트에 걸려 return 한
	// 호출에서는 세우지 않는다. "ShowResult 가 불렸는가"가 아니다).
	bool bResultShown = false;

	// 마지막으로 그린 결과 본문 — 결과 화면은 종료 후에도 **매 틱 다시 만든다** (아래 이유).
	// 같으면 SetText 를 건너뛰고, 달라졌을 때만 로그를 찍는다 (전이 감지).
	//
	// 굳히지 않는 이유 (2026-08-06 실측 버그): bMatchEnded 는 GameState, FinalRank 는
	// PlayerState — **서로 다른 액터라 복제 도착 순서 보장이 없다.** 클라에서 bMatchEnded 가
	// 먼저 오면 우승자 랭크가 아직 0이라, 첫 프레임에 굳히면 잘못된 표시를 영영 안 고친다
	// (서버 로그 "우승자 확정" vs 위젯 "무승부"가 실제로 어긋났다). 지금은 여기에 더해
	// IsResultDataComplete 게이트가 **랭크가 다 도착할 때까지 패널 자체를 숨기므로**(Task 40),
	// 다 도착한 틱에 완성본으로 첫 표시되고 이 본문 비교는 표시 후 재작업만 걸러 준다.
	FString LastResultBody;

	friend class FMatchWidgetTest; // 자동화 테스트가 갱신 캐시 상태를 확인하기 위한 접근
};
