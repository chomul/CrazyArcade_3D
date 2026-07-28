#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CA3DGameState.generated.h"

class UCA3DRuleSet;

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

	// 생존자 수 — HUD 표시용. 서버(GameMode)만 갱신 (갱신 로직은 Task 18 승패 판정에서).
	UPROPERTY(Replicated)
	int32 AliveCount = 0;

	// 매치 경과 시간 기준점 — 서든데스 카운트다운·HUD 타이머 계산용.
	// 클라는 GetServerWorldTimeSeconds() - MatchStartServerTime 으로 경과를 구한다.
	// 서든데스 발동 시각 등 비교 대상 수치는 전부 Rules 에서 읽는다 (매직 넘버 금지).
	UPROPERTY(Replicated)
	float MatchStartServerTime = 0.f;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
