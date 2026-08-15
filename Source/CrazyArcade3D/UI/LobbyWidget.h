#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyWidget.generated.h"

class UButton;
class UTextBlock;
class ACA3DGameState;
class APlayerState;

// 로비 위젯의 C++ 베이스 (Task 41 — 2026-08-16 사용자 요청 ①: "사람이 모이고 게임 시작 버튼이
// 눌린 다음에 캐릭터 선택창이 진행되어야 한다. 처음 들어온 사람이 방장, 나머지는 준비 버튼").
// 레이아웃·비주얼은 전부 BP(WBP_Lobby), 여기는 바인딩 필드와 갱신 로직만.
//
// 구조는 UCharacterSelectWidget 을 그대로 따른다:
//   · 데이터 흐름은 **한 방향 폴링** — GameState / PlayerArray → 여기서 표시.
//     OnRep 을 UI 가 직접 잡지 않는다. GameState(bLobbyActive)와 PlayerState(bReady·bIsHost)는
//     **서로 다른 액터라 복제 도착 순서 보장이 없어**, 이벤트 체인으로 엮으면 "준비는 켜졌는데
//     목록은 옛날 값" 같은 중간 상태가 화면에 남는다. 매 틱 읽고 스냅샷이 바뀐 프레임에만 만진다.
//   · 표시 가공은 전부 static 순수 함수 — 캔버스 폴백 HUD(ACA3DHUD::DrawHUD)와 위젯이
//     **같은 함수**를 통과해야 표시가 어긋나지 않고, 위젯 인스턴스 없이 테스트된다.
//
// ⚠️ **시작 가능 판정을 여기서 다시 계산하지 않는다.** 반드시 서버와 같은 함수
// (ACA3DGameMode::CanStartFromLobby / CountLobbyReadiness)를 통과한다 — 공식이 두 벌이면
// "버튼은 활성인데 눌러도 시작이 안 되는" 어긋남이 생긴다 (UExplosionSubsystem::Propagate 를
// 프리뷰·봇·서버가 공유하는 것과 같은 근거).
//
// 버튼 클릭만은 "쓰기"처럼 보이지만, 위젯이 상태를 바꾸는 것이 아니라 **컨트롤러의 공개
// 진입점(ACA3DPlayerController::ServerSetReady / ServerStartMatch)을 부를 뿐**이다.
// 검증·판정·시작은 전부 서버 GameMode 소관이고, 응답 RPC 도 없다 — 성공이면 복제된
// bReady / bLobbyActive 가 바뀌고, 거부면 안 바뀐다. 상태 복제가 곧 응답이다.
UCLASS(Abstract)
class CRAZYARCADE3D_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 폴링만 (MatchWidget·CharacterSelectWidget 관례). 매 프레임 액터 탐색·캐스팅은 하지 않는다.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ─── 순수 함수 (표시 가공) — 위젯 인스턴스 없이 테스트된다 ───

	// 참가자 한 줄 — "★ 방장  Player 1" / "[준비]  Player 2" / "[대기]  Player 3".
	// 앞 3칸은 본인 표식(▶) 자리다 (남의 행은 공백으로 들여써 열 정렬을 유지 — FormatResultRow 관례).
	// 방장 행은 bReady 를 보지 않는다: 방장은 준비 대상이 아니라 **시작을 누르는 사람**이며,
	// 서버의 인원 집계(CountLobbyReadiness)도 방장을 세지 않는다.
	static FString FormatRosterLine(const FString& PlayerName, bool bIsHost, bool bReady, bool bIsLocal);

	// 안내 문구 — 방장/비방장으로 갈린다. 시작 가능 여부는 **CanStartFromLobby** 가 판정한다
	// (여기서 부등호를 다시 쓰지 않는다 — 클래스 주석).
	static FText FormatLobbyStatus(int32 NonHostCount, int32 ReadyNonHostCount, bool bIsHost);

	// 준비 버튼 라벨 — 지금 준비 상태면 "준비 해제", 아니면 "준비" (누르면 일어날 일).
	static FText FormatReadyButtonLabel(bool bReady);

	// 캔버스 폴백 헤드라인 한 줄 — 로비임을 알리고 WBP 없이 쓸 콘솔 경로를 안내한다.
	static FString FormatLobbyHeadline(bool bIsHost);

	// ─── 읽기 헬퍼 (출처 해석) — 캔버스 폴백과 공유한다 ───

	// PlayerArray → 참가자 표시 줄 목록 (고정 순서 = 접속 순서라 매 프레임 순서가 흔들리지 않는다).
	// **봇은 제외한다** — 봇은 로비에 앉아 준비를 누르는 참가자가 아니라 매치 시작 시점에
	// 서버가 채우는 인원이다 (판별은 APlayerState::IsABot(), ACA3DGameMode 가 SetIsABot 으로
	// 세워 두는 프로젝트 기존 관례).
	// bIsLocal 표식은 **이름이 아니라 포인터로** 대조한다 — 같은 이름이 둘일 수 있다.
	static TArray<FString> CollectRosterLines(const ACA3DGameState* GameState,
	                                          const APlayerState* LocalPlayerState);

	// 캔버스 폴백 전체 줄 (헤드라인 + 상태 + 참가자 목록). WBP_Lobby 없이도 로비가 보이고
	// 콘솔 명령(ca3d.Ready / ca3d.StartMatch)으로 진행까지 된다 — "에디터 작업 0 으로도 검증"
	// 이라는 이 프로젝트 UI 관례. GameState 가 없으면 빈 배열.
	static TArray<FString> BuildLobbyFallbackLines(const ACA3DGameState* GameState,
	                                               const APlayerState* LocalPlayerState);

