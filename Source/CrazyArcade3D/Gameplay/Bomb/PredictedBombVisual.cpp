#include "Gameplay/Bomb/PredictedBombVisual.h"

#include "CrazyArcade3D.h"
#include "Gameplay/SpinVisual.h"
#include "Components/StaticMeshComponent.h"

APredictedBombVisual::APredictedBombVisual()
{
	// 틱은 메시 제자리 회전 **전용** (2026-08-06) — ABomb 과 같은 속도로 돌아야 서버 확정 시
	// 교체가 안 보인다. **로직은 여전히 0 이다** (불변식 3): 타이머·판정·연쇄를 여기에 넣는
	// 순간 불변식 3이 깨진다. 회전은 상태가 아니다 — 잘못 돌아도 게임 결과가 달라지지 않는다.
	PrimaryActorTick.bCanEverTick = true;

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

void APredictedBombVisual::BeginPlay()
{
	Super::BeginPlay();

	// 이 액터는 원격 클라에서만 스폰된다(헤더 주석) — 데디 가드는 스폰 경로가 이미 진다.
	SpinDegreesPerSecond = CA3DSpin::ResolveDegreesPerSecond(GetWorld());

	// BP 가 잡아 둔 원래 회전값을 기억해 둔다 — 풀 재사용 때 여기로 되돌린다 (아래 주석).
	if (MeshComponent)
	{
		AuthoredMeshRotation = MeshComponent->GetRelativeRotation();
	}
}

void APredictedBombVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CA3DSpin::ApplyYaw(MeshComponent, DeltaSeconds, SpinDegreesPerSecond);
}

void APredictedBombVisual::OnAcquiredFromPool()
{
	// 반납 시 컴포넌트 가시성을 명시적으로 껐으므로 대칭으로 켠다 (ADangerDecal 관례).
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(true);

		// 회전을 원래 각도로 되돌린다. 안 되돌리면 재사용된 예측 폭탄이 직전 사용분의
		// 누적 각도에서 시작하고, 곧이어 도착한 ABomb(각도 0에서 시작)로 교체될 때
		// 각도가 툭 튄다 — "예측과 진짜가 구분되지 않아야 정상" (헤더)이 눈에 띄게 깨진다.
		// 0 이 아니라 BP 가 잡아 둔 값으로 되돌리는 이유: 메시 정렬용 회전을 지우면 안 된다.
		MeshComponent->SetRelativeRotation(AuthoredMeshRotation);
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
