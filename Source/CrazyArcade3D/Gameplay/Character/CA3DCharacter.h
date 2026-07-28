#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CA3DCharacter.generated.h"

class AVoxelWorld;
class USpringArmComponent;
class UCameraComponent;

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

	// 서버 전용: 낙사 검사. GetActorLocation().Z < KillZ → 사망 처리(Task 12에서 연결).
	virtual void Tick(float DeltaSeconds) override;

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

protected:
	virtual void BeginPlay() override;

	// BeginPlay 에서 탐색·캐시 (좌표 변환·CellSize 의 유일한 출처).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	// ⚠️ 임시 — 맵 최하층 아래. 룰셋 이동 여부는 튜닝 때 결정 (Task 10 문서).
	UPROPERTY(EditDefaultsOnly, Category="Character")
	float KillZ = -500.f;

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

	// 낙사 로그 1회 제한 — 틱마다 스팸 방지. 사망 처리(Task 12) 연결 전 임시.
	bool bKillZLogged = false;

	friend class FCA3DCharacterTest; // 자동화 테스트가 튜닝 재적용·발밑 셀 검증을 위한 접근
};
