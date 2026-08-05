#include "Gameplay/Character/OcclusionFadeComponent.h"

#include "CrazyArcade3D.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelRayCast.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

namespace
{
	// ⚠️ 헬퍼 이름에 Occ 접두사 — 번역 단위 병합(unity build) 시 다른 .cpp 의 익명 네임스페이스
	// 헬퍼와 이름이 겹치면 C2084 가 난다 (Sd~ 접두사와 같은 사정).

	// 룰셋 해석 — GameState 복제 포인터 → CDO 폴백 (StatusComponent::ResolveRules 관례).
	const UCA3DRuleSet* OccResolveRules(const UWorld* World)
	{
		if (World)
		{
			if (const ACA3DGameState* GameState = World->GetGameState<ACA3DGameState>())
			{
				if (GameState->Rules)
				{
					return GameState->Rules;
				}
			}
		}
		return GetDefault<UCA3DRuleSet>();
	}

	// 격자 밖에서 출발해도 캐릭터까지 닿을 만큼 넉넉한 상한. 카메라 거리 12칸 × 3축이라
	// 실제로는 40 걸음 안쪽에서 끝난다 — 이 값은 폭주 방지용이지 성능 예산이 아니다.
	constexpr int32 OccMaxMarchSteps = 256;

	static TAutoConsoleVariable<int32> CVarCA3DDebugOcclusionFade(
		TEXT("ca3d.DebugOcclusionFade"),
		0,
		TEXT("1 = 가림으로 판정된 블록에 디버그 박스를 그린다.\n")
		TEXT("머티리얼 작업 전에 C++ 판정이 맞는지 눈으로 확인하는 용도."),
		ECVF_Cheat);
}

UOcclusionFadeComponent::UOcclusionFadeComponent()
{
	// 페이드 보간은 매 프레임, **재계산은 0.1초마다** (GDD 7.4). 틱 자체를 0.1초로 늦추면
	// 페이드가 초당 10단계로 끊겨 보인다 — 두 주기를 분리하는 것이 요점이다.
	PrimaryComponentTick.bCanEverTick = true;
}

void UOcclusionFadeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (IsRunningDedicatedServer())
	{
		// 불변식 5 — 순수 시각. 데디에는 카메라도 렌더러도 없다.
		SetComponentTickEnabled(false);
		return;
	}

	const UCA3DRuleSet* Rules = OccResolveRules(GetWorld());
	TraceInterval = Rules->OcclusionTraceInterval;
	FadeSpeed     = Rules->OcclusionFadeSpeed;
	FadeAmount    = Rules->OcclusionFadeAmount;
	SampleCount   = Rules->OcclusionSampleCount;
}

void UOcclusionFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 추적하던 칸을 전부 원래대로. 안 하면 사망·레벨 전환 뒤에도 그 블록만 투명하게 남는다
	// (컴포넌트는 사라지고 값은 인스턴스에 남으므로 되돌릴 주체가 없어진다).
	if (AVoxelWorld* VoxelWorld = CachedVoxelWorld)
	{
		for (const TPair<FIntVector, float>& Pair : FadeValues)
		{
			VoxelWorld->SetCellFade(Pair.Key, 0.f);
		}
	}
	FadeValues.Empty();
	CurrentTargets.Empty();

	Super::EndPlay(EndPlayReason);
}

void UOcclusionFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TimeSinceTrace += DeltaTime;
	if (TimeSinceTrace >= TraceInterval)
	{
		TimeSinceTrace = 0.f;
		RefreshOccluders();
	}

	AdvanceFades(DeltaTime);
}

