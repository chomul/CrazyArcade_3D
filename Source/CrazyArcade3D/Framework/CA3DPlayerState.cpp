#include "Framework/CA3DPlayerState.h"

#include "Net/UnrealNetwork.h"

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
}
