#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Gameplay/Item/ItemTypes.h"
#include "ItemPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UStaticMesh;

// 맵에 놓인 아이템 1개 (Task 23). 서버 권한 액터 — bReplicates = true.
//
// 스폰 흐름 (크아식 — GDD 3장): 맵 생성기가 Destructible 블록 "안"에 FItemPlacement 로
// 예약해 두고, 그 블록이 부서지는 순간 서버(UExplosionSubsystem::ProcessChainStep)가
// 이 액터를 노출 스폰한다. 풀링하지 않는다 — 서버 권한 상태를 가진 액터라 재사용 시
// 상태 오염 위험이 이득보다 크다 (CLAUDE.md). 수명은 스폰/Destroy 로만.
//
// 사라지는 경로가 둘이고 **의미가 다르다**:
//   · 획득(OnOverlap) — 효과를 적용하고 사라진다
//   · 소멸(ServerBurn) — 효과 없이 사라진다 ("적이 못 먹게 태우기" 심리전, GDD 3장)
// 로그로 구분되게 경로를 분리해 둔다.
//
// ⚠️ 컴포넌트가 둘인 이유 (2026-08-02 에 크게 데인 함정):
//   · PickupSphere  = 획득 판정용 컬리전. **데디 서버에서도 살아 있어야 한다.**
//   · MeshComponent = 표시 전용. 데디 가드로 꺼도 되는 건 이쪽뿐이다.
// AVoxelWorld 가 HISM 을 "시각 전용"으로 보고 데디에서 파괴했다가 **서버에 지형 컬리전이
// 사라져 캐릭터가 지형을 통과**했다. 컬리전을 메시에 얹으면 같은 방식으로 데디에서만
// 아이템을 못 줍게 되고, PIE 로는 재현되지 않는다(에디터는 IsRunningDedicatedServer()==false).
UCLASS()
class CRAZYARCADE3D_API AItemPickup : public AActor
{
	GENERATED_BODY()

public:
	AItemPickup();

	// 서버: 스폰 직후 종류·셀 지정 + 레지스트리 등록.
	// 첫 복제 전에 값이 확정돼야 클라 OnRep_Type 이 올바른 메시를 고른다 —
	// 호출부는 SpawnActorDeferred → ServerInit → FinishSpawning 순서 (ABomb::ServerArm 관례).
	void ServerInit(EItemType InType, const FIntVector& InCell);

	FIntVector GetCell() const { return Cell; }
	EItemType  GetItemType() const { return Type; }

	// 서버: 물줄기에 닿아 소멸. 획득과 달리 **효과 없이** 사라진다 — 심리전 요소 (GDD 3장).
	void ServerBurn();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override; // 메시 제자리 회전만 — 데디에서는 틱 자체를 끈다

	UPROPERTY(ReplicatedUsing=OnRep_Type)
	EItemType Type = EItemType::Balloon;

	UPROPERTY(Replicated)
	FIntVector Cell = FIntVector::ZeroValue;

	// 서버: 캐릭터 오버랩 → Alive 확인 → StatusComponent->ServerApplyItem(Type) → 제거.
	// **갇힌(Trapped) 상태에서는 못 줍는다** (2026-08-02 사용자 확정) — 물방울에 갇힌 채
	// 강화 아이템을 먹으면 갇힘이 위험이 아니라 이득이 된다.
	UFUNCTION()
	void OnOverlap(AActor* OverlappedActor, AActor* OtherActor);

	// 클라: 종류 복제 도착 → 종류별 메시 반영 (데디 가드는 RefreshVisual 안).
	UFUNCTION()
	void OnRep_Type();

	// 획득 판정 볼륨. **데디 서버에서도 파괴하지 않는다** (클래스 주석 참조).
	UPROPERTY(VisibleAnywhere, Category="Item")
	TObjectPtr<USphereComponent> PickupSphere;

	// 표시 전용 — 데디 서버는 BeginPlay 에서 파괴 (불변식 5).
	// 제자리 회전은 이 컴포넌트에만 건다 (Gameplay/SpinVisual.h) — PickupSphere 는 안 돈다
	// (구체라 돌려봐야 판정은 같고, 판정 컴포넌트를 돌리는 관례를 만들면 폭탄에서 사고가 난다).
	UPROPERTY(VisibleAnywhere, Category="Item")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// 종류별 메시 — BP 서브클래스(BP_ItemPickup) **하나**에서 에셋만 지정한다.
	// 종류마다 BP 를 만들지 않는 이유: 클래스가 5개로 갈리면 룰셋도 5개를 알아야 하고
	// 종류가 늘 때마다 에디터 작업이 함께 는다 (UHISMVoxelRenderer::BlockMeshes 와 같은 관례).
	UPROPERTY(EditDefaultsOnly, Category="Item")
	TMap<EItemType, TObjectPtr<UStaticMesh>> ItemMeshes;

private:
	// 획득 판정 반지름·표시 크기를 셀 크기에서 파생 (매직 넘버 금지 — 캐릭터 CMC 튜닝과 같은 관례).
	// 서버·클라 공통 경로: 판정은 서버만 하지만 계산은 같은 함수 하나로 유지한다.
	void ApplyCellScale();

	// 종류별 메시 반영 — BeginPlay(클라)와 OnRep_Type 양쪽이 이 함수 하나만 탄다.
	void RefreshVisual();

	// 획득·소멸이 정확히 1회만 일어나게 하는 가드 — 같은 프레임에 오버랩과 물줄기가
	// 겹치면 ServerApplyItem 이 두 번 불릴 수 있다 (Destroy 는 프레임 말미에 반영된다).
	bool bConsumed = false;

	// 메시 미지정 경고 1회 — 매 스폰마다 찍히면 로그가 아이템으로 도배된다 (ADangerDecal 관례).
	static bool bWarnedMissingMesh;

	// BeginPlay 에서 룰셋 1회 조회 (틱마다 GameState 조회 방지). ABomb 과 같은 값을 쓴다.
	float SpinDegreesPerSecond = 0.f;

	friend class FItemPickupTest; // 자동화 테스트가 오버랩 판정을 직접 호출하기 위한 접근
};
