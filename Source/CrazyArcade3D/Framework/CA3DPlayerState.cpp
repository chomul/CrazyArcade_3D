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
}
