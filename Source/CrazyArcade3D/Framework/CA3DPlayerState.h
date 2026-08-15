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

	// 선택한 캐릭터 (Task 36) — UCA3DRuleSet::Characters 배열 인덱스. INDEX_NONE = 미선택.
	// 로직 없음(이 클래스의 원칙) — 검증·배정은 전부 서버의 단일 경로
	// ACA3DGameMode::TryAssignCharacter 가 한다. 외형 적용(메시·애님 교체)은 Task 37 이
	// 이 값을 읽어 수행한다. 매치 도중 캐릭터가 파괴·리스폰돼도 선택은 남아야 하므로 여기다.
	UPROPERTY(Replicated)
	int32 CharacterIndex = INDEX_NONE;

	// ─── 로비 페이즈 (Task 41) ─── 로직 없음(이 클래스의 원칙) — 판정·갱신은 전부 서버의
	// 단일 경로 ACA3DGameMode(TrySetReady / RegisterParticipant / HandleParticipantLeft)가 한다.

	// 로비 준비 완료. **방장은 항상 무시된다** — 개념상 준비 대상이 아니다(자기 자신에게
	// 준비를 알릴 이유가 없다). 시작 조건은 "방장을 제외한 전원이 준비"이며 그 판정 공식은
	// ACA3DGameMode::CanStartFromLobby 한 곳에만 있다 (서버 판정과 UI 버튼 활성이 같은 공식).
	UPROPERTY(Replicated)
	bool bReady = false;

	// 방장 = **첫 입장 사람**. 봇은 절대 방장이 아니다 — 봇은 매치 시작 후에 투입되는 인원이라
	// 시작 버튼을 누를 주체가 될 수 없다(그러면 사람이 하나도 준비되지 않아도 판이 시작된다).
	// 방장이 나가면 남은 사람 중 가장 먼저 들어온 사람(ColorIndex 최소)에게 승계된다.
	UPROPERTY(Replicated)
	bool bIsHost = false;

	// 탈락 순위 (0 = 아직 생존, 1 = 우승, N = N등). 매치 종료 판정·결과 화면용.
	// 동시 사망자는 같은 값을 공유한다 (공동 등수 — GameMode::ResolvePendingDeaths 참조).
	UPROPERTY(Replicated)
	int32 FinalRank = 0;

	// 관전 전환 등 UI 편의용 생존 미러. 원본은 StatusComponent::LifeState 이며,
	// 이 값은 폰이 사라진 뒤에도 읽을 수 있게 복제해 두는 사본이다.
	UPROPERTY(Replicated)
	bool bAlive = true;

	// 이 참가자를 **관전하는 사람이 볼 카메라 yaw** — 90도 스냅 인덱스(0~3).
	// 변환은 CameraYawSnap(Core/CameraYawSnap.h) 한 곳에만 있다.
	//
	// 왜 복제하는가: 카메라 yaw 는 원래 컨트롤러의 로컬 값(ControlRotation)이라 남의 화면으로
	// 건너가지 않는다. 그런데 관전 카메라의 각은 **대상 폰의 스프링암**(bUsePawnControlRotation)
	// → APawn::GetViewRotation() 이 정한다. 복제되는 표현 값이 없으면 관전자는
	//   · 봇을 볼 때     → AAIController 가 계산한 연속 각(90도 배수가 아니다)
	//   · 원격 폰을 볼 때 → 폰의 Controller 가 null 이라 엉뚱한 폴백 각
	// 을 보게 되어 격자가 비스듬히 눕고 카메라가 계속 미끄러진다.
	//
	// **관전 대상은 여전히 복제하지 않는다** (Checklist 29) — 그건 각자의 로컬 시점이다.
	// 여기 있는 것은 관전 상태가 아니라 **폰의 표현**이라 복제가 맞다. 4방향이라 uint8 하나면
	// 충분하고, 갱신은 값이 바뀌는 순간(Q/E · 봇의 방향 전환)뿐이라 대역폭도 사실상 0 이다.
	UPROPERTY(Replicated)
	uint8 CamYawIndex = 0;

	// 이 참가자가 **매치 도중 접속을 끊었는가** (2026-08-10 사용자 확정 규칙).
	// 나간 사람은 그 자리에서 사망 처리해 순위를 받고, 결과 화면에는 등수 옆에 "(탈주)" 가 붙는다.
	// 참가 인원에서 빼지는 않는다 — 그 사람은 매치에 참가했고 자리를 차지했다.
	//
	// ⚠️ **엔진의 `bIsInactive` 를 재사용하지 않는 이유**: 그 플래그는 우리가 소유한 값이 아니다.
	// 엔진의 inactive player 시스템(AGameMode::AddInactivePlayer / 재접속 시 복원)이 세우고
	// 지우며, 우리가 모르는 시점에 바뀔 수 있다. 결과 화면이 뜻해야 하는 것은 정확히
	// **"이 매치를 끝까지 안 뛰었다"** 이며, 그 판단의 주인은 ACA3DGameMode::Logout 하나뿐이어야 한다.
	// (게다가 우리는 AGameModeBase 파생이라 AddInactivePlayer 자체가 없다 — 그 플래그를 세울
	//  엔진 경로가 애초에 이 프로젝트에 없다.)
	UPROPERTY(Replicated)
	bool bLeftMatch = false;

	// 소유 컨트롤러가 사라질 때 엔진이 부르는 훅 — **PlayerState 를 파괴하지 않는다.**
	//
	// 기본 구현(APlayerState::OnDeactivated)은 `Destroy()` 한 줄이다. 그대로 두면 나간 사람의
	// PlayerState 가 GameState->PlayerArray 에서 사라져 **결과 화면에서 그 사람이 통째로
	// 증발한다** — 등수를 부여해 놓고 그 등수를 보여줄 데이터가 없어지는 셈이다.
	// (엔진 주석도 "games can override" 라고 명시한다.)
	//
	// 대가: 매치가 끝날 때까지 나간 사람의 PlayerState 액터가 남는다. 최대 8인이고 값은
	// int32 몇 개라 비용은 무시할 수준이며, 레벨 전환에서 월드와 함께 회수된다.
	// 되살릴 필요도 없다 — GDD 6.3 에 재접속이 없다.
	virtual void OnDeactivated() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
