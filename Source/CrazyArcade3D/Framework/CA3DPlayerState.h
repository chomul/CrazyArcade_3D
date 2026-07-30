#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CA3DPlayerState.generated.h"

// 플레이어별 매치 상태 (Task 18). 로직 없음 — 순수 복제 데이터다.
//
// 왜 StatusComponent 가 아니라 여기인가: StatusComponent 는 폰에 붙어 있어 폰과 함께 사라진다.
// 사망 후 관전 전환·매치 종료 결과 화면(Task 26)은 "죽은 사람의 등수"를 읽어야 하는데,
// 그때 폰이 남아 있으리라는 보장이 없다. **캐릭터가 파괴돼도 남아야 하는 정보**만 여기 둔다.
//
// 갱신 주체는 ACA3DGameMode(서버) 단독이다 (불변식 5). 이 클래스에 판정 로직을 넣으면
// 동시 사망 같은 경계 상황에서 판정 주체가 둘이 되어 결과가 갈린다 — 여기는 데이터만.
// 전적 저장은 없다 (GDD 6.3) — 매치가 끝나면 이 값들은 함께 사라진다.
UCLASS()
class CRAZYARCADE3D_API ACA3DPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	// 캐릭터 색상 인덱스 — 1종 캐릭터 + 색 구분 (GDD 5장). GameMode 가 접속 순서로 배정한다.
	UPROPERTY(Replicated)
	int32 ColorIndex = 0;

	// 탈락 순위 (0 = 아직 생존, 1 = 우승, N = N등). 매치 종료 판정·결과 화면용.
	// 동시 사망자는 같은 값을 공유한다 (공동 등수 — GameMode::ResolvePendingDeaths 참조).
	UPROPERTY(Replicated)
	int32 FinalRank = 0;

	// 관전 전환 등 UI 편의용 생존 미러. 원본은 StatusComponent::LifeState 이며,
	// 이 값은 폰이 사라진 뒤에도 읽을 수 있게 복제해 두는 사본이다.
	UPROPERTY(Replicated)
	bool bAlive = true;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
