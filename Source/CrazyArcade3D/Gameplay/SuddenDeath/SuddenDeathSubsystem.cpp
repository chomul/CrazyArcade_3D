#include "Gameplay/SuddenDeath/SuddenDeathSubsystem.h"

#include "CrazyArcade3D.h"
#include "Core/PoolSubsystem.h"
#include "Gameplay/Bomb/ExplosionSubsystem.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "Algo/BinarySearch.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	// 룰셋 해석 — GameState 복제 포인터 → CDO 폴백 (ExplosionSubsystem::ResolveChainRules 관례).
	const UCA3DRuleSet* SuddenDeathResolveRules(const UWorld* World)
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

	// 기둥 재추첨 상한. **튜닝 값이 아니라 무한 루프 방지 장치**라 룰셋에 두지 않았다 —
	// 이 값을 바꿔도 게임 밸런스는 변하지 않는다 (뚫린 기둥을 몇 번까지 다시 뽑아 볼지일 뿐).
	// 32회면 후보 기둥이 전체의 3%만 남아도 실패 확률이 1% 미만이다.
	constexpr int32 SuddenDeathMaxPickAttempts = 32;

	// 가중치 보간의 기준값(= 중심 셀의 가중치). 정수 퍼센트 계산의 100% 를 뜻한다.
	constexpr int32 SuddenDeathCenterWeight = 100;
}

// ═══ ASuddenDeathDropMarker — 클라 예고 마커 (시각 전용) ═════════════════════

ASuddenDeathDropMarker::ASuddenDeathDropMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	// 클라 로컬 시각 전용 — 복제하지 않는다 (서버는 Multicast 로 셀 목록만 보낸다).
	bReplicates = false;

	// 루트는 빈 SceneComponent — 풀 Acquire 가 SetActorTransform(위치만) 으로 복원하므로
	// 메시가 루트면 BP 에서 잡은 스케일이 매번 1.0 으로 리셋된다 (AWaterSegment 와 같은 구조).
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 예고는 판정에 관여하지 않는다 — 컬리전 없음. 캐릭터가 마커를 통과해 지나갈 수 있어야 한다.
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ASuddenDeathDropMarker::StartWarning(float Seconds)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (Multicast 가드의 이중 안전망)

	// 0 이하가 들어와도 최소 한 프레임은 보이고 반납되게 클램프.
	GetWorldTimerManager().SetTimer(WarnTimer,
		FTimerDelegate::CreateUObject(this, &ASuddenDeathDropMarker::ReleaseSelf),
		FMath::Max(Seconds, KINDA_SMALL_NUMBER), false);
}

void ASuddenDeathDropMarker::OnAcquiredFromPool()
{
	// 위치·표시·컬리전은 풀이 이미 복원했다 (IPooledActor 계약) — 여기서는 검사만.
	if (!bWarnedMissingMesh && MeshComponent && !MeshComponent->GetStaticMesh())
	{
		bWarnedMissingMesh = true;
		UE_LOG(LogCA3D, Warning,
			TEXT("ASuddenDeathDropMarker: 메시 미지정 — BP_DropMarker 를 만들어 메시를 지정하고 룰셋 DropMarkerClass 에 연결할 것 (미지정이어도 낙하·판정은 정상)"));
	}
}

void ASuddenDeathDropMarker::OnReleasedToPool()
{
	// 풀 오염 방지 — 잔존 타이머가 풀 안에서 ReleaseSelf 를 다시 부르면 이중 반납이 된다.
	GetWorldTimerManager().ClearTimer(WarnTimer);
}

void ASuddenDeathDropMarker::ReleaseSelf()
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

// ═══ ASuddenDeathRelay — 예고 Multicast 소유 액터 ════════════════════════════

ASuddenDeathRelay::ASuddenDeathRelay()
{
	PrimaryActorTick.bCanEverTick = false;

	bReplicates = true;
	bAlwaysRelevant = true; // 예고를 놓치면 피할 수 없는 낙하가 된다 (헤더 주석)
}