protected:
	// WBP_Lobby 의 같은 이름 위젯과 자동 바인딩.
	// BindWidget(필수)이 아니라 **BindWidgetOptional** — 필수면 이름이 전부 맞을 때까지 WBP 가
	// 컴파일되지 않아 첫 제작이 막힌다. 미바인딩 이름은 NativeConstruct 가 한 줄로 경고한다.
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RosterText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ReadyButton;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ReadyButtonText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> StartButton;

	// 클릭 핸들러 — 컨트롤러의 공개 진입점을 부르는 얇은 껍데기다 (클래스 주석).
	UFUNCTION() void OnReadyClicked();
	UFUNCTION() void OnStartClicked();

private:
	// 상태 문구·버튼 가시성/활성을 한 곳에서 반영한다 — 스냅샷이 바뀐 프레임에만 불린다.
	void RefreshControls(int32 NonHostCount, int32 ReadyNonHostCount, bool bIsHost, bool bReady);

	// 캐시 — 매 틱 GetGameState/캐스팅을 반복하지 않기 위한 것 (MatchWidget 관례).
	TWeakObjectPtr<ACA3DGameState> CachedGameState;

	// 마지막으로 화면에 반영한 스냅샷 — 달라진 프레임에만 위젯을 만진다.
	// 목록은 본문 문자열로 비교한다 (행 수·이름·준비 상태가 한 값에 다 들어간다).
	FString LastRosterBody;

	// 빈 목록도 **한 번은** 반영해야 한다 — LastRosterBody 초기값이 빈 문자열이라
	// 이 플래그가 없으면 "참가자 0명" 상태에서 SetText 가 영영 안 불린다.
	bool bRosterApplied = false;

	// bool 대신 int32 로 두는 이유: MIN_int32 초기값이 "아직 한 번도 반영 안 됨"을 뜻해
	// 첫 틱이 반드시 통과한다 (bool 이면 초기값이 실제 값과 우연히 같을 때 첫 반영을 건너뛴다).
	int32 LastNonHostCount = MIN_int32;
	int32 LastReadyNonHostCount = MIN_int32;
	int32 LastHostFlag = MIN_int32;
	int32 LastReadyFlag = MIN_int32;

	friend class FLobbyWidgetTest; // 자동화 테스트가 기본값·캐시 상태를 확인하기 위한 접근
};
