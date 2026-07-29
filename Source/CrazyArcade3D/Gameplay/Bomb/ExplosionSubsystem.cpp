#include "Gameplay/Bomb/ExplosionSubsystem.h"

#include "CrazyArcade3D.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/ExplosionFXRelay.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"   // Gameplay→Framework 는 .cpp 에서만 include (폴더 의존 규칙)
#include "Framework/CA3DGameState.h" // 룰셋 출처(복제된 에셋 포인터) — .cpp 에서만
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
	// 룰셋 해석 — ChainStepDelay·bFloorDestructible 의 출처. 서버 전용 경로에서만 쓰므로
	// GameState 복제 포인터 → CDO 폴백이면 충분하다 (StatusComponent::ResolveRules 와 동일 관례).
	const UCA3DRuleSet* ResolveChainRules(const UWorld* World)
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

FExplosionResult UExplosionSubsystem::Propagate(
	const FVoxelGrid& Grid, const FIntVector& Origin, int32 Range,
	bool bFloorDestructible, const TArray<FIntVector>& BombCells)
{
	// 순수 함수 (불변식 2): 입력만 읽고 결과만 반환한다. 멤버·전역·룰셋 접근 금지.
	// 정수 연산과 고정 순회 순서만 사용 — 같은 입력이면 배열 순서까지 같은 출력 (불변식 4 준용).

	FExplosionResult Result;

	// 최악(전 방향 Empty) 크기 = 원점 1 + 6방향 × Range. Range<=0 이어도 안전.
	Result.WaterCells.Reserve(1 + 6 * FMath::Max(Range, 0));

	// 원점 칸. 원점의 폭탄은 "지금 터지는 폭탄" 자신이므로 연쇄 판정하지 않는다 (명세 의사코드).
	Result.WaterCells.Add(Origin);

	// 방향 순서 고정 [+X,-X,+Y,-Y,+Z,-Z] — 결정론의 일부. 바꾸면 시드 재현·프리뷰 대조가 깨진다.
	static const FIntVector Directions[6] =
	{
		FIntVector( 1,  0,  0),
		FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0),
		FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1),
		FIntVector( 0,  0, -1),
	};

	for (const FIntVector& Dir : Directions)
	{
		for (int32 Step = 1; Step <= Range; ++Step)
		{
			const FIntVector Cell = Origin + Dir * Step;

			// 범위 밖은 Grid.Get 이 Empty 를 반환 — 경계 검사 없이 자연스럽게 통과한다
			// (FVoxelGrid 설계 의도). 맵 외곽은 생성기가 Immortal 벽으로 막는다.
			bool bStopThisDirection = false;
			switch (Grid.Get(Cell))
			{
			case EBlockType::Immortal:
				// 막힘 — 셀 미포함.
				bStopThisDirection = true;
				break;

			case EBlockType::Destructible:
				// 부수고 멈춤.
				Result.BrokenCells.Add(Cell);
				bStopThisDirection = true;
				break;

			case EBlockType::Floor:
				// 바닥 규칙(룰셋 bFloorDestructible)에 따라 부수되, 어느 쪽이든 멈춤.
				if (bFloorDestructible)
				{
					Result.BrokenCells.Add(Cell);
				}
				bStopThisDirection = true;
				break;

			case EBlockType::Empty:
			default:
				// 물줄기 통과. 폭탄이 놓인 칸이면 연쇄 표시 — 전파는 계속 (GDD 2.2).
				Result.WaterCells.Add(Cell);
				if (BombCells.Contains(Cell))
				{
					Result.ChainedCells.Add(Cell);
				}
				break;
			}

			if (bStopThisDirection)
			{
				break;
			}
		}
	}

	return Result;
}

// ─── 서버 전용: 연쇄 스케줄링 (Task 16) ─────────────────────────────────────

void UExplosionSubsystem::RequestDetonate(ABomb* Bomb)
{
	if (!IsValid(Bomb) || !Bomb->HasAuthority()) return; // 불변식 5 — 연쇄는 100% 서버 소유

	PendingChain.AddUnique(Bomb); // 호출 전 bDetonated 가드가 서 있지만 이중 안전망으로 AddUnique

	if (bProcessingStep)
	{
		return; // 단계 처리 중 유발된 연쇄 — 단계 종료 시 ChainStepDelay 타이머가 예약된다
	}

	if (GetWorld()->GetTimerManager().IsTimerActive(ChainTimer))
	{
		return; // 이미 다음 단계가 예약됨 — 그 단계에 합류
	}

	// 큐가 놀고 있었다 → 첫 단계는 즉시 처리 (퓨즈 만료 시각이 정확히 폭발 시각이 된다).
	// 이후 연쇄 단계만 ChainStepDelay 간격 — 프레임 분산 + "촤르륵" (GDD 7.3).
	ProcessChainStep();
}

