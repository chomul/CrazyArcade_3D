#include "Framework/CA3DGameState.h"

#include "Net/UnrealNetwork.h"

void ACA3DGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACA3DGameState, Rules);
	DOREPLIFETIME(ACA3DGameState, AliveCount);
	DOREPLIFETIME(ACA3DGameState, MatchStartServerTime);
	DOREPLIFETIME(ACA3DGameState, bMatchEnded);
	DOREPLIFETIME(ACA3DGameState, MatchWinner); // bMatchEnded 와 같은 액터 = 같은 번들로 원자 도착 (헤더 주석)
	DOREPLIFETIME(ACA3DGameState, bSuddenDeathActive);
	DOREPLIFETIME(ACA3DGameState, bLobbyActive); // 로비 페이즈 (Task 41) — 시간 제한이 없어 짝이 되는 시각 필드가 없다
	DOREPLIFETIME(ACA3DGameState, bCharacterSelectActive);
	DOREPLIFETIME(ACA3DGameState, CharacterSelectEndServerTime); // 플래그와 같은 액터 = 같은 번들로 원자 도착
}
