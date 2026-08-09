#include "Gameplay/CA3DFeedback.h"

#include "CrazyArcade3D.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"  // Private 의존 (CrazyArcade3D.Build.cs 주석) — 헤더에는 안 나온다
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr int32 CA3DCueCount = static_cast<int32>(ECA3DCue::Count);

	// 미지정 큐 경고 1회 제한. 프로세스 전역인 것이 의도다 — PIE 를 여러 번 돌려도
	// 같은 미지정 에셋을 매번 다시 알릴 이유가 없다 (AItemPickup::bWarnedMissingMesh 관례).
	bool GWarnedMissingCue[CA3DCueCount] = {};

#if !UE_BUILD_SHIPPING
	int32 GPlayCount = 0;
	int32 GRelayBroadcastCount = 0;
#endif

	// 룰셋 해석 — GameState 의 복제 포인터, 없으면 CDO 폴백
	// (ABomb::ResolveBombRules / ACA3DCharacter::ResolveVisualRules 와 동일 관례).
	const UCA3DRuleSet* ResolveFeedbackRules(const UWorld* World)
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
}

// ─── 큐 → 룰셋 필드 (순수) ────────────────────────────────────────────────────

void CA3DFeedback::ResolveCueAssets(const UCA3DRuleSet& Rules, ECA3DCue Cue,
	USoundBase*& OutSound, UNiagaraSystem*& OutFX)
{
	OutSound = nullptr;
	OutFX = nullptr;

	// TMap 이 아니라 switch + 이름 있는 필드인 이유는 룰셋 헤더의 Feedback 절 주석에 있다 —
	// 사용자가 에디터에서 채우는 값이라 필드 이름이 곧 설명이어야 한다.
	switch (Cue)
	{
	case ECA3DCue::BombPlace:       OutSound = Rules.BombPlaceSound;       OutFX = Rules.BombPlaceFX;       break;
	case ECA3DCue::Explosion:       OutSound = Rules.ExplosionSound;       OutFX = Rules.ExplosionFX;       break;
	case ECA3DCue::BlockBreak:      OutSound = Rules.BlockBreakSound;      OutFX = Rules.BlockBreakFX;      break;
	case ECA3DCue::ItemPickup:      OutSound = Rules.ItemPickupSound;      OutFX = Rules.ItemPickupFX;      break;
	case ECA3DCue::Trapped:         OutSound = Rules.TrappedSound;         OutFX = Rules.TrappedFX;         break;
	case ECA3DCue::Escape:          OutSound = Rules.EscapeSound;          OutFX = Rules.EscapeFX;          break;
	case ECA3DCue::Death:           OutSound = Rules.DeathSound;           OutFX = Rules.DeathFX;           break;
	case ECA3DCue::Kick:            OutSound = Rules.KickSound;            OutFX = Rules.KickFX;            break;
	case ECA3DCue::SuddenDeathWarn: OutSound = Rules.SuddenDeathWarnSound; OutFX = Rules.SuddenDeathWarnFX; break;
	case ECA3DCue::MatchEnd:        OutSound = Rules.MatchEndSound;        OutFX = Rules.MatchEndFX;        break;
	default:
		// 큐를 늘리고 여기를 잊으면 그 큐는 영원히 소리가 안 난다 — 조용히 넘어가지 않는다.
		ensureMsgf(false, TEXT("CA3DFeedback::ResolveCueAssets: 미처리 큐 %d"), static_cast<int32>(Cue));
		break;
	}
}

// ─── 재생 (데디 가드가 사는 유일한 곳) ────────────────────────────────────────

void CA3DFeedback::Play(const UWorld* World, ECA3DCue Cue, const FVector& Location)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 소리·이펙트는 순수 시각

	if (!World)
	{
		return; // 정리 중 월드 등 — 재생 대상이 없다
	}

	const int32 Index = static_cast<int32>(Cue);
	if (!ensure(Index >= 0 && Index < CA3DCueCount))
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	++GPlayCount;
#endif

	const UCA3DRuleSet* Rules = ResolveFeedbackRules(World);

	USoundBase* Sound = nullptr;
	UNiagaraSystem* FX = nullptr;
	ResolveCueAssets(*Rules, Cue, Sound, FX);

	if (!Sound && !FX)
	{
		// 에셋 미지정은 **정상 동작**이다 (에셋 없이도 게임은 돌아간다 — ADangerDecal 관례).
		// 큐당 한 번만 알린다: 폭발 한 번에 이 경로가 수십 번 지나가므로 매번 찍으면 로그가
		// 통째로 무의미해진다.
		if (!GWarnedMissingCue[Index])
		{
			GWarnedMissingCue[Index] = true;
			UE_LOG(LogCA3D, Verbose,
				TEXT("CA3DFeedback: 큐 %s 에 사운드·이펙트 미지정 — 룰셋(DA_Rules_Default)의 Feedback 카테고리에서 지정할 것 (미지정이어도 게임 동작은 정상)"),
				*UEnum::GetValueAsString(Cue));
		}
		return;
	}

	// 볼륨 외의 튜닝 노브는 두지 않는다 — 감쇠·동시 재생 상한·랜덤 피치는 전부 에셋 쪽 일이다.
	const float Volume = FMath::Max(Rules->FeedbackVolumeMultiplier, 0.f);

	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, Volume);
	}
	if (FX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, FX, Location);
	}
}

