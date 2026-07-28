#include "Gameplay/Character/StatusComponent.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Item/ItemTypes.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UStatusComponent::UStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // 상태 변경은 전부 이벤트 진입점 — 틱 불필요
	SetIsReplicatedByDefault(true);
}

void UStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStatusComponent, MaxBombCount);
	DOREPLIFETIME(UStatusComponent, BombRange);
	DOREPLIFETIME(UStatusComponent, MoveSpeedMul);
	DOREPLIFETIME(UStatusComponent, bHasNeedle);
	DOREPLIFETIME(UStatusComponent, bHasKick);
	DOREPLIFETIME(UStatusComponent, LifeState);
	// ActiveBombCount 는 서버 전용 — 의도적으로 비복제 (설계서 2.5).
}

void UStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 파괴·레벨 전환 중 타이머가 남아 죽은 컴포넌트를 부르지 않게 정리.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TrappedTimer);
	}
	Super::EndPlay(EndPlayReason);
}

// ─── 서버 전용 진입점 ────────────────────────────────────────────────────────

void UStatusComponent::ServerApplyItem(EItemType Item)
{
	if (!GetOwner()->HasAuthority()) return; // 불변식 5

	if (LifeState == ELifeState::Dead || LifeState == ELifeState::Spectating)
	{
		return; // 사망 이후 스탯 변경 없음
	}

	const UCA3DRuleSet* Rules = ResolveRules();

	switch (Item)
	{
	case EItemType::Balloon:
		MaxBombCount = FMath::Min(MaxBombCount + 1, Rules->MaxBombCountCap);
		break;
	case EItemType::Potion:
		BombRange = FMath::Min(BombRange + 1, Rules->MaxBombRangeCap);
		break;
	case EItemType::Roller:
		MoveSpeedMul = FMath::Min(MoveSpeedMul + Rules->RollerSpeedStep, Rules->MoveSpeedMulCap);
		break;
	case EItemType::Needle:
		bHasNeedle = true;
		break;
	case EItemType::Kick:
		bHasKick = true;
		break;
	default:
		ensureMsgf(false, TEXT("UStatusComponent::ServerApplyItem: 미처리 아이템 %d"), static_cast<int32>(Item));
		break;
	}

	// 서버(리슨 호스트 포함)는 OnRep 이 불리지 않는다 — 클라와 같은 재계산 경로를 직접 태운다.
	RefreshOwnerMoveSpeed();

	UE_LOG(LogCA3D, Verbose, TEXT("UStatusComponent %s: 아이템 %s 적용 — Bomb %d / Range %d / SpeedMul %.2f / Needle %d / Kick %d"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(Item),
		MaxBombCount, BombRange, MoveSpeedMul, bHasNeedle, bHasKick);
}

void UStatusComponent::ServerTrap()
{
	if (!GetOwner()->HasAuthority()) return; // 불변식 5

	if (LifeState != ELifeState::Alive)
	{
		return; // Trapped 중복 갇힘·Dead 이후는 무시
	}

	LifeState = ELifeState::Trapped;

	// 만료 시 익사 — 그 전에 ServerEscape 가 타이머를 해제하면 살아남는다.
	const UCA3DRuleSet* Rules = ResolveRules();
	GetWorld()->GetTimerManager().SetTimer(TrappedTimer,
		FTimerDelegate::CreateUObject(this, &UStatusComponent::ServerKill, EDeathCause::Water),
		Rules->TrappedDuration, false);

	// 서버측 즉시 반영 (TrappedMoveSpeed) — 클라는 OnRep_Life 가 같은 경로를 탄다.
	RefreshOwnerMoveSpeed();

	UE_LOG(LogCA3D, Log, TEXT("UStatusComponent %s: 갇힘 — %.1f초 후 익사"),
		*GetOwner()->GetName(), Rules->TrappedDuration);
}

void UStatusComponent::ServerEscape()
{
	if (!GetOwner()->HasAuthority()) return; // 불변식 5

	if (LifeState != ELifeState::Trapped || !bHasNeedle)
	{
		return; // 갇힌 상태 + 니들 보유일 때만 탈출
	}

	bHasNeedle = false; // 1회 소모품
	LifeState = ELifeState::Alive;
	GetWorld()->GetTimerManager().ClearTimer(TrappedTimer);

	// 속도 복원 — 서버측 즉시, 클라는 OnRep 으로 같은 경로.
	RefreshOwnerMoveSpeed();

	UE_LOG(LogCA3D, Log, TEXT("UStatusComponent %s: 니들 탈출 — Alive 복귀"), *GetOwner()->GetName());
}

void UStatusComponent::ServerKill(EDeathCause Cause)
{
	if (!GetOwner()->HasAuthority()) return; // 불변식 5

	if (LifeState == ELifeState::Dead)
	{
		return; // 중복 사망 방지 (낙사 틱 검사 등에서 재호출될 수 있음)
	}

	LifeState = ELifeState::Dead;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TrappedTimer); // 갇힌 채 죽어도 타이머 잔존 금지
	}

	RefreshOwnerMoveSpeed(); // 경로 일관 — Dead 처리(입력 차단·관전)는 Task 18 에서

	UE_LOG(LogCA3D, Log, TEXT("UStatusComponent %s: 사망 — 원인 %s"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(Cause));
	// TODO(Task 18): GameMode 에 사망 통지 — 생존자 수 갱신·라스트맨 스탠딩 판정·관전 전환.
}

// ─── OnRep (클라) ────────────────────────────────────────────────────────────

void UStatusComponent::OnRep_Stats()
{
	// MoveSpeedMul 변경 반영 — 서버와 같은 ACA3DCharacter::RefreshMoveSpeed 단일 경로 (중복 공식 금지).
	RefreshOwnerMoveSpeed();
}

void UStatusComponent::OnRep_Life()
{
	// CMC 속도는 시뮬레이션 값 — 데디 가드 밖 (Trapped/Alive 속도 반영).
	RefreshOwnerMoveSpeed();

	if (IsRunningDedicatedServer()) return; // 불변식 5 — 이하 시각 전용

	// TODO(후속 Task): 갇힘 물방울 비주얼·사망 연출·관전 전환. 현재는 시각 에셋 없음 (로그만).
	UE_LOG(LogCA3D, Verbose, TEXT("UStatusComponent %s: LifeState 복제 도착 — %s"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(LifeState));
}

// ─── 내부 ────────────────────────────────────────────────────────────────────

const UCA3DRuleSet* UStatusComponent::ResolveRules() const
{
	// 캐릭터 CMC 튜닝(TryApplyMovementTuning)과 동일 관례 — GameState 의 복제 포인터, 없으면 CDO.
	// 서버 전용 경로에서만 쓰므로 next-tick 재시도는 불필요 (서버는 GameState 를 즉시 가진다).
	if (const UWorld* World = GetWorld())
	{
		if (const ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>())
		{
			if (GameState->Rules)
			{
				return GameState->Rules;
			}
		}
	}
	return GetDefault<UCA3DRuleSet>();
}

void UStatusComponent::RefreshOwnerMoveSpeed()
{
	if (ACA3DCharacter* Character = Cast<ACA3DCharacter>(GetOwner()))
	{
		Character->RefreshMoveSpeed();
	}
}