void UExplosionSubsystem::ProcessChainStep()
{
	if (bProcessingStep)
	{
		return; // 재진입 가드 (정상 흐름에선 도달 없음 — 계약 안전망)
	}

	// 이번 단계 폭탄들을 꺼낸다 — 처리 중 유발되는 연쇄는 새 PendingChain 에 쌓인다.
	TArray<TObjectPtr<ABomb>> StepBombs = MoveTemp(PendingChain);
	PendingChain.Reset();
	if (StepBombs.Num() == 0)
	{
		return;
	}

	AVoxelWorld* VoxelWorld = ResolveVoxelWorld();
	if (!VoxelWorld)
	{
		UE_LOG(LogCA3D, Error, TEXT("UExplosionSubsystem: 월드에 AVoxelWorld 없음 — 폭발 처리 불가"));
		return;
	}

	const UCA3DRuleSet* Rules = ResolveChainRules(GetWorld());

	// 이번 단계의 모든 폭발은 같은 그리드 스냅샷·같은 폭탄 셀 목록 기준 — 단계 내 순서 무관 결정론.
	const TArray<FIntVector> BombCells = GetActiveBombCellsSorted();

	TArray<FIntVector> StepWater;
	TArray<FIntVector> StepBroken;

	bProcessingStep = true;

	// ── ① 단계 폭탄 각각 Propagate + ⑥ 연쇄 셀 → 폭탄 해석 ──
	// ChainedBombs 를 FExplosionResult 에 싣지 않고 여기서 바로 레지스트리로 해석한다 —
	// 결과 구조체는 순수 셀 데이터로 유지(ExplosionTypes.h 의 보류 결정), 해석 결과는
	// ServerForceDetonate → RequestDetonate 를 거쳐 곧장 다음 단계 PendingChain 으로 들어간다.
	for (const TObjectPtr<ABomb>& BombPtr : StepBombs)
	{
		ABomb* Bomb = BombPtr.Get();
		if (!IsValid(Bomb))
		{
			continue; // 단계 대기 중 파괴된 폭탄 (매치 정리 등) — 건너뜀
		}

		const FExplosionResult Result = Propagate(
			VoxelWorld->GetGrid(), Bomb->GetCell(), Bomb->GetRange(),
			Rules->bFloorDestructible, BombCells);

		for (const FIntVector& Broken : Result.BrokenCells)
		{
			StepBroken.AddUnique(Broken); // 같은 단계의 폭발끼리 겹칠 수 있다 — 중복 파괴 방지
		}
		for (const FIntVector& Water : Result.WaterCells)
		{
			StepWater.AddUnique(Water);
		}

		for (const FIntVector& ChainedCell : Result.ChainedCells)
		{
			if (ABomb* Chained = FindBombAt(ChainedCell))
			{
				// 이미 이번 단계에 든 폭탄이면 bDetonated 가드가 무시한다 (중복 폭발 방지).
				// bProcessingStep 이 서 있어 RequestDetonate 는 큐 적재만 한다 (즉시 처리 없음).
				Chained->ServerForceDetonate();
			}
		}
	}

	// ── ② 블록 파괴 — 단일 경로 (불변식 1): ServerDestroyBlocks → ApplyDestruction + Multicast ──
	if (StepBroken.Num() > 0)
	{
		VoxelWorld->ServerDestroyBlocks(StepBroken);
	}

	// ── ③ 물줄기 셀 Multicast → 클라 풀 FX (소유 액터 선택 근거는 ExplosionFXRelay.h) ──
	if (AExplosionFXRelay* Relay = ResolveFXRelay())
	{
		Relay->MulticastWaterCells(StepWater);
	}

	// ── ④ 피격 — WaterCells 안의 캐릭터를 발밑 셀 기준으로 판정 (GDD 2.3 "발판만이 안전하다").
	// 제자리 점프는 발밑 셀이 그대로라 피격, 다른 발판에 올라가면 셀이 달라져 회피.
	// 순회 순서는 판정 결과에 영향 없다 (각 캐릭터 독립 — 불변식 4의 순서 민감 영역 아님).
	for (TActorIterator<ACA3DCharacter> It(GetWorld()); It; ++It)
	{
		ACA3DCharacter* Character = *It;
		UStatusComponent* Status = Character ? Character->GetStatus() : nullptr;
		if (Status && Status->LifeState == ELifeState::Alive && StepWater.Contains(Character->GetFootCell()))
		{
			Status->ServerTrap();
		}
	}

	// ── ⑤ 아이템 소멸 — TODO(Task 23): WaterCells 안의 아이템 픽업 제거 ──

	// ── 이번 단계 폭탄 정리: 소유자 슬롯 반환 + 파괴 (풀링 금지 — Destroy 로만 수명 관리) ──
	for (const TObjectPtr<ABomb>& BombPtr : StepBombs)
	{
		ABomb* Bomb = BombPtr.Get();
		if (IsValid(Bomb))
		{
			Bomb->ServerReleaseSlot();
			Bomb->Destroy();
		}
	}

	bProcessingStep = false;

	UE_LOG(LogCA3D, Log, TEXT("UExplosionSubsystem: 연쇄 단계 처리 — 폭탄 %d / 물줄기 %d칸 / 파괴 %d칸 / 다음 단계 %d"),
		StepBombs.Num(), StepWater.Num(), StepBroken.Num(), PendingChain.Num());

	// ── 다음 단계 예약 — ChainStepDelay(룰셋) 간격 프레임 분산 (GDD 7.3) ──
	if (PendingChain.Num() > 0)
	{
		GetWorld()->GetTimerManager().SetTimer(ChainTimer,
			FTimerDelegate::CreateUObject(this, &UExplosionSubsystem::ProcessChainStep),
			FMath::Max(Rules->ChainStepDelay, KINDA_SMALL_NUMBER), false);
	}
}

