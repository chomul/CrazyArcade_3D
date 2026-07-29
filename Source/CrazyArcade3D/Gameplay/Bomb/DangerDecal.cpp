#include "Gameplay/Bomb/DangerDecal.h"

#include "CrazyArcade3D.h"
#include "Components/DecalComponent.h"

ADangerDecal::ADangerDecal()
{
	PrimaryActorTick.bCanEverTick = false;

	// 클라 로컬 시각 전용 — 복제하지 않는다 (각 클라가 Propagate 로 스스로 계산 — 권한 매트릭스 "전송 없음").
	bReplicates = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	DecalComponent = CreateDefaultSubobject<UDecalComponent>(TEXT("Decal"));
	DecalComponent->SetupAttachment(RootComponent);
	// 데칼은 로컬 +X 방향으로 투영된다 — pitch -90 으로 아래(-Z)를 향하게 해
	// 셀 중심에서 그 칸 발판 윗면에 찍히게 한다.
	DecalComponent->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
}

void ADangerDecal::SetCellSize(float CellSize)
{
	if (!DecalComponent) return;

	// DecalSize 는 반크기 박스 (X = 투영 깊이). 깊이는 셀 1칸(위아래 반 칸씩 여유),
	// 바닥 면적은 셀 한 칸에 맞춘다.
	DecalComponent->DecalSize = FVector(CellSize, CellSize * 0.5f, CellSize * 0.5f);
}

void ADangerDecal::OnAcquiredFromPool()
{
	if (!bWarnedMissingMaterial && DecalComponent && !DecalComponent->GetDecalMaterial())
	{
		bWarnedMissingMaterial = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("ADangerDecal: 데칼 머티리얼 미지정 — BP_DangerDecal 을 만들어 머티리얼을 지정하고 룰셋 DangerDecalClass 에 연결할 것 (미지정이어도 판정은 정상)"));
	}
}

void ADangerDecal::OnReleasedToPool()
{
	// 타이머·FX 없음 — 정리할 것 없음 (계약상 명시적 구현만 둔다).
}