void CA3DFeedback::ServerBroadcast(UWorld* World, ECA3DCue Cue, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return; // 릴레이는 서버 소유 — 클라에서 부르면 아무 일도 하지 않는다
	}

	if (UCA3DFeedbackSubsystem* Feedback = World->GetSubsystem<UCA3DFeedbackSubsystem>())
	{
		if (ACA3DCueRelay* CueRelay = Feedback->ResolveRelay())
		{
#if !UE_BUILD_SHIPPING
			++GRelayBroadcastCount; // 헤더 주석 — 여기까지가 우리 코드의 책임 범위다
#endif
			// 스탠드얼론·리슨 호스트에서는 이 호출이 로컬에서도 실행된다 — 호스트도 정확히 1회.
			CueRelay->MulticastCue(Cue, Location);
		}
	}
}

void CA3DFeedback::ResetMissingWarnings()
{
	for (int32 Index = 0; Index < CA3DCueCount; ++Index)
	{
		GWarnedMissingCue[Index] = false;
	}
}

bool CA3DFeedback::HasWarnedMissing(ECA3DCue Cue)
{
	const int32 Index = static_cast<int32>(Cue);
	return Index >= 0 && Index < CA3DCueCount && GWarnedMissingCue[Index];
}

#if !UE_BUILD_SHIPPING
int32 CA3DFeedback::GetPlayCount()
{
	return GPlayCount;
}

void CA3DFeedback::ResetPlayCount()
{
	GPlayCount = 0;
}

int32 CA3DFeedback::GetRelayBroadcastCount()
{
	return GRelayBroadcastCount;
}

void CA3DFeedback::ResetRelayBroadcastCount()
{
	GRelayBroadcastCount = 0;
}
#endif

// ─── 릴레이 ───────────────────────────────────────────────────────────────────

ACA3DCueRelay::ACA3DCueRelay()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	bAlwaysRelevant = true;
}

void ACA3DCueRelay::MulticastCue_Implementation(ECA3DCue Cue, FVector_NetQuantize Location)
{
	// 가드도 룰셋 해석도 하지 않는다 — 전부 Play 한 곳에 있다 (경로가 갈리면 언젠가 어긋난다).
	CA3DFeedback::Play(GetWorld(), Cue, Location);
}

// ─── 서브시스템 (그리드 구독 + 릴레이 캐시) ───────────────────────────────────

void UCA3DFeedbackSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// AVoxelWorld 는 레벨 배치 액터라 월드 BeginPlay 시점에 이미 존재한다 (레벨에 1개가 계약).
	// 없으면 지형이 없는 맵이라는 뜻이므로 재시도하지 않는다.
	for (TActorIterator<AVoxelWorld> It(&InWorld); It; ++It)
	{
		CachedVoxelWorld = *It;
		break;
	}

	if (CachedVoxelWorld && !GridChangedHandle.IsValid())
	{
		GridChangedHandle = CachedVoxelWorld->OnGridChanged.AddUObject(
			this, &UCA3DFeedbackSubsystem::OnGridChanged);
	}
}

void UCA3DFeedbackSubsystem::Deinitialize()
{
	if (CachedVoxelWorld && GridChangedHandle.IsValid())
	{
		CachedVoxelWorld->OnGridChanged.Remove(GridChangedHandle);
	}
	GridChangedHandle.Reset();
	CachedVoxelWorld = nullptr;
	Relay = nullptr;

	Super::Deinitialize();
}

ACA3DCueRelay* UCA3DFeedbackSubsystem::ResolveRelay()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr; // 릴레이 스폰은 서버만 (클라는 복제로 받는다)
	}

	if (!IsValid(Relay))
	{
		// 서버에서 lazy 스폰 — bAlwaysRelevant 복제 액터라 전 클라에 존재하게 된다
		// (UExplosionSubsystem::ResolveFXRelay 와 같은 관례).
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient; // 레벨 저장 대상이 아니다
		Relay = World->SpawnActor<ACA3DCueRelay>(Params);
	}
	return Relay;
}

void UCA3DFeedbackSubsystem::OnGridChanged(const TArray<FIntVector>& ChangedCells)
{
	if (ChangedCells.Num() == 0 || !CachedVoxelWorld)
	{
		return;
	}

	// 한 폭발이 20칸을 부숴도 소리는 한 번 — 위치는 부서진 칸들의 무게중심이다.
	// 칸마다 내면 같은 소리 20개가 겹쳐 소음이 되고, 그것을 Concurrency 로 깎으면
	// "왜 소리가 잘리지" 라는 다른 문제로 바뀐다. 사건 단위로 한 번 내는 편이 맞다.
	FVector Sum = FVector::ZeroVector;
	for (const FIntVector& Cell : ChangedCells)
	{
		Sum += CachedVoxelWorld->CellToWorld(Cell);
	}

	CA3DFeedback::Play(GetWorld(), ECA3DCue::BlockBreak, Sum / static_cast<double>(ChangedCells.Num()));
}
