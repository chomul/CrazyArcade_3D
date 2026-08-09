#include "Gameplay/Character/StatusComponent.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/CA3DFeedback.h"
#include "Gameplay/Item/ItemTypes.h"
#include "Framework/CA3DRuleSet.h"     // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h"   // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Framework/CA3DGameMode.h"    // 사망 통지 대상 (서버 전용) — .cpp 에서만
#include "Framework/CA3DPlayerState.h" // 사망 통지 인자 — .cpp 에서만
#include "GameFramework/Pawn.h"
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

void UStatusComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetOwner()->HasAuthority()) return; // 불변식 5 — 초기 스탯은 서버가 정하고 복제로 내려간다

	// 초기 스탯은 룰셋에서 (GDD 7.5 — 폭발 범위도 데이터 노출, 코드 매직 넘버 금지).
	// Cap 과의 교차 오설정(초기값 > 상한)도 여기서 흡수한다.
	const UCA3DRuleSet* Rules = ResolveRules();
	MaxBombCount = FMath::Clamp(Rules->InitialBombCount, 1, Rules->MaxBombCountCap);
	BombRange    = FMath::Clamp(Rules->InitialBombRange, 1, Rules->MaxBombRangeCap);
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
	PlayLifeStateCue(); // 〃 (리슨 호스트는 OnRep 이 안 오므로 여기가 유일한 경로)

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
	PlayLifeStateCue();

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
	LastDeathCause = Cause; // 사인은 여기서 딱 한 번 굳는다 (위 Dead 가드가 덮어쓰기를 막는다)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TrappedTimer); // 갇힌 채 죽어도 타이머 잔존 금지
	}

	RefreshOwnerMoveSpeed(); // 경로 일관 — 이동 입력 차단 자체는 ACA3DCharacter::Move/DoJump 의 생존 가드

	// 사망 상태 적용 (Task 27) — 컬리전·이동 모드·메시. 클라는 OnRep_Life 가 같은 함수를 탄다.
	ApplyOwnerDeathState();
	PlayLifeStateCue();

	UE_LOG(LogCA3D, Log, TEXT("UStatusComponent %s: 사망 — 원인 %s"),
		*GetOwner()->GetName(), *UEnum::GetValueAsString(Cause));

	// ─ GameMode 통지 (Task 18) ─
	// 생존자 수 갱신·라스트맨 스탠딩 판정은 서버(GameMode)가 단독으로 한다. 여기서 등수를
	// 계산하지 않는 이유: 판정 주체가 여럿이면 같은 프레임에 여러 명이 죽었을 때 결과가 갈린다.
	// 이 지점은 함수 최상단 권한 가드를 이미 통과했으므로 서버 전용 경로다 (불변식 5).
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	ACA3DPlayerState* OwnerState = OwnerPawn ? OwnerPawn->GetPlayerState<ACA3DPlayerState>() : nullptr;
	if (!OwnerState)
	{
		// 봇은 Task 20 에서 ABotController 의 bWantsPlayerState 로 PlayerState 를 갖는다.
		// 그전까지(그리고 PlayerState 없는 테스트 폰)는 조용히 반환한다 — 사망 자체는 위에서 이미 반영됐다.
		return;
	}

	if (UWorld* World = GetWorld())
	{
		// 테스트 월드처럼 GameMode 가 ACA3DGameMode 가 아닐 수 있다 → null 가드.
		if (ACA3DGameMode* GameMode = World->GetAuthGameMode<ACA3DGameMode>())
		{
			GameMode->NotifyPlayerDeath(OwnerState);
		}
	}
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

	// 사망 상태 적용 — 서버 ServerKill 과 **같은 함수**를 탄다 (경로가 갈리면 어긋난다).
	// 시각/서버 전용 구분은 ApplyDeathState 안에서 각각 가드한다.
	// OnRep 은 클라에서만 불린다 — 서버(리슨 호스트 포함)는 ServerKill 에서 이미 통과했다.
	if (LifeState == ELifeState::Dead)
	{
		ApplyOwnerDeathState();
	}

	// 갇힘·탈출·사망 큐 — 서버 Server* 와 **같은 함수**를 탄다. OnRep 은 클라에서만 불리고
	// 서버(리슨 호스트 포함)는 Server* 에서 이미 지났으므로 양쪽이 각자 1회다.
	PlayLifeStateCue();

	if (IsRunningDedicatedServer()) return; // 불변식 5 — 이하 시각 전용

	// TODO(후속 Task): 갇힘 물방울 비주얼·사망 연출.
	// 관전 전환은 여기서 하지 않는다 — 폰은 그대로 두고 **보는 대상만** 바꾸는 방식이라
	// 시점 전환은 로컬 컨트롤러(ACA3DPlayerController::UpdateSpectateView)가 매 프레임
	// 스스로 판단한다. 상태 컴포넌트는 "죽었다" 는 사실만 복제하면 된다
	// (그래서 ELifeState::Spectating 은 지금도 쓰지 않는다 — 관전은 복제 상태가 아니다).
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

void UStatusComponent::PlayLifeStateCue()
{
	const ELifeState Previous = LastCuedLifeState;
	if (Previous == LifeState)
	{
		return; // 전이가 아니다 (같은 값이 다시 복제되거나 Server* 가 중복 호출된 경우)
	}
	LastCuedLifeState = LifeState;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const FVector Location = Owner->GetActorLocation();

	switch (LifeState)
	{
	case ELifeState::Trapped:
		CA3DFeedback::Play(GetWorld(), ECA3DCue::Trapped, Location);
		break;

	case ELifeState::Alive:
		// Trapped → Alive 는 니들 탈출뿐이다 (ServerEscape 가 유일한 경로). 최초 스폰은
		// LastCuedLifeState 초기값이 Alive 라 여기까지 오지 않는다.
		if (Previous == ELifeState::Trapped)
		{
			CA3DFeedback::Play(GetWorld(), ECA3DCue::Escape, Location);
		}
		break;

	case ELifeState::Dead:
		CA3DFeedback::Play(GetWorld(), ECA3DCue::Death, Location);
		break;

	default:
		break; // Spectating 은 지금 쓰지 않는다 (OnRep_Life 주석)
	}
}

void UStatusComponent::ApplyOwnerDeathState()
{
	// 소유자가 캐릭터가 아닐 수 있다(테스트 폰·후속 관전 전용 폰) — 그 경우 상태만 Dead 로 남는다.
	if (ACA3DCharacter* Character = Cast<ACA3DCharacter>(GetOwner()))
	{
		Character->ApplyDeathState();
	}
}