// ─── 서버 전용: 활성 폭탄 레지스트리 ────────────────────────────────────────

void UExplosionSubsystem::RegisterBomb(ABomb* Bomb)
{
	if (!IsValid(Bomb) || !Bomb->HasAuthority()) return; // 불변식 5 — 레지스트리는 서버 전용

	ActiveBombs.AddUnique(Bomb);
}

void UExplosionSubsystem::UnregisterBomb(ABomb* Bomb)
{
	// EndPlay(월드 정리 포함)에서 불린다 — 권한 가드 없이 제거만 (이미 없으면 no-op).
	ActiveBombs.Remove(Bomb);
}

ABomb* UExplosionSubsystem::FindBombAt(const FIntVector& Cell) const
{
	for (const TObjectPtr<ABomb>& BombPtr : ActiveBombs)
	{
		ABomb* Bomb = BombPtr.Get();
		if (IsValid(Bomb) && Bomb->GetCell() == Cell)
		{
			return Bomb;
		}
	}
	return nullptr;
}

TArray<FIntVector> UExplosionSubsystem::GetActiveBombCellsSorted() const
{
	TArray<FIntVector> Cells;
	Cells.Reserve(ActiveBombs.Num());
	for (const TObjectPtr<ABomb>& BombPtr : ActiveBombs)
	{
		ABomb* Bomb = BombPtr.Get();
		if (IsValid(Bomb))
		{
			Cells.Add(Bomb->GetCell());
		}
	}

	// 좌표 사전순(X→Y→Z) 정렬 — 등록 순서(서버 로컬)를 지우고 결정론적 입력을 만든다 (불변식 4 준용).
	Cells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.X != B.X) return A.X < B.X;
		if (A.Y != B.Y) return A.Y < B.Y;
		return A.Z < B.Z;
	});
	return Cells;
}

// ─── 내부 ────────────────────────────────────────────────────────────────────

AVoxelWorld* UExplosionSubsystem::ResolveVoxelWorld()
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

AExplosionFXRelay* UExplosionSubsystem::ResolveFXRelay()
{
	if (!IsValid(FXRelay))
	{
		// 서버에서 lazy 스폰 — bAlwaysRelevant 복제 액터라 전 클라에 존재하게 된다.
		// 스폰 시점이 첫 폭발이어도 Reliable Multicast 는 스폰 데이터와 함께 도착한다.
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient; // 레벨 저장 대상이 아니다
		FXRelay = GetWorld()->SpawnActor<AExplosionFXRelay>(Params);
	}
	return FXRelay;
}
