#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CA3DGameState.generated.h"

class UCA3DRuleSet;
class ACA3DPlayerState;

// 클라에 복제되는 매치 상태 (Task 08). 로직 없음 — 갱신은 전부 GameMode(서버)가,
// 소비는 UI(읽기 전용)와 클라 프리뷰 계산이 한다.
//
// 핵심은 룰셋 "에셋 포인터" 복제 — UE는 에셋 참조를 경로로 복제하므로 값 전체가
// 아니라 참조만 오간다. 클라의 폭탄 타이머 표시·위험 구역 프리뷰·맵 재생성이
// 서버와 같은 값으로 계산되게 하는 유일한 데이터 출처다.
UCLASS()
class CRAZYARCADE3D_API ACA3DGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// 룰셋 에셋 포인터 — GameMode(서버)가 BeginPlay에서 세팅.
	UPROPERTY(Replicated)
	TObjectPtr<UCA3DRuleSet> Rules;

	// 생존자 수 — HUD 표시용. 서버(GameMode)만 갱신 (입장 시 +1, 사망 해소 시 -N).
	UPROPERTY(Replicated)
	int32 AliveCount = 0;

	// 매치 종료 여부 (Task 18). 결과 화면(Task 26)의 표시 트리거.
	//
	// ⚠️ 무승부는 별도 bool 플래그를 두지 않는다 — 규약:
	//   bMatchEnded == true 인데 MatchWinner == nullptr 면 무승부다.
	// bool 을 하나 더 두면 "종료=true, 무승부=true, 그런데 우승자가 있음" 같은 모순 상태를
	// 표현할 수 있게 된다. 우승자 유무 자체가 유일한 사실이므로 그것(아래 포인터)만 남긴다.
	UPROPERTY(Replicated)
	bool bMatchEnded = false;

	// 우승자 (nullptr = 아직 안 끝났거나 무승부). 서버(GameMode)만 쓴다 — bMatchEnded 와 동시에.
	//
	// **여기(GameState)에 있는 것 자체가 요점이다** (2026-08-06 무승부 오표시 버그의 수정):
	// 예전엔 클라가 "FinalRank==1 인 PlayerState 있음?"으로 우승자를 찾았는데, FinalRank 는
	// **다른 액터**(PlayerState)라 bMatchEnded 와 복제 도착 순서 보장이 없다 — 랭크가 한 프레임
	// 늦으면 우승 매치가 무승부로 보였다. 같은 액터의 프로퍼티는 한 번들로 원자 복제되므로
	// 판정을 bMatchEnded 옆에 실으면 종료를 아는 순간 승패도 반드시 함께 안다.
	// (전원 랭크 도착을 기다리는 방식은 랭크가 영영 없는 관전자가 생기는 순간 화면이 영영 안 뜬다.)
	UPROPERTY(Replicated)
	TObjectPtr<ACA3DPlayerState> MatchWinner;

	// 매치 경과 시간 기준점 — 서든데스 카운트다운·HUD 타이머 계산용.
	// 클라는 GetServerWorldTimeSeconds() - MatchStartServerTime 으로 경과를 구한다.
	// 서든데스 발동 시각 등 비교 대상 수치는 전부 Rules 에서 읽는다 (매직 넘버 금지).
	UPROPERTY(Replicated)
	float MatchStartServerTime = 0.f;

	// 서든데스 진행 여부 (Task 24). 갱신 주체는 **GameMode 단독** — 이 클래스는 로직이 없다.
	// (낙하 스케줄러인 USuddenDeathSubsystem 이 직접 쓰지 않는 이유: GameState 갱신 주체가
	//  둘이 되면 "구동 중인데 플래그는 false" 같은 모순 상태가 표현 가능해진다.)
	//
	// 소비처 둘 다 **읽기 전용**:
	//   · UMatchWidget — HUD 서든데스 경고 표시 (GDD 5장 ③)
	//   · ACA3DCharacter — KillZ 낙사의 사망 원인 분기 (Fall / SuddenDeath)
	UPROPERTY(Replicated)
	bool bSuddenDeathActive = false;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