AVoxelWorld* ASuddenDeathRelay::ResolveVoxelWorld()
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

void ASuddenDeathRelay::MulticastWarnDrop_Implementation(const TArray<FIntVector>& Cells, float Delay)
{
	if (IsRunningDedicatedServer()) return; // 불변식 5 — 시각 전용 (리슨 호스트는 플레이어라 표시)

	// 룰셋 해석 — 마커 클래스의 출처. 복제 미도착이면 CDO 폴백 (AExplosionFXRelay 와 동일 관례).
	const UCA3DRuleSet* Rules = SuddenDeathResolveRules(GetWorld());

	// 마커 클래스 미지정이면 **예고만 생략하고 게임은 그대로 간다** — 에셋 없이도 동작해야 한다.
	// 다른 FX 클래스들과 달리 C++ 기본 클래스 폴백을 쓰지 않는 이유: 메시 없는 마커는 화면에
	// 아무것도 그리지 않으므로 폴백해도 "예고가 안 보인다" 는 결과가 같고, 풀에 빈 액터만 쌓인다.
	if (!Rules->DropMarkerClass)
	{
		if (!bWarnedMissingMarkerClass)
		{
			bWarnedMissingMarkerClass = true;
			UE_LOG(LogCA3D, Warning,
				TEXT("ASuddenDeathRelay: 룰셋 DropMarkerClass 미지정 — 낙하 예고 마커 생략 (낙하·판정은 정상). BP_DropMarker 를 만들어 지정할 것"));
		}
		return;
	}

	AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	UPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UPoolSubsystem>() : nullptr;
	if (!VoxelWorld || !Pool)
	{
		UE_LOG(LogCA3D, Warning, TEXT("ASuddenDeathRelay: VoxelWorld/풀 미확보 — 낙하 예고 생략 (판정은 서버가 수행)"));
		return;
	}

	for (const FIntVector& Cell : Cells)
	{
		ASuddenDeathDropMarker* Marker =
			Pool->Acquire<ASuddenDeathDropMarker>(Rules->DropMarkerClass, FTransform(VoxelWorld->CellToWorld(Cell)));
		if (Marker)
		{
			Marker->StartWarning(Delay);
		}
	}
}

// ═══ USuddenDeathSubsystem — 서버 낙하 스케줄러 ══════════════════════════════

int32 USuddenDeathSubsystem::ToOuterWeightPercent(float OuterWeightBias)
{
	// 하한 1: 0 이하가 들어오면 최외곽 가중치가 0 이 되어 외곽이 절대 안 뽑힌다 —
	// 서든데스가 중앙만 파먹는 정반대 동작이 되므로 값 실수를 여기서 막는다.
	return FMath::Max(1, FMath::RoundToInt(OuterWeightBias * static_cast<float>(SuddenDeathCenterWeight)));
}

