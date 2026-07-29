#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bomb.generated.h"

class ACA3DCharacter;
class AVoxelWorld;
class UStaticMeshComponent;

// 서버 권한 폭탄 (Task 16). bReplicates = true — 클라에는 액터 리플리케이션으로 존재만 복제.
// 타이머·판정·연쇄는 100% 서버 소유 (불변식 3의 서버 쪽 절반) — 클라 예측 표시는
// APredictedBombVisual(Task 17)이 담당하고 서버 거부 시 이펙트만 지운다.
//
// **풀링 금지** (설계서 2.8) — 퓨즈 타이머·bDetonated·소유자 등 서버 권한 상태를 가진
// 액터는 재사용 시 상태 오염 위험이 이득보다 크다. 스폰/Destroy 로만 수명 관리.
UCLASS()
class CRAZYARCADE3D_API ABomb : public AActor
{
	GENERATED_BODY()

public:
	ABomb();

	// 서버: 스폰 직후 호출. 소유자·범위·셀 기록 + ActiveBombCount 점유 + 서브시스템 등록 +
	// Rules->BombFuseTime 타이머 시작. Cell·Range 를 첫 복제 전에 확정해야 하므로
	// SpawnActorDeferred → ServerArm → FinishSpawning 순서로 부른다 (ACA3DCharacter::ServerPlaceBomb).
	void ServerArm(ACA3DCharacter* InOwner, int32 InRange, const FIntVector& InCell);

	FIntVector GetCell() const { return Cell; }
	int32      GetRange() const { return Range; }
	bool       IsDetonated() const { return bDetonated; }
	ACA3DCharacter* GetOwnerChar() const { return OwnerChar; }

	// 서버: 폭발의 단일 진입점 — 남은 퓨즈를 무시하고 연쇄 큐(ExplosionSubsystem)에 합류.
	// bDetonated 플래그로 중복 폭발 방지 (같은 폭탄이 두 연쇄 단계에 들어갈 수 없다).
	// 퓨즈 만료(OnFuseExpired)도 이 함수를 탄다 — bDetonated 기록 경로를 하나로 유지.
	void ServerForceDetonate();

	// 서버: 실제 폭발 처리 시(ProcessChainStep) 소유자 슬롯 반환 + 서브시스템 등록 해제.
	// EndPlay 가 안전망으로 한 번 더 부른다 — bSlotReturned 로 정확히 1회 보장.
	void ServerReleaseSlot();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;   // 클라(+리슨 호스트): 위험 프리뷰 데칼 표시
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 설치 셀 (그리드 판정용). 클라 프리뷰 계산에 필요해 복제.
	UPROPERTY(Replicated)
	FIntVector Cell = FIntVector::ZeroValue;

	// 폭발 전파 범위(칸). 클라 프리뷰 계산에 필요해 복제.
	UPROPERTY(Replicated)
	int32 Range = 1;

	// BP_Bomb 에서 메시 에셋만 지정 — BP 로직 금지. 데디 서버는 BeginPlay 에서 파괴 (불변식 5).
	UPROPERTY(VisibleAnywhere, Category="Bomb")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

private:
	// 서버 전용 — 폭발 시 ActiveBombCount 반환 대상 (사망/파괴 시 null 가드).
	UPROPERTY()
	TObjectPtr<ACA3DCharacter> OwnerChar;

	// 프리뷰용 좌표 변환·그리드 접근 (클라·리슨). lazy 탐색·캐시.
	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	// 클라 시각 — 풀에서 획득한 위험 프리뷰 데칼. 폭발/파괴(EndPlay) 시 반납.
	UPROPERTY()
	TArray<TObjectPtr<AActor>> PreviewDecals;

	FTimerHandle FuseTimer;      // 서버 전용 — 클라는 타이머를 모른다 (불변식 3)
	bool bDetonated = false;     // 연쇄 중복 폭발 방지
	bool bSlotReturned = false;  // ActiveBombCount 반환 1회 보장

	void OnFuseExpired();        // 서버: ServerForceDetonate → ExplosionSubsystem::RequestDetonate

	// 클라(+리슨): 위험 프리뷰 데칼 — 실폭발과 **같은** Propagate 를 호출한다 (별도 계산 금지 —
	// 설계서 5장 9번 "표시와 실제가 구조적으로 일치"). 그리드/룰셋 미도착 시 next-tick 재시도.
	void TryShowDangerPreview();
	void ReleasePreviewDecals();

	friend class FBombTest; // 자동화 테스트가 퓨즈 타이머·플래그 검증을 위한 접근
};
