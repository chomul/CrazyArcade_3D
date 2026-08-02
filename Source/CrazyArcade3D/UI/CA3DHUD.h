#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CA3DHUD.generated.h"

class UMatchWidget;

// 매치 위젯(Task 26)의 수명 관리자 (Task 25). **클라 전용 · 순수 시각**이다.
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

private:
	// 폴백을 그릴지 — 콘솔 변수 ca3d.DebugHUD (-1 자동 / 0 끄기 / 1 강제).
	bool ShouldDrawDebugFallback() const;

	UPROPERTY()
	TObjectPtr<UMatchWidget> MatchWidget;

	friend class FMatchWidgetTest; // 자동화 테스트가 위젯 클래스 지정·폴백 조건을 확인하기 위한 접근
};
