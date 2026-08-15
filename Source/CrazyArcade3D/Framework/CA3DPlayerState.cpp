#include "Framework/CA3DPlayerState.h"

#include "CrazyArcade3D.h"
#include "Net/UnrealNetwork.h"

void ACA3DPlayerState::OnDeactivated()
{
	// **Super 를 부르지 않는다** — 그게 곧 Destroy() 다 (헤더 주석의 근거).
	// 여기서 파괴하면 나간 사람이 GameState->PlayerArray 에서 빠지고, ACA3DGameMode::Logout 이
	// 방금 부여한 순위·탈주 표시를 보여줄 데이터가 사라진다. 되살리는(재접속) 경로는 없으므로
	// 남겨두기만 하면 되고, 액터는 레벨 전환에서 월드와 함께 회수된다.
	UE_LOG(LogCA3D, Log, TEXT("ACA3DPlayerState %s: 컨트롤러 이탈 — PlayerState 는 결과 화면을 위해 남긴다 (등수 %d%s)"),
		*GetPlayerName(), FinalRank, bLeftMatch ? TEXT(", 탈주") : TEXT(""));
}

void ACA3DPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 조건 없이 전원에게 복제한다 — 결과 화면·관전 UI 가 "남의 등수"를 그려야 하므로
	// COND_OwnerOnly 로 좁히면 안 된다 (셋 다 int32/bool 한 개짜리라 대역폭도 문제되지 않는다).
	DOREPLIFETIME(ACA3DPlayerState, ColorIndex);
	DOREPLIFETIME(ACA3DPlayerState, FinalRank);
	DOREPLIFETIME(ACA3DPlayerState, bAlive);

	// 관전 카메라 각도 전원에게 — 누구든 이 참가자를 관전 대상으로 고를 수 있다.
	// COND_OwnerOnly 로 좁히면 정작 필요한 사람(관전자)에게만 안 간다.
	DOREPLIFETIME(ACA3DPlayerState, CamYawIndex);

	// 탈주 표시도 전원에게 — 정작 이 값을 화면에 그려야 하는 사람은 **남아 있는 사람들**이다
	// (나간 본인에게는 보낼 커넥션조차 없다).
	DOREPLIFETIME(ACA3DPlayerState, bLeftMatch);

	// 캐릭터 인덱스도 전원에게 — 선택 화면이 "남이 이미 고른 캐릭터"를 잠금 표시해야 하고
	// (선착순 규칙의 화면 표현), 외형(Task 37)은 모든 클라가 그린다.
	DOREPLIFETIME(ACA3DPlayerState, CharacterIndex);

	// 로비 준비·방장 표시도 전원에게 (Task 41) — 로비 화면은 **남들의** 준비 현황과 누가 방장인지를
	// 그려야 하고(내 것만 보이면 "누구를 기다리는지" 를 알 수 없다), 시작 버튼 활성 판정도
	// 모든 클라가 서버와 같은 공식(CanStartFromLobby)으로 계산한다 — COND_OwnerOnly 로 좁히면
	// 정작 필요한 쪽에 값이 안 간다.
	DOREPLIFETIME(ACA3DPlayerState, bReady);
	DOREPLIFETIME(ACA3DPlayerState, bIsHost);
}