void UOcclusionFadeComponent::RefreshOccluders()
{
	CurrentTargets.Reset();

	// **내가 조종하는 폰일 때만.** 남의 캐릭터 앞을 뚫어줄 이유가 없고, 리슨 호스트에서
	// 모든 폰이 각자 페이드를 걸면 화면의 절반이 사라진다.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return; // 목표가 빈 채로 AdvanceFades 가 돌면서 걸려 있던 페이드가 스스로 풀린다
	}

	const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}

	AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld || !VoxelWorld->IsGridInitialized())
	{
		return; // 클라에서 그리드가 아직 안 왔다 (OnRep_Seed 이전) — 다음 주기에 다시 본다
	}

	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	const FVector PawnLocation   = OwnerPawn->GetActorLocation();

	// 캡슐 크기로 샘플 지점을 만든다 — 광선 하나면 블록 모서리를 스쳐 지나가 캐릭터가
	// 반쯤 가린 채로 남는다. 좌우 오프셋은 **시선에 수직**이어야 의미가 있다.
	float CapsuleRadius = 0.f;
	float CapsuleHalfHeight = 0.f;
	OwnerPawn->GetSimpleCollisionCylinder(CapsuleRadius, CapsuleHalfHeight);

	const FVector ToCamera = (CameraLocation - PawnLocation).GetSafeNormal();
	FVector Right = FVector::CrossProduct(ToCamera, FVector::UpVector).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector; // 카메라가 정확히 머리 위 — 좌우 정의가 없다
	}

	TArray<FVector, TInlineAllocator<5>> Samples;
	Samples.Add(PawnLocation);
	if (SampleCount >= 3)
	{
		Samples.Add(PawnLocation + FVector(0.f, 0.f, CapsuleHalfHeight * 0.8f)); // 머리
		Samples.Add(PawnLocation - FVector(0.f, 0.f, CapsuleHalfHeight * 0.8f)); // 발
	}
	if (SampleCount >= 5)
	{
		Samples.Add(PawnLocation + Right * CapsuleRadius);
		Samples.Add(PawnLocation - Right * CapsuleRadius);
	}

	const FVector CameraCell = VoxelWorld->WorldToCellFloat(CameraLocation);
	const FVoxelGrid& Grid = VoxelWorld->GetGrid();

	TArray<FIntVector> Hits;
	for (const FVector& Sample : Samples)
	{
		Hits.Reset();
		VoxelRay::GatherSolidCells(Grid, CameraCell, VoxelWorld->WorldToCellFloat(Sample),
			OccMaxMarchSteps, Hits);
		for (const FIntVector& Cell : Hits)
		{
			CurrentTargets.Add(Cell);
		}
	}

	// 가려진 칸 수가 바뀔 때만 한 줄 — 매 주기 찍으면 초당 10줄이 된다.
	// 머티리얼 작업 전에 "C++ 판정은 도는가"를 로그만으로 확인하는 통로다.
	if (CurrentTargets.Num() != LastLoggedOccluderCount)
	{
		LastLoggedOccluderCount = CurrentTargets.Num();
		UE_LOG(LogCA3D, Verbose, TEXT("UOcclusionFadeComponent: 가림 블록 %d칸 (샘플 %d개)"),
			CurrentTargets.Num(), Samples.Num());
	}

	// ENABLE_DRAW_DEBUG 가드: Shipping 빌드에는 DrawDebug* 심볼 자체가 없다.
#if ENABLE_DRAW_DEBUG
	if (CVarCA3DDebugOcclusionFade.GetValueOnGameThread() != 0)
	{
		const float CellSize = VoxelWorld->CellSize;
		for (const FIntVector& Cell : CurrentTargets)
		{
			DrawDebugBox(GetWorld(), VoxelWorld->CellToWorld(Cell),
				FVector(CellSize * 0.5f), FColor::Cyan, /*bPersistent=*/false, TraceInterval);
		}
		DrawDebugLine(GetWorld(), CameraLocation, PawnLocation, FColor::Yellow,
			false, TraceInterval);
	}
#endif
}

void UOcclusionFadeComponent::AdvanceFades(float DeltaTime)
{
	AVoxelWorld* VoxelWorld = CachedVoxelWorld;
	if (!VoxelWorld)
	{
		return;
	}

	// 새로 가려진 칸을 추적 목록에 편입 (값 0에서 시작 — 갑자기 사라지지 않고 서서히).
	for (const FIntVector& Cell : CurrentTargets)
	{
		FadeValues.FindOrAdd(Cell, 0.f);
	}

	const float StepAmount = FadeSpeed * DeltaTime;

	TArray<FIntVector, TInlineAllocator<32>> Finished;
	for (TPair<FIntVector, float>& Pair : FadeValues)
	{
		const float Target = CurrentTargets.Contains(Pair.Key) ? FadeAmount : 0.f;
		Pair.Value = FMath::FInterpConstantTo(Pair.Value, Target, 1.f, StepAmount);

		// 렌더러가 false 를 주면 그 셀에는 더 이상 인스턴스가 없다 (폭발로 파괴됨) —
		// 추적을 끊지 않으면 사라진 블록을 계속 갱신하려 든다.
		if (!VoxelWorld->SetCellFade(Pair.Key, Pair.Value))
		{
			Finished.Add(Pair.Key);
			continue;
		}

		// 0 까지 돌아왔고 더 이상 가리지도 않는다 — 추적 종료 (0 은 방금 기록했다).
		if (Target == 0.f && FMath::IsNearlyZero(Pair.Value))
		{
			Finished.Add(Pair.Key);
		}
	}

	for (const FIntVector& Cell : Finished)
	{
		FadeValues.Remove(Cell);
	}
}

AVoxelWorld* UOcclusionFadeComponent::ResolveVoxelWorld()
{
	if (!CachedVoxelWorld)
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			CachedVoxelWorld = *It;
			break; // 레벨에 1개 배치가 계약
		}
	}
	return CachedVoxelWorld;
}
