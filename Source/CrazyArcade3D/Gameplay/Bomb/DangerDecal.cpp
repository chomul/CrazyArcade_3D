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

	// DecalSize 는 반크기 박스 (X = 투영 깊이). 바닥 면적을 정확히 셀 크기로 잡으면
	// 이웃 블록 옆면이 박스 경계 평면과 정확히 겹쳐 픽셀 단위로 칠해졌다 빠졌다 한다
	// (Z-파이팅성 검붉은 얼룩) — 면적은 살짝 안쪽(0.96배)으로 줄이고, 깊이는 위아래
	// 3/4칸으로 잡아 대상 면(그 칸 발판 윗면, 중심에서 반 칸 아래)만 여유 있게 덮는다.
	DecalComponent->DecalSize = FVector(CellSize * 0.75f, CellSize * 0.48f, CellSize * 0.48f);

	// DecalSize 는 세터가 없는 프로퍼티 — 렌더 프록시는 생성/이동/재생성 시점의 값을 트랜스폼에
	// 구워 쓴다 (GetTransformIncludingDecalSize). 신규 스폰 직후엔 프록시가 이미 엔진 기본값
	// (128,256,256 = 약 5×5칸)으로 만들어져 있어, 명시적으로 재생성하지 않으면 거대 데칼이 남는다
	// (풀 재사용 경로는 Acquire 의 이동·숨김 해제가 우연히 갱신해 줘서 정상으로 보였다).
	DecalComponent->MarkRenderStateDirty();
}

void ADangerDecal::OnAcquiredFromPool()
{
	if (DecalComponent)
	{
		DecalComponent->SetVisibility(true);
	}

	if (!bWarnedMissingMaterial && DecalComponent && !DecalComponent->GetDecalMaterial())
	{
		bWarnedMissingMaterial = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("ADangerDecal: 데칼 머티리얼 미지정 — BP_DangerDecal 을 만들어 머티리얼을 지정하고 룰셋 DangerDecalClass 에 연결할 것 (미지정이어도 판정은 정상)"));
	}
}

void ADangerDecal::OnReleasedToPool()
{
	// 풀은 반납 액터를 옮기지 않고 SetActorHiddenInGame 으로만 숨기는데, UDecalComponent 는
	// 액터 단위 HiddenInGame 전파를 안정적으로 따르지 않는다 (렌더 프록시가 컴포넌트
	// 가시성 기준) — 반납 후 옛 셀 위치에 투영이 남아 붉은 얼룩이 되므로 명시적으로 끈다.
	if (DecalComponent)
	{
		DecalComponent->SetVisibility(false);
	}
}