bool USuddenDeathSubsystem::PickDropCell(const FVoxelGrid& Grid, FRandomStream& Stream,
                                         int32 OuterWeightPercent, int32 MaxAttempts, FIntVector& OutCell)
{
	if (Grid.Size.X <= 0 || Grid.Size.Y <= 0 || Grid.Size.Z <= 0)
	{
		return false; // 초기화되지 않은 그리드 — 호출자가 웨이브를 건너뛴다
	}

	const int32 OuterWeight = FMath::Max(1, OuterWeightPercent);

	// ── 가중치 표 (XY 기둥마다 1개) ──
	// 좌표를 2배로 다뤄 중심을 정수로 표현한다 — 21칸이면 중심은 10.0, 20칸이면 9.5 라
	// 그냥 나누면 짝수 크기에서 반올림 편향이 생긴다. (2X - (Size-1)) 은 항상 정수다.
	// 거리는 체비셰프(가장 큰 축) — 사각 맵에서 "테두리에 가까운가" 가 그대로 나온다.
	// **float 연산 없음**: 같은 스트림 상태면 어떤 플랫폼에서도 같은 셀이 나온다.
	const int32 SpanX = Grid.Size.X - 1;
	const int32 SpanY = Grid.Size.Y - 1;
	const int32 MaxSpan = FMath::Max(FMath::Max(SpanX, SpanY), 1);

	TArray<int32> Cumulative;
	Cumulative.Reserve(Grid.Size.X * Grid.Size.Y);

	int32 Total = 0;
	for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
	{
		for (int32 X = 0; X < Grid.Size.X; ++X)
		{
			const int32 Distance = FMath::Max(FMath::Abs(2 * X - SpanX), FMath::Abs(2 * Y - SpanY));

			// 중심(거리 0) = 100 → 최외곽(거리 MaxSpan) = OuterWeight 선형 보간. 정수 나눗셈.
			const int32 Weight = FMath::Max(1,
				SuddenDeathCenterWeight + (OuterWeight - SuddenDeathCenterWeight) * Distance / MaxSpan);

			Total += Weight;
			Cumulative.Add(Total); // 누적합 — 뽑기는 이분 탐색 1회로 끝난다
		}
	}

	if (Total <= 0)
	{
		return false;
	}

	// ── 뽑기 + 재추첨 ──
	for (int32 Attempt = 0; Attempt < FMath::Max(MaxAttempts, 1); ++Attempt)
	{
		const int32 Roll = Stream.RandRange(0, Total - 1);
		const int32 Index = Algo::UpperBound(Cumulative, Roll); // 첫 "누적합 > Roll" 인덱스
		if (!Cumulative.IsValidIndex(Index))
		{
			continue; // 도달 불가 (Roll < Total) — 계약 안전망
		}

		const int32 X = Index % Grid.Size.X;
		const int32 Y = Index / Grid.Size.X;

		// 그 기둥에서 가장 높은 solid 블록. 위에서부터 내려오며 첫 solid 를 찾는다.
		int32 TopSolidZ = INDEX_NONE;
		for (int32 Z = Grid.Size.Z - 1; Z >= 0; --Z)
		{
			if (Grid.IsSolid(FIntVector(X, Y, Z)))
			{
				TopSolidZ = Z;
				break;
			}
		}

		if (TopSolidZ == INDEX_NONE)
		{
			continue; // 이미 전부 뚫린 기둥 — 떨어뜨려 봐야 부술 것이 없다. 재추첨.
		}

		// 폭발 원점은 **가장 높은 블록 위 칸** — 그 칸이 곧 캐릭터가 서 있는 칸이라
		// 물줄기가 발판 위 사람을 덮고, 아래로는 그 블록과 바닥을 부순다.
		OutCell = FIntVector(X, Y, TopSolidZ + 1);
		return true;
	}

	return false;
}

void USuddenDeathSubsystem::ServerStart()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return; // 불변식 5 — 낙하 결정은 100% 서버

	if (bRunning)
	{
		return; // 중복 시작 방지 (타이머가 두 겹으로 돌면 낙하 주기가 조용히 2배가 된다)
	}

	bRunning = true;
	bWarnedNoCandidate = false;

	// 시드는 서버 로컬 난수로 잡는다 — 클라는 결과(Multicast)만 받으므로 재현할 필요가 없다.
	// 여기가 FMath::Rand() 가 허용되는 자리다 (불변식 4 는 맵 생성기 한정 — GameMode 시드와 같은 근거).
	Stream.Initialize(FMath::Rand());

	const UCA3DRuleSet* Rules = SuddenDeathResolveRules(World);
	const float Interval = FMath::Max(Rules->DropInterval, KINDA_SMALL_NUMBER);

	// 첫 웨이브도 Interval 뒤다 — 시작하자마자 예고 없이 떨어지는 것처럼 보이지 않게.
	World->GetTimerManager().SetTimer(DropTimer,
		FTimerDelegate::CreateUObject(this, &USuddenDeathSubsystem::ProcessDrop),
		Interval, true);

	UE_LOG(LogCA3D, Log,
		TEXT("USuddenDeathSubsystem: 서든데스 시작 — 주기 %.2f초 / 웨이브당 %d발 / 범위 %d칸 / 예고 %.2f초 / 외곽가중 %.2f / 바닥파괴 %s"),
		Interval, Rules->DropsPerWave, Rules->DropExplosionRange, Rules->DropWarningTime,
		Rules->OuterWeightBias, Rules->bSuddenDeathDestroysFloor ? TEXT("O") : TEXT("X"));
}

