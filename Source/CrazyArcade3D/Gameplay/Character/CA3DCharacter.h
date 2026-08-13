#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CA3DCharacter.generated.h"

class AVoxelWorld;
class USpringArmComponent;
class UCameraComponent;
class UOcclusionFadeComponent;
class UStatusComponent;
class UCA3DRuleSet;
class ABomb;
class APredictedBombVisual;

// 플레이어와 봇이 완전히 같은 코드 경로를 타는 공용 캐릭터 (Task 10).
// 이동은 CMC 기본 리플리케이션에 맡긴다 — 커스텀 이동 코드 금지 (예측·보정·리플레이 무료 획득).
//
// 이동속도·점프 높이 등 파생 값은 전부 "셀 단위 계수(UCA3DRuleSet) × AVoxelWorld::CellSize" 로
// BeginPlay 에서 계산한다 — 셀 크기(⚠️ 임시 100, 튜닝 미확정)를 바꿔도 게임 감각이 유지된다.
UCLASS()
class CRAZYARCADE3D_API ACA3DCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACA3DCharacter();   // 캡슐 크기·JumpZVelocity·MaxWalkSpeed 초기값 (전부 튜닝 대상)

	// 서버 전용: 낙사 검사. GetActorLocation().Z < KillZ → Status->ServerKill(Fall).
	// 갇힌 채 구멍에 빠지는 상황은 막지 않는다 (미결정 정책 — Task 12 문서).
	virtual void Tick(float DeltaSeconds) override;

	// **관전 카메라 각의 출처** (2026-08-09).
	//
	// CameraBoom 이 bUsePawnControlRotation 이라 엔진(USpringArmComponent::UpdateDesiredArmLocation)
	// 이 매 프레임 이 함수를 읽어 카메라 회전을 정한다. 기본 구현(APawn::GetViewRotation)은
	// 컨트롤러의 ControlRotation 을 돌려주는데, 그 값이 옳은 것은 **내 폰**뿐이다:
	//   · 봇      → AAIController 가 계산한 "봇이 향한 방향". 스냅 배수가 아니고 매 프레임 변한다
	//   · 원격 폰 → 클라에서는 Controller 가 null 이라 폴백 각으로 떨어진다 (카메라 yaw 는
	//               컨트롤러 로컬 값이라 애초에 복제되지 않는다)
	// 그래서 남을 관전하면 격자가 비스듬히 눕고 카메라가 계속 미끄러진다.
	//
	// 규칙은 두 갈래다:
	//   ① **이 폰을 보고 있는 로컬 컨트롤러가 있으면 그 컨트롤러의 각.** 조종 중인 내 폰과
	//      관전 중인 남의 폰이 같은 가지다 — 그래야 관전 중에도 Q/E 회전이 먹는다
	//      (2026-08-09 사용자 요청)
	//   ② 아무도 안 보고 있으면 **복제된 스냅 인덱스**(ACA3DPlayerState::CamYawIndex).
	//      관전을 시작하는 순간 관전자가 이 값을 **시작각으로 받아 간다**
	virtual FRotator GetViewRotation() const override;

	// 스탯·생존 상태의 단일 출처 (Task 12) — 봇·플레이어 공용.
	UStatusComponent* GetStatus() const;

	// 이동속도 재계산의 단일 경로 — Trapped 면 룰셋 TrappedMoveSpeed, 아니면
	// 기본 속도(계수 × CellSize) × Status->MoveSpeedMul. 서버(Server* 직후)와
	// 클라(OnRep) 양쪽이 이 함수 하나만 탄다 (중복 공식 금지).
	void RefreshMoveSpeed();

	// 캐릭터의 "발밑 셀" — 폭발 피격·폭탄 설치 위치의 기준 (GDD 2.3 "발판만이 안전하다").
	// ⚠️ 공중에 있을 때의 정의는 미결정 (설계서 3.2) — 1차 구현은
	// WorldToCell(ActorLocation - FVector(0,0,CapsuleHalfHeight)) 로 하되 튜닝에서 확정.
	// (이 정의로는 점프 정점에서 한 칸 위 셀이 나온다.)
	FIntVector GetFootCell() const;

	// 이동·점프 입력 바인딩 대상 (호출은 Task 11 의 컨트롤러·이후 봇 AIController 가).
	// WorldAxis 는 "월드 평면 방향"(X=월드 +X, Y=월드 +Y) — 입력 기준(월드축/카메라)
	// 변환은 컨트롤러 소관. 캐릭터는 카메라를 모른다.
	// 사망(Dead) 중에는 둘 다 무시한다 — 차단 지점이 캐릭터인 이유는 각 함수 주석 참조.
	void Move(const FVector2D& WorldAxis);
	void DoJump();

	// ─── 사망 처리 (Task 27) ───

	// 사망 시 캐릭터에 적용할 것을 전부 모은 단일 지점 — 서버(UStatusComponent::ServerKill)와
	// 클라(OnRep_Life)가 **같은 이 함수**를 통과한다 (불변식 1의 정신: 경로가 갈리면 어긋난다).
	// 폰은 파괴하지도 풀에 반납하지도 않는다 (CLAUDE.md: 서버 권한 상태 액터 풀링 금지) —
	// 상태만 바꾸므로 부활은 "여기서 바꾼 것을 되돌리기" 하나로 끝난다.
	void ApplyDeathState();

	// ─── 니들 사용 (Task 23) ───

	// 니들 수동 사용의 **단일 진입점**. 자동 사용이 아니다 (2026-08-02 사용자 확정) —
	// 갇힌 순간 자동으로 터지면 "니들을 아껴 두었다가 결정적인 순간에 쓴다" 는 선택이 사라진다.
	// 플레이어(컨트롤러 입력)와 봇(AIController)이 **같은 이 함수**를 부른다:
	// 서버 권한이면 곧장 ServerEscape, 원격 클라면 RPC 경유 — 규칙은 ServerEscape 한 곳에만 있다.
	void TryUseNeedle();

	// 클라→서버: 니들 사용 요청. 최종 판정(Trapped + 니들 보유)은 ServerEscape 단독.
	UFUNCTION(Server, Reliable)
	void ServerUseNeedle();

	// ─── 폭탄 설치 (Task 16 — 데이터 흐름 3.1) ───

	// 설치 요청 셀 계산 (서버·클라 공용 — 컨트롤러는 입력만, 셀 계산은 캐릭터 소관).
	// ⚠️ 공중 설치 규칙 (잠정 확정 — 사용자 결정): 발밑 셀부터 -Z 로 스캔해
	// 첫 솔리드 블록 **바로 위의 Empty 셀**. 지상에 서 있으면 그냥 발밑 셀.
	// 스캔이 그리드 아래 밖까지 내려가면(발판 없음) false — 설치 거부.
	bool TryGetBombPlacementCell(FIntVector& OutCell) const;

	// 설치 입력의 단일 진입점 (컨트롤러·이후 봇이 호출 — Task 17, 데이터 흐름 3.1).
	// 셀 계산 → 리슨 호스트(권한 있음)는 지연이 없어 예측 생략·바로 서버 경로 →
	// 원격 클라는 로컬 검증 통과 시 예측 비주얼 획득 후 ServerPlaceBomb.
	// 로컬 검증 실패는 서버도 거부할 요청 — RPC 없이 조용히 반환 (서버 왕복 절약).
	void TryPlaceBombPredicted();

	// 클라: Cell 일치하는 예측 비주얼을 풀에 반납하고 목록에서 제거.
	// 호출처는 서버 확정(ABomb::BeginPlay — 진짜 폭탄으로 교체)과 거부(ClientRejectBomb) 두 곳.
	//
	// **반환값 = 실제로 회수한 것이 있는가.** ABomb::BeginPlay 가 설치음 중복을 피하는 데 쓴다 —
	// 회수했다는 것은 이 클라가 예측 시점에 이미 설치음을 냈다는 뜻이다 (거기 주석 참조).
	bool ReleasePredictedVisualAt(const FIntVector& Cell);

	// 클라→서버: 셀에 폭탄 설치 요청. 서버가 권위 검증(개수·셀 Empty·기존 폭탄·Alive) 후
	// ABomb 스폰 + ServerArm. 실패 시 ClientRejectBomb.
	UFUNCTION(Server, Reliable)
	void ServerPlaceBomb(FIntVector Cell);

	// 서버→해당 클라: 설치 거부 통보 — 같은 셀의 예측 비주얼(APredictedBombVisual)만 반납.
	// 예측에 상태가 없으므로 이펙트만 지우면 끝 (불변식 3).
	UFUNCTION(Client, Reliable)
	void ClientRejectBomb(FIntVector Cell);

