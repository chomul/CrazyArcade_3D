#include "Framework/CA3DGameState.h"

#include "Net/UnrealNetwork.h"

void ACA3DGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACA3DGameState, Rules);
	DOREPLIFETIME(ACA3DGameState, AliveCount);
	DOREPLIFETIME(ACA3DGameState, MatchStartServerTime);
	DOREPLIFETIME(ACA3DGameState, bMatchEnded);
	DOREPLIFETIME(ACA3DGameState, bSuddenDeathActive);
}
