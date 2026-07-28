#include "Core/PoolSubsystem.h"

#include "CrazyArcade3D.h"
#include "Core/PooledActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UPoolSubsystem::Prewarm(TSubclassOf<AActor> Class, int32 Count)
{
	if (!ensureMsgf(Class, TEXT("PoolSubsystem::Prewarm — Class가 비어 있음")) || Count <= 0)
	{
		return;
	}

	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		AActor* Actor = SpawnPooledActor(Class, FTransform::Identity);
		if (!Actor)
		{
			break;
		}
		// 스폰 직후 Release 상태로 보관 — OnReleasedToPool 콜백 포함.
		// 액터가 풀 안에 있는 동안은 항상 "정리 완료" 상태라는 규약을 Prewarm에도 적용한다.
		DeactivateAndStore(Actor);
		++Spawned;
	}

	// 체크리스트 검증용 — 선스폰 개수 로그.
	UE_LOG(LogCA3D, Log, TEXT("PoolSubsystem::Prewarm — %s %d/%d개 선스폰"),
		*GetNameSafe(*Class), Spawned, Count);
}

AActor* UPoolSubsystem::Acquire(TSubclassOf<AActor> Class, const FTransform& T)
{
	if (!ensureMsgf(Class, TEXT("PoolSubsystem::Acquire — Class가 비어 있음")))
	{
		return nullptr;
	}

	AActor* Actor = nullptr;

	// TMap은 조회만 한다 (순회 금지 규칙).
	if (FPooledActorArray* List = Free.Find(Class))
	{
		while (!Actor && List->Actors.Num() > 0)
		{
			// 레벨 정리 등으로 파괴된 액터가 남아 있을 수 있음 — 유효한 것이 나올 때까지 버린다.
			AActor* Candidate = List->Actors.Pop(EAllowShrinking::No);
			Actor = IsValid(Candidate) ? Candidate : nullptr;
		}
	}

	if (Actor)
	{
		// Release의 역순으로 복원: 위치 → 틱 on → 컬리전 on → 표시.
		Actor->SetActorTransform(T, /*bSweep*/ false, nullptr, ETeleportType::ResetPhysics);
		Actor->SetActorTickEnabled(true);
		Actor->SetActorEnableCollision(true);
		Actor->SetActorHiddenInGame(false);

		// 체크리스트 검증용 — 프리 리스트 반환 여부 로그.
		UE_LOG(LogCA3D, Verbose, TEXT("PoolSubsystem::Acquire — %s 프리 리스트에서 반환"),
			*Actor->GetName());
	}
	else
	{
		Actor = SpawnPooledActor(Class, T);
		if (!Actor)
		{
			return nullptr;
		}
		UE_LOG(LogCA3D, Verbose, TEXT("PoolSubsystem::Acquire — %s 신규 스폰 (프리 리스트 비어 있음)"),
			*Actor->GetName());
	}

	// 위치·활성 상태가 준비된 뒤에 콜백 — IPooledActor 계약("위치는 이미 세팅됨").
	if (IPooledActor* Pooled = Cast<IPooledActor>(Actor))
	{
		Pooled->OnAcquiredFromPool();
	}
	else
	{
		// 계약 위반 조기 발견 — 풀링 대상은 반드시 IPooledActor를 구현해야 한다.
		ensureMsgf(false, TEXT("PoolSubsystem::Acquire — %s가 IPooledActor 미구현"),
			*GetNameSafe(Actor->GetClass()));
	}

	return Actor;
}

void UPoolSubsystem::Release(AActor* Actor)
{
	if (!ensureMsgf(IsValid(Actor), TEXT("PoolSubsystem::Release — 유효하지 않은 액터")))
	{
		return;
	}

	// 이중 Release 방어 — 같은 액터가 두 번 들어가면 이후 두 사용자에게 중복 대여된다.
	const FPooledActorArray* List = Free.Find(Actor->GetClass());
	if (!ensureMsgf(!List || !List->Actors.Contains(Actor),
		TEXT("PoolSubsystem::Release — %s 이중 반납"), *Actor->GetName()))
	{
		return;
	}

	DeactivateAndStore(Actor);
}

AActor* UPoolSubsystem::SpawnPooledActor(TSubclassOf<AActor> Class, const FTransform& T)
{
	UWorld* World = GetWorld();
	check(World);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // 풀 액터는 레벨 저장 대상이 아니다.
	return World->SpawnActor<AActor>(Class, T, Params);
}

void UPoolSubsystem::DeactivateAndStore(AActor* Actor)
{
	// 콜백 먼저 — 타이머·FX·사운드 정지 등 정리가 끝난 뒤에 비활성화한다.
	if (IPooledActor* Pooled = Cast<IPooledActor>(Actor))
	{
		Pooled->OnReleasedToPool();
	}
	else
	{
		// 계약 위반 조기 발견 — 풀링 대상은 반드시 IPooledActor를 구현해야 한다.
		ensureMsgf(false, TEXT("PoolSubsystem::Release — %s가 IPooledActor 미구현"),
			*GetNameSafe(Actor->GetClass()));
	}

	// Release 상태: 숨김 + 컬리전 off + 틱 off.
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);

	// Release는 액터의 실제 클래스로 보관한다 — Acquire(요청 클래스)와 정확히 같은
	// 클래스 단위로만 재사용된다 (파생 클래스를 베이스 요청에 섞지 않는다).
	Free.FindOrAdd(Actor->GetClass()).Actors.Add(Actor);
}