protected:
	virtual void BeginPlay() override;

	// 지상 ↔ 공중 전환 시 이동 상한을 다시 잡는다 (공중은 지상 속도 × JumpAirSpeedFactor).
	// 도약 순간의 관성도 여기서 상한으로 깎는다 — CMC 는 이미 상한을 넘은 속도를 스스로
	// 낮추지 않기 때문에, 안 깎으면 전력 질주 점프가 지상 속도 그대로 날아간다.
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override; // 남은 예측 비주얼 반납

	// BeginPlay 에서 탐색·캐시 (좌표 변환·CellSize 의 유일한 출처).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// 상태 컴포넌트 (Task 12) — 생성자에서 부착.
	UPROPERTY(VisibleAnywhere, Category="Status")
	TObjectPtr<UStatusComponent> Status;

	// ⚠️ 임시 — 맵 최하층 아래. 룰셋 이동 여부는 튜닝 때 결정 (Task 10 문서).
	UPROPERTY(EditDefaultsOnly, Category="Character")
	float KillZ = -500.f;

	// 서버가 스폰할 폭탄 클래스 — BP_Bomb(메시·이펙트 지정용) 연결. 기본은 C++ ABomb.
	UPROPERTY(EditDefaultsOnly, Category="Bomb")
	TSubclassOf<ABomb> BombClass;

	// 카메라 붐 — 90도 스냅 카메라(Task 11)가 컨트롤러 ControlRotation 으로 회전시킨다
	// (bUsePawnControlRotation). 데디 서버는 BeginPlay 에서 파괴 (불변식 5 — 시각 전용).
	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	// 카메라~캐릭터 사이 블록 디더 페이드 (2026-08-06). 클라 시각 전용 — 판정과 무관하다.
	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<UOcclusionFadeComponent> OcclusionFade;