void USuddenDeathSubsystem::ServerStop()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return; // 불변식 5

	if (!bRunning)
	{
		return;
	}

	bRunning = false;
	World->GetTimerManager().ClearTimer(DropTimer);

	// 예고만 하고 아직 안 떨어진 웨이브까지 전부 취소 — 결과 화면이 뜬 뒤에 블록이
	// 떨어지면 "끝났는데 지형이 계속 변한다" 는 상태가 된다.
	const int32 CancelledWaves = PendingWaves.Num();
	ClearPendingWaves();

	UE_LOG(LogCA3D, Log, TEXT("USuddenDeathSubsystem: 서든데스 정지 — 예고 중이던 웨이브 %d개 취소"), CancelledWaves);
}

void USuddenDeathSubsystem::Deinitialize()
{
	// 레벨 전환·PIE 종료 안전망 — 타이머가 남아 파괴 중인 월드를 건드리지 않게 한다.
	// (GetTimerManager 접근이 가능한 마지막 시점이다.)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DropTimer);
	}
	ClearPendingWaves();
	bRunning = false;

	Super::Deinitialize();
}

void USuddenDeathSubsystem::ClearPendingWaves()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		for (FSuddenDeathWave& Wave : PendingWaves)
		{
			Timers.ClearTimer(Wave.Timer);
		}
	}
	PendingWaves.Reset();
}

void USuddenDeathSubsystem::ProcessDrop()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return; // 불변식 5

	if (!bRunning)
	{
		return; // 정지 직후 남은 타이머 콜백 — 안전망
	}

	AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld || !VoxelWorld->IsGridInitialized())
	{
		UE_LOG(LogCA3D, Error, TEXT("USuddenDeathSubsystem: VoxelWorld/그리드 미확보 — 이번 웨이브 건너뜀"));
		return;
	}

	const UCA3DRuleSet* Rules = SuddenDeathResolveRules(World);
	const int32 OuterWeightPercent = ToOuterWeightPercent(Rules->OuterWeightBias);
	const int32 DropsPerWave = FMath::Max(Rules->DropsPerWave, 1);

	FSuddenDeathWave Wave;
	Wave.Id = NextWaveId++;
	Wave.Cells.Reserve(DropsPerWave);

	for (int32 Index = 0; Index < DropsPerWave; ++Index)
	{
		FIntVector Cell;
		if (PickDropCell(VoxelWorld->GetGrid(), Stream, OuterWeightPercent, SuddenDeathMaxPickAttempts, Cell))
		{
			// AddUnique: 같은 웨이브가 한 셀을 두 번 때려도 폭발이 두 번 겹칠 뿐 낭비다.
			Wave.Cells.AddUnique(Cell);
		}
	}

	if (Wave.Cells.Num() == 0)
	{
		// 맵이 전부 뚫려 후보 기둥이 없다 — 매치 말기의 정상 상태다. 경고는 1회만.
		if (!bWarnedNoCandidate)
		{
			bWarnedNoCandidate = true;
			UE_LOG(LogCA3D, Warning,
				TEXT("USuddenDeathSubsystem: 낙하 지점 선정 실패(%d회 재추첨) — 부술 기둥이 남지 않았다. 이후 웨이브는 조용히 건너뛴다"),
				SuddenDeathMaxPickAttempts);
		}
		return;
	}

	// ── 예고 ──
	// 셀은 **지금** 확정해 Wave 에 담는다. 만료 시점에 다시 뽑으면 마커와 실제 낙하가 어긋나
	// "마커를 보고 피한다" 는 규칙이 무너진다 (체크리스트 24 필수 요건).
	const float WarningTime = FMath::Max(Rules->DropWarningTime, KINDA_SMALL_NUMBER);
	if (ASuddenDeathRelay* WarnRelay = ResolveRelay())
	{
		WarnRelay->MulticastWarnDrop(Wave.Cells, WarningTime);
	}

	World->GetTimerManager().SetTimer(Wave.Timer,
		FTimerDelegate::CreateUObject(this, &USuddenDeathSubsystem::ExecuteWave, Wave.Id),
		WarningTime, false);

	UE_LOG(LogCA3D, Verbose, TEXT("USuddenDeathSubsystem: 웨이브 #%d 예고 — %d발, %.2f초 후 낙하"),
		Wave.Id, Wave.Cells.Num(), WarningTime);

	PendingWaves.Add(MoveTemp(Wave));
}

