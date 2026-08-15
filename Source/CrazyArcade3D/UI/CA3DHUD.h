#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CA3DHUD.generated.h"

class UMatchWidget;
class UCharacterSelectWidget;
class ULobbyWidget;

// 매치 위젯(Task 26)·캐릭터 선택 위젯(Task 36 후속)·로비 위젯(Task 41)의 수명 관리자 (Task 25).
// **클라 전용 · 순수 시각**이다.
// 표시 데이터 가공은 위젯(UMatchWidget 의 static 순수 함수), 원본은 GameState/PlayerState/StatusComponent.
// 게임 상태를 바꾸지 않는다 — 서버 RPC 호출도 없다 (입력은 ACA3DPlayerController 소관).
//
// 데디 서버에는 HUD 가 애초에 생성되지 않지만 BeginPlay 최상단 가드를 유지한다 (GDD 7.4).
// ⚠️ 이 가드를 다른 클래스에 흉내 내지 말 것: AVoxelWorld 의 HISM 은 "시각 전용"처럼 보이지만
// 지형의 유일한 컬리전이라 데디에서 끄면 서버에 바닥이 사라진다 (CLAUDE.md 알려진 함정).
// HUD 는 컬리전·판정·복제를 하나도 갖지 않으므로 꺼도 되는 몇 안 되는 대상이다.
UCLASS()
class CRAZYARCADE3D_API ACA3DHUD : public AHUD
{
	GENERATED_BODY()

public:
	// MatchWidgetClass 가 지정돼 있으면 생성해 뷰포트에 올린다.
	virtual void BeginPlay() override;

	// 로비·캐릭터 선택 위젯 수명 폴링 — 페이즈 전이는 GameState 복제 플래그로만 안다
	// (전용 이벤트 없음). 상태 기반이라 "이미 페이즈 중에 들어온 늦은 접속자"도 첫 틱에
	// 같은 경로로 위젯을 띄운다. 매치 위젯의 페이즈 숨김도 여기서 구동한다 (아래 함수 주석).
	virtual void Tick(float DeltaSeconds) override;

	// 캔버스 텍스트 폴백 — WBP 를 만들기 전에도 숫자가 보이게 한다 (개발 편의, 시각 전용).
	virtual void DrawHUD() override;

	// 매치 종료 시 결과 화면 전환 (순위 — GDD 5장). 위젯이 bMatchEnded 전이를 스스로도
	// 감지하므로 이 함수는 "외부에서 앞당겨 부를 수 있는 문"이다 (중복 호출 무해).
	void ShowResult();

protected:
	// BP(에셋 지정만): WBP_Match 를 가리킨다. C++ 는 UMatchWidget 베이스만 안다.
	// 비어 있으면 위젯 대신 캔버스 폴백이 그려진다 — 에디터 작업 0 으로도 값이 보이게.
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UMatchWidget> MatchWidgetClass;

	// BP(에셋 지정만): WBP_CharacterSelect 를 가리킨다. 비어 있으면 캔버스 폴백이 선택 페이즈
	// 정보를 대신 그린다 (선택은 개발용 콘솔 ca3d.SelectCharacter, 미선택자는 자동 배정).
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UCharacterSelectWidget> CharacterSelectWidgetClass;

	// BP(에셋 지정만): WBP_Lobby 를 가리킨다 (Task 41). 비어 있으면 캔버스 폴백이 참가자 목록·
	// 준비 상태를 대신 그린다 (진행은 개발용 콘솔 ca3d.Ready / ca3d.StartMatch).
	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<ULobbyWidget> LobbyWidgetClass;

private:
	// 매치 폴백을 그릴지 — 콘솔 변수 ca3d.DebugHUD (-1 자동 / 0 끄기 / 1 강제).
	bool ShouldDrawDebugFallback() const;

	// 선택 페이즈 폴백을 그릴지 — 같은 콘솔 변수, 자동 판정 기준만 다르다
	// (담당 위젯이 CharacterSelectWidget 이므로 그것의 유무를 본다).
	bool ShouldDrawSelectFallback() const;

	// 로비 폴백을 그릴지 — 같은 판정 방식, 담당 위젯이 LobbyWidget 이므로 그것의 유무를 본다.
	bool ShouldDrawLobbyFallback() const;

	// Tick 폴링 본문 — bCharacterSelectActive 와 위젯 유무를 맞춘다 (생성/RemoveFromParent).
	void UpdateCharacterSelectLifetime();

	// 〃 로비판 — bLobbyActive 와 위젯 유무를 맞춘다 (완전히 같은 상태 기반 패턴).
	void UpdateLobbyLifetime();

	// 매치 위젯의 **페이즈 숨김 구동자** (2026-08-16 요청 ② — 아이템 현황 UI 는 게임 안에서만).
	//
	// ⚠️ 왜 위젯이 아니라 HUD 가 구동하는가: Slate 는 위젯 Tick 을 **Paint 경로에서** 실행하고
	// (SWidget::Paint 안의 EWidgetUpdateFlags::NeedsTick), Collapsed 위젯은 부모가 arrange 하지
	// 않아 Paint 되지 않는다 — 즉 **스스로 접은 위젯은 NativeTick 이 멈춰 자기 힘으로 다시 펴지
	// 못한다.** 액터 틱인 여기가 매 틱 같은 판정을 적용해 되돌린다 (판정 공식은 위젯의 static
	// 순수 함수 하나 — UMatchWidget::ShouldShowMatchHUD).
	void UpdateMatchWidgetVisibility();

	UPROPERTY()
	TObjectPtr<UMatchWidget> MatchWidget;

	// 선택 페이즈 동안만 산다 — MatchWidget(매치 내내 유지)과 달리 종료 전이에서 내려간다.
	UPROPERTY()
	TObjectPtr<UCharacterSelectWidget> CharacterSelectWidget;

	// 로비 페이즈 동안만 산다 (CharacterSelectWidget 과 같은 수명 규칙).
	UPROPERTY()
	TObjectPtr<ULobbyWidget> LobbyWidget;

	friend class FMatchWidgetTest;           // 자동화 테스트가 위젯 클래스 지정·폴백 조건을 확인하기 위한 접근
	friend class FCharacterSelectWidgetTest; // 〃 (캐릭터 선택 위젯 스위트)
	friend class FLobbyWidgetTest;           // 〃 (로비 위젯 스위트)
};
