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

	// 이 참가자를 **관전하는 사람이 볼 카메라 yaw** — 45도 스냅 인덱스(0~7).
	// 변환은 CameraYawSnap(Core/CameraYawSnap.h) 한 곳에만 있다.
	//
	// 왜 복제하는가: 카메라 yaw 는 원래 컨트롤러의 로컬 값(ControlRotation)이라 남의 화면으로
	// 건너가지 않는다. 그런데 관전 카메라의 각은 **대상 폰의 스프링암**(bUsePawnControlRotation)
	// → APawn::GetViewRotation() 이 정한다. 복제되는 표현 값이 없으면 관전자는
	//   · 봇을 볼 때     → AAIController 가 계산한 연속 각(45도 배수가 아니다)
	//   · 원격 폰을 볼 때 → 폰의 Controller 가 null 이라 엉뚱한 폴백 각
	// 을 보게 되어 격자가 비스듬히 눕고 카메라가 계속 미끄러진다.
	//
	// **관전 대상은 여전히 복제하지 않는다** (Checklist 29) — 그건 각자의 로컬 시점이다.
	// 여기 있는 것은 관전 상태가 아니라 **폰의 표현**이라 복제가 맞다. 8방향이라 uint8 하나면
	// 충분하고, 갱신은 값이 바뀌는 순간(Q/E · 봇의 방향 전환)뿐이라 대역폭도 사실상 0 이다.
	UPROPERTY(Replicated)
	uint8 CamYawIndex = 0;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