void USuddenDeathSubsystem::ExecuteWave(int32 WaveId)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return; // 불변식 5

	const int32 WaveIndex = PendingWaves.IndexOfByPredicate(
		[WaveId](const FSuddenDeathWave& Each) { return Each.Id == WaveId; });
	if (WaveIndex == INDEX_NONE)
	{
		return; // ServerStop 이 이미 취소한 웨이브 — 정상 경로
	}

	// 예고 때 확정한 셀을 그대로 꺼낸다 (재추첨 금지 — 마커와 실제가 같아야 한다).
	const TArray<FIntVector> Cells = MoveTemp(PendingWaves[WaveIndex].Cells);
	PendingWaves.RemoveAt(WaveIndex);

	UExplosionSubsystem* Explosion = World->GetSubsystem<UExplosionSubsystem>();
	if (!Explosion)
	{
		UE_LOG(LogCA3D, Error, TEXT("USuddenDeathSubsystem: UExplosionSubsystem 없음 — 낙하 적용 불가"));
		return;
	}

	const UCA3DRuleSet* Rules = SuddenDeathResolveRules(World);

	// 폭탄과 **같은 함수**를 통과한다 — 파괴·물줄기 FX·갇힘·아이템이 전부 동일하게 처리된다.
	// bSuddenDeathDestroysFloor 를 넘기는 것이 서든데스와 폭탄의 유일한 차이다:
	// 폭탄은 bFloorDestructible(false)이라 바닥을 못 뚫고, 서든데스만 바닥을 뚫어 구멍을 만든다.
	for (const FIntVector& Cell : Cells)
	{
		Explosion->ServerApplyExplosionAt(Cell, Rules->DropExplosionRange, Rules->bSuddenDeathDestroysFloor);
	}

	UE_LOG(LogCA3D, Verbose, TEXT("USuddenDeathSubsystem: 웨이브 #%d 낙하 — %d발 적용"), WaveId, Cells.Num());
}

AVoxelWorld* USuddenDeathSubsystem::ResolveVoxelWorld()
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

ASuddenDeathRelay* USuddenDeathSubsystem::ResolveRelay()
{
	if (!IsValid(Relay))
	{
		// 서버에서 lazy 스폰 — bAlwaysRelevant 복제 액터라 전 클라에 존재하게 된다
		// (AExplosionFXRelay 와 같은 근거·같은 수명 관리).
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient; // 레벨 저장 대상이 아니다
		Relay = GetWorld()->SpawnActor<ASuddenDeathRelay>(Params);
	}
	return Relay;
}
