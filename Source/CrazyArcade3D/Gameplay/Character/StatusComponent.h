#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "StatusComponent.generated.h"

class UCA3DRuleSet;
enum class EItemType : uint8;

// 생존 상태 (GDD 2.3). Trapped: 물방울에 갇힘 — 니들로만 탈출, 미세 이동만 가능.
UENUM()
enum class ELifeState : uint8
{
	Alive,
	Trapped,
	Dead,
	Spectating,
};

// 사망 원인 — 통계·킬 피드(후속 Task)용.
UENUM()
enum class EDeathCause : uint8
{
	Water,       // 물방울 갇힘 시간 만료 (익사)
	Fall,        // 맵 밖 추락 (KillZ)
	SuddenDeath, // 서든데스 블록 낙하
};

// 캐릭터에 부착되는 상태 컴포넌트 — 스탯·생존 상태의 단일 출처 (Task 12).
// 컴포넌트로 분리해 봇과 플레이어가 완전히 같은 코드 경로를 탄다 (설계서 2.5).
// 상태 변경은 전부 Server* 진입점으로만 — 최상단 권한 가드 (불변식 5).
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class CRAZYARCADE3D_API UStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStatusComponent();

	// ─── 아이템 스탯 (GDD 3장 — Replicated 변수 5개면 충분) ───

	// 동시 설치 가능한 폭탄 수. Balloon 아이템이 +1 (룰셋 MaxBombCountCap 클램프).
	UPROPERTY(ReplicatedUsing=OnRep_Stats)
	int32 MaxBombCount = 1;

	// 폭발 전파 범위(칸). Potion 아이템이 +1 (룰셋 MaxBombRangeCap 클램프).
	UPROPERTY(ReplicatedUsing=OnRep_Stats)
	int32 BombRange = 1;

	// 이동속도 배율 — 기본 속도(계수 × CellSize)에 곱한다. Roller 아이템이 증가.
	UPROPERTY(ReplicatedUsing=OnRep_Stats)
	float MoveSpeedMul = 1.f;

	// 니들 보유 (1회 소모품) — Trapped 탈출 전용.
	UPROPERTY(Replicated)
	bool bHasNeedle = false;

	// 폭탄 차기 보유 — 판정은 Task 15/16 의 폭탄 쪽이 읽는다.
	UPROPERTY(Replicated)
	bool bHasKick = false;

	// ─── 생존 상태 ───

	UPROPERTY(ReplicatedUsing=OnRep_Life)
	ELifeState LifeState = ELifeState::Alive;

	// 현재 설치되어 살아 있는 폭탄 수 — 서버 전용, 복제 불필요.
	// 설치 +1 / 폭발 -1 (Task 16 설치 검증이 MaxBombCount 와 비교).
	int32 ActiveBombCount = 0;

	// ─── 서버 전용 진입점 (모두 최상단 권한 가드 — 불변식 5) ───

	// 아이템 적용 — 스탯 증가는 룰셋 Cap 으로 클램프. Dead 이후는 무시.
	void ServerApplyItem(EItemType Item);

	// Alive → Trapped. 룰셋 TrappedDuration 타이머 시작 — 만료 시 ServerKill(Water).
	void ServerTrap();

	// Trapped + 니들 보유 → Alive. 니들 소모, 타이머 해제, 속도 복원.
	void ServerEscape();

	// → Dead. 타이머 정리 후 GameMode 에 사망 통지 (순위·생존자 수 판정은 GameMode 단독 — Task 18).
	void ServerKill(EDeathCause Cause);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	// 서버: 초기 스탯을 룰셋(InitialBombCount/InitialBombRange)에서 로드 — 복제로 클라에 내려간다.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 클라: 스탯 복제 도착 — 이동속도 재계산 (서버와 같은 단일 경로).
	UFUNCTION()
	void OnRep_Stats();

	// 클라: 생존 상태 복제 도착 — 속도 재계산 + 갇힘 비주얼·관전 전환 (시각부는 데디 가드).
	UFUNCTION()
	void OnRep_Life();

private:
	// 룰셋 해석 — GameState 의 복제 포인터, 없으면 CDO 폴백 (캐릭터 CMC 튜닝과 동일 관례).
	const UCA3DRuleSet* ResolveRules() const;

	// 소유 캐릭터의 이동속도 재계산 위임 — 서버 Server* 와 클라 OnRep 양쪽이
	// ACA3DCharacter::RefreshMoveSpeed 단일 경로를 타게 한다 (중복 공식 금지).
	void RefreshOwnerMoveSpeed();

	// 갇힘 만료 타이머 — 만료 시 ServerKill(Water). ServerEscape/ServerKill 이 해제.
	FTimerHandle TrappedTimer;

	friend class FStatusComponentTest; // 자동화 테스트가 타이머 가동 여부 검증을 위한 접근
};
