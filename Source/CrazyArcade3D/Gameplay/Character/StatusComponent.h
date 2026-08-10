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
//
// ⚠️ 원인은 **섞지 않는다.** 새 사인이 생기면 기존 값을 재활용하지 말고 항목을 추가한다 —
// 한 번 섞으면 집계에서 "익사" 와 "밀려서 터짐" 을 나중에 다시 가를 방법이 없다.
// (숫자에 의존하는 곳은 없다: 복제·직렬화 대상이 아니고 switch 도 없다. 유일한 소비처는
//  UEnum::GetValueAsString 로그와 UStatusComponent::LastDeathCause 다 — 2026-08-10 확인.)
UENUM()
enum class EDeathCause : uint8
{
	None,        // 아직 죽지 않았다 — LastDeathCause 의 초기값 전용. ServerKill 의 인자로 쓰지 않는다
	Water,       // 물방울 갇힘 시간 만료 (익사)
	Fall,        // 맵 밖 추락 (KillZ)
	SuddenDeath, // 서든데스 블록 낙하
	Popped,      // 갇힌(Trapped) 상태에서 갇히지 않은 상대에게 몸으로 밀려 터짐 (2026-08-10 확정)
	Left,        // 매치 도중 접속 종료 — 나간 그 자리에서 사망 처리해 순위를 준다 (ACA3DGameMode::Logout)
	// ⚠️ 새 값은 **맨 끝에 append** 한다. 중간에 끼우면 기존 값의 숫자가 밀려
	//    이미 남은 진단 로그·저장된 집계의 의미가 조용히 어긋난다 (EBotState 와 같은 규약).
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

	// 마지막 사망 원인 — **서버 전용, 비복제** (ActiveBombCount 와 같은 근거: 판정에만 쓰이고
	// 클라가 볼 이유가 없다). `LifeState == Dead` 이후에만 의미가 있다.
	// 사인이 어딘가에 남지 않으면 EDeathCause 를 나눠 둔 의미가 없다 — 통계·킬 피드(후속)와
	// 자동화 테스트가 "무엇에 죽었는가" 를 확인하는 유일한 출처다.
	EDeathCause LastDeathCause = EDeathCause::None;

	// ─── 서버 전용 진입점 (모두 최상단 권한 가드 — 불변식 5) ───

	// 아이템 적용 — 스탯 증가는 룰셋 Cap 으로 클램프. Dead 이후는 무시.
	void ServerApplyItem(EItemType Item);

	// "이 아이템을 지금 먹으면 **무엇이든 바뀌는가**" (2026-08-10 — 봇의 아이템 목표 선별용).
	//
	// ⚠️ 가치 판단이 아니다 — "범위가 급한가 속도가 급한가" 같은 것은 여기 들어오지 않는다
	// (튜닝 지옥의 입구다). 오직 **이미 상한이라 먹어도 아무 일도 안 일어나는가**만 본다.
	//
	// **ServerApplyItem 바로 옆에 두는 것이 이 함수의 전부다.** 클램프 조건이 두 파일로
	// 갈라지면 "봇은 상한인 줄 아는데 실제로는 아직 오르는" 어긋남이 조용히 생긴다 —
	// 아래 구현은 위 함수의 각 case 와 1:1 로 대응한다. 한쪽을 고치면 다른 쪽이 눈에 들어온다.
	bool HasRoomForItem(EItemType Item) const;

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

	// 사망 상태 적용 위임 (Task 27) — 서버 ServerKill 과 클라 OnRep_Life 가
	// ACA3DCharacter::ApplyDeathState 단일 경로를 타게 한다 (부활 시 되돌릴 항목이 한 곳에).
	void ApplyOwnerDeathState();

	// 갇힘·탈출·사망 큐 (사운드/이펙트). 서버 Server* 와 클라 OnRep_Life 가 **같은 이 함수**를
	// 지난다 — 서버는 OnRep 이 안 불리고 클라는 Server* 를 안 타므로 양쪽에서 각자 정확히 1회다.
	// (데디 서버는 CA3DFeedback::Play 최상단 가드에서 되돌아간다 — 여기에 가드를 두지 않는 이유는
	//  가드가 흩어지면 언젠가 하나가 빠지기 때문. CA3DFeedback.h 주석 참조.)
	//
	// **전이**를 봐야 하므로 직전 상태를 따로 들고 있는다: LifeState 만 보면 "Alive 로 바뀐 것"이
	// 니들 탈출인지 최초 스폰인지 구분되지 않고, OnRep 은 값이 아니라 도착 사실만 알려 준다.
	void PlayLifeStateCue();

	// 큐 판정용 직전 상태 — **복제하지 않는다** (머신마다 로컬 관찰값이면 충분하고,
	// 복제하면 같은 사실을 두 벌로 들고 있게 된다).
	ELifeState LastCuedLifeState = ELifeState::Alive;

	// 갇힘 만료 타이머 — 만료 시 ServerKill(Water). ServerEscape/ServerKill 이 해제.
	FTimerHandle TrappedTimer;

	friend class FStatusComponentTest; // 자동화 테스트가 타이머 가동 여부 검증을 위한 접근
	friend class FDeathHandlingTest;   // 갇힘 타이머 잔존 여부 검증을 위한 접근 (Task 27)
	friend class FItemPickupTest;      // 니들 탈출 시 타이머 해제 검증을 위한 접근 (Task 23)
	friend class FFeedbackTest;        // 클라 경로(OnRep_Life) 재진입 시 큐 중복 여부 검증을 위한 접근
	friend class FTrappedPopTest;      // 터뜨려 죽인 뒤 갇힘 타이머 잔존 여부 검증을 위한 접근 (2026-08-10)
};
