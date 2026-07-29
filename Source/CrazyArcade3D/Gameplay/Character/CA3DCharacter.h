#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CA3DCharacter.generated.h"

class AVoxelWorld;
class USpringArmComponent;
class UCameraComponent;
class UStatusComponent;
class UCA3DRuleSet;
class ABomb;

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
	void Move(const FVector2D& WorldAxis);
	void DoJump();

	// ─── 폭탄 설치 (Task 16 — 데이터 흐름 3.1) ───

	// 설치 요청 셀 계산 (서버·클라 공용 — 컨트롤러는 입력만, 셀 계산은 캐릭터 소관).
	// ⚠️ 공중 설치 규칙 (잠정 확정 — 사용자 결정): 발밑 셀부터 -Z 로 스캔해
	// 첫 솔리드 블록 **바로 위의 Empty 셀**. 지상에 서 있으면 그냥 발밑 셀.
	// 스캔이 그리드 아래 밖까지 내려가면(발판 없음) false — 설치 거부.
	bool TryGetBombPlacementCell(FIntVector& OutCell) const;

	// 클라→서버: 셀에 폭탄 설치 요청. 서버가 권위 검증(개수·셀 Empty·기존 폭탄·Alive) 후
	// ABomb 스폰 + ServerArm. 실패 시 ClientRejectBomb.
	UFUNCTION(Server, Reliable)
	void ServerPlaceBomb(FIntVector Cell);

	// 서버→해당 클라: 설치 거부 통보 — 예측 비주얼(APredictedBombVisual) 제거용.
	// 핸들러 본문은 Task 17 (현재 로그만).
	UFUNCTION(Client, Reliable)
	void ClientRejectBomb(FIntVector Cell);

protected:
	virtual void BeginPlay() override;

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

	// 카메라 붐 — 45도 스냅 카메라(Task 11)가 컨트롤러 ControlRotation 으로 회전시킨다
	// (bUsePawnControlRotation). 데디 서버는 BeginPlay 에서 파괴 (불변식 5 — 시각 전용).
	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category="Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

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

	friend class FCA3DCharacterTest;    // 자동화 테스트가 튜닝 재적용·발밑 셀 검증을 위한 접근
	friend class FStatusComponentTest;  // 상태 전이·속도 재계산 검증을 위한 접근 (Task 12)
	friend class FBombTest;             // 설치 검증·공중 스캔·연쇄 검증을 위한 접근 (Task 16)
};
