#include "Gameplay/Bomb/PredictedBombVisual.h"

#include "CrazyArcade3D.h"
#include "Components/StaticMeshComponent.h"

APredictedBombVisual::APredictedBombVisual()
{
	PrimaryActorTick.bCanEverTick = false; // 로직 0 — 틱도 없다 (불변식 3)

	// 클라 로컬 시각 전용 — 복제하지 않는다 (설치자 클라에만 존재, 서버·타 클라는 모른다).
	bReplicates = false;

	// 루트는 빈 SceneComponent — 풀 Acquire 가 SetActorTransform(위치만, 스케일 1)으로 복원하므로
	// 메시가 루트면 BP 에서 잡은 스케일이 매번 1.0 으로 리셋된다. 메시를 자식으로 두면
	// BP 의 상대 스케일이 보존된다 (ABomb·ADangerDecal·AWaterSegment 와 같은 구조).
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 진짜 폭탄(ABomb)과 동일하게 순수 표시용 — 컬리전 없음 (판정은 100% 서버 소관).
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetReceivesDecals(false); // 위험 데칼이 예측 폭탄 구체에 빨갛게 입혀지는 것 방지
}

void APredictedBombVisual::OnAcquiredFromPool()
{
	// 반납 시 컴포넌트 가시성을 명시적으로 껐으므로 대칭으로 켠다 (ADangerDecal 관례).
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(true);
	}

	if (!bWarnedMissingMesh && MeshComponent && !MeshComponent->GetStaticMesh())
	{
		bWarnedMissingMesh = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("APredictedBombVisual: 메시 미지정 — BP_PredictedBombVisual 을 만들어 BP_Bomb 과 같은 메시를 지정하고 룰셋 PredictedBombVisualClass 에 연결할 것 (미지정이어도 판정은 정상)"));
	}
}

void APredictedBombVisual::OnReleasedToPool()
{
	// 정지할 타이머·FX 가 없음이 이 클래스의 계약 (불변식 3) — 가시성만 끈다.
	// 풀의 SetActorHiddenInGame 과 별개로 컴포넌트 단에서도 꺼서 잔류 표시를 원천 차단한다.
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(false);
	}
}