private:
	// CMC 튜닝 값 적용 — 서버·클라가 같은 룰셋 × 같은 CellSize 로 같은 값을 계산해야
	// CMC 예측이 안 어긋난다. 클라는 BeginPlay 시점에 GameState/Rules 미도착 가능 →
	// next-tick 재시도 (VoxelWorld.cpp 의 InitGridFromSeed 와 동일 관례).
	void TryApplyMovementTuning();

	bool bMovementTuningApplied = false;

	// TryApplyMovementTuning 이 캐시 — RefreshMoveSpeed 가 서버·클라 동일 값으로 재계산하는 재료.
	UPROPERTY()
	TObjectPtr<const UCA3DRuleSet> CachedRules;

	// 기본 이동속도 = MoveSpeedCellsPerSec × CellSize. 초기값은 생성자 임시값과 동일 근거.
	float BaseWalkSpeed = 400.f;

	// 서버 응답 대기 중인 예측 비주얼 목록 (원격 클라 로컬 전용 — 복제 안 함). 매칭은 Cell 기준.
	UPROPERTY()
	TArray<TObjectPtr<APredictedBombVisual>> PredictedBombVisuals;

	// 원격 클라: 로컬 검증(Alive·셀 Empty·같은 셀 예측 없음·개수 예측치) + 예측 비주얼 획득.
	// false 반환 = 검증 실패 — 호출자(TryPlaceBombPredicted)가 RPC 를 보내지 않는다.
	bool TryAcquirePredictedVisual(const FIntVector& Cell);

	// ─── 폭탄 킥 (2026-08-06 사용자 확정: 방향키로 밀기 — 별도 키 없음) ───
	//
	// 서버: 지금 밀고 있는 방향 앞 칸에 폭탄이 있으면 차 보낸다. Tick 이 매 틱 부른다 —
	// 발동에 전용 입력이 없으므로 "걸어 들어가는 것" 자체를 매 틱 관찰하는 수밖에 없다.
	// WorldInputDirection 은 월드 평면 이동 의도(크기는 무시, 방향만 본다).
	//
	// 인자로 받는 이유: 입력의 출처(CMC 가속도)를 호출부에 두면 봇·자동화 테스트가
	// CMC 를 돌리지 않고도 같은 규칙을 검증할 수 있다.
	void ServerTryKickBomb(const FVector& WorldInputDirection);

public:
	// ─── 접촉 판정의 단일 출처 (2026-08-06 킥 / 2026-08-10 갇힌 상대 터뜨리기) ───
	//
	// 사거리 = **내 형상 + 상대 형상 + 룰셋 여유**(BombKickReachToleranceCells × CellSize).
	// 이 프로젝트에서 "걸어 들어가면 닿았다" 는 전부 이 한 공식이다 — 공식이 두 벌이 되면
	// "차지는 거리" 와 "터지는 거리" 가 조용히 갈라진다.
	//   · 킥       : GetContactReach(캡슐 반지름, 폭탄 막힘 박스 반경)
	//   · 터뜨리기 : GetContactReach(캡슐 반지름, 상대 캡슐 반지름)   ← 수평
	//                GetContactReach(캡슐 반높이, 상대 캡슐 반높이)   ← 수직
	// 형상을 인자로 받는 이유: 상대가 폭탄(박스)일 수도, 캐릭터(캡슐)일 수도 있고,
	// 같은 상대라도 수평·수직에서 재는 치수가 다르기 때문이다.
	float GetContactReach(float SelfExtent, float TargetExtent) const;

