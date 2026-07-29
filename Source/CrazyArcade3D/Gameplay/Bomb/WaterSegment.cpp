#include "Gameplay/Bomb/WaterSegment.h"

#include "CrazyArcade3D.h"
#include "Core/PoolSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"

AWaterSegment::AWaterSegment()
{
	PrimaryActorTick.bCanEverTick = false;

	// 클라 로컬 시각 전용 — 복제하지 않는다 (서버는 Multicast 로 셀 목록만 보낸다).
	bReplicates = false;

	// 물줄기는 판정에 관여하지 않는다 (판정은 서버의 WaterCells × GetFootCell) — 컬리전 없음.
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	RootComponent = MeshComponent;
}

void AWaterSegment::StartLinger(float Seconds)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (Multicast 가드의 이중 안전망)

	// 0 이하가 들어와도 최소 한 프레임은 보이고 반납되게 클램프.
	GetWorldTimerManager().SetTimer(LingerTimer,
		FTimerDelegate::CreateUObject(this, &AWaterSegment::ReleaseSelf),
		FMath::Max(Seconds, KINDA_SMALL_NUMBER), false);
}

void AWaterSegment::OnAcquiredFromPool()
{
	// 위치·표시·컬리전은 풀이 이미 복원했다 (IPooledActor 계약) — 여기서는 검사만.
	if (!bWarnedMissingMesh && MeshComponent && !MeshComponent->GetStaticMesh())
	{
		bWarnedMissingMesh = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("AWaterSegment: 메시 미지정 — BP_WaterSegment 를 만들어 메시를 지정하고 룰셋 WaterSegmentClass 에 연결할 것 (미지정이어도 판정은 정상)"));
	}
}

void AWaterSegment::OnReleasedToPool()
{
	// 풀 오염 방지 — 잔존 타이머가 풀 안에서 ReleaseSelf 를 다시 부르면 이중 반납이 된다.
	GetWorldTimerManager().ClearTimer(LingerTimer);
}

void AWaterSegment::ReleaseSelf()
{
	if (UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr)
	{
		Pool->Release(this);
	}
	else
	{
		Destroy(); // 월드 정리 중 폴백 — 풀이 없으면 그냥 소멸
	}
}