private:
	// ─── 갇힌 상대 터뜨리기 (2026-08-10 사용자 확정 — 원작 크레이지 아케이드 규칙) ───
	//
	// **검사 주체가 "갇힌 쪽" 인 것이 이 함수의 요점이다.** 터뜨리는 쪽에 두면 살아 있는
	// 모두가 매 틱 "갇힌 사람 있나" 를 뒤져야 하지만(8인 × 매 틱 전수 비교), 갇힌 쪽에 두면
	// 최상단의 `LifeState != Trapped` 한 줄에서 되돌아간다 — **갇힌 사람이 없으면 이 기능의
	// 총비용은 캐릭터당 enum 비교 한 번**이고, 그것이 기본 상태다.
	//
	// 판정은 순수 거리다(입력 방향을 보지 않는다): 봇이 지나가다 닿아도 사람이 밀어도 같은
	// 결과여야 하고, 캐릭터끼리는 캡슐이 서로 Block 이라 "닿았다" 가 곧 접촉 사거리 안이다.
	void ServerTryPopIfTouched();

	// ─── 선택 캐릭터 외형 (Task 37) ───

	// GameState->Rules->Characters 를 PlayerState->CharacterIndex 로 인덱싱해 GetMesh() 에
	// 메시·AnimClass·트랜스폼 보정을 적용한다. **순수 시각** — 컬리전은 캡슐 담당, 메시는
	// 어떤 판정에도 쓰이지 않는다 (최상단 데디 가드, 함수 본문 주석 참조).
	// Tick 이 매 틱 부르되 아래 스냅샷 비교로 실제 적용은 인덱스가 바뀔 때 1회다.
	void ApplyCharacterAppearance();

	// 적용 완료 스냅샷 — "같은 인덱스 재적용" 을 거르는 기준 (HUD 의 스냅샷 비교 관례).
	// 범위 밖·Mesh 미지정으로 스킵한 인덱스도 기록한다 — 룰셋 에셋은 매치 중 안 바뀌므로
	// 재시도해도 결과가 같고, 기록해야 Verbose 로그가 1회로 끝난다.
	int32 AppliedCharacterIndex = INDEX_NONE;

	// ─── 관전 카메라 각 (2026-08-09) ───

	// 지금 이 폰을 보고 있는 **로컬** 플레이어 컨트롤러 (없으면 nullptr).
	// 블렌드 중에는 이전 대상과 새 대상이 둘 다 카메라 계산을 타므로 `PendingViewTarget` 도 본다 —
	// 한쪽만 인정하면 전환 0.4초 동안 한 화면에 각이 두 개가 되어 카메라가 휘청인다.
	//
	// **보간 상태를 폰이 들고 있지 않는다.** 각의 주인이 "보는 사람" 으로 정리되면서
	// (관전자가 Q/E 로 직접 돌린다) 폰이 따로 보간할 이유가 없어졌다 — 관전자의 컨트롤러가
	// 이미 자기 SmoothCamYaw 를 굴리고 있고, 그것 하나만 남는 편이 각의 출처가 명확하다.
	const APlayerController* GetLocalViewingController() const;

	// 복제된 스냅 인덱스를 각도로. 아무도 로컬에서 보고 있지 않은 폰의 "표현" 이자,
	// 관전을 시작할 때 관전자가 받아 가는 **시작각**이다.
	float GetReplicatedCamYawDeg() const;

	friend class FCA3DCharacterTest;    // 자동화 테스트가 튜닝 재적용·발밑 셀 검증을 위한 접근
	friend class FStatusComponentTest;  // 상태 전이·속도 재계산 검증을 위한 접근 (Task 12)
	friend class FBombTest;             // 설치 검증·공중 스캔·연쇄 검증을 위한 접근 (Task 16)
	friend class FPredictedBombVisualTest; // 로컬 검증·예측 목록 검증을 위한 접근 (Task 17)
	friend class FDeathHandlingTest;    // 사망 상태 적용·입력 차단 검증을 위한 접근 (Task 27)
	friend class FBotControllerTest;    // 봇이 조종하는 캐릭터의 튜닝 수동 적용을 위한 접근 (Task 20)
	friend class FItemPickupTest;       // 아이템 획득·니들 사용 검증의 튜닝 수동 적용을 위한 접근 (Task 23)
	friend class FBombKickTest;         // 킥 발동 조건 검증을 위한 접근 (방향키 밀기)
	friend class FSpectateTest;         // 관전 카메라 각 보간을 Tick 부작용 없이 돌리기 위한 접근
	friend class FTrappedPopTest;       // 갇힌 상대 터뜨리기 판정을 Tick 부작용 없이 돌리기 위한 접근
	friend class FBotPopTrappedTest;    // 봇이 갇힌 적을 노리는 경로 검증의 튜닝 수동 적용을 위한 접근
	friend class FBotSeekItemTest;      // 봇이 아이템을 노리는 경로 검증의 튜닝 수동 적용을 위한 접근
	friend class FCharacterAppearanceTest; // 외형 적용·스냅샷 비교 검증을 위한 접근 (Task 37)
};
