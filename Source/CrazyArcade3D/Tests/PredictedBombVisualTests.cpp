// APredictedBombVisual + 설치 로컬 예측 경로 자동화 테스트 (Task 17).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.PredictedBombVisual"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: 로컬 검증 각 분기(사망·셀 점유·개수 초과 — 실패 시
// 비주얼 획득 없음 = TryPlaceBombPredicted 가 RPC 를 보내지 않는 분기), 통과 시 비주얼 획득 +
// Cell 기록, ClientRejectBomb 의 셀 매칭 반납, 타이머 부재(불변식 3 — 시간이 지나도 혼자
// 아무 일도 안 함), 풀 반납→재획득 시 Cell 잔존 오염 없음, 리슨 호스트의 예측 생략.
// 실제 리플리케이션(Listen+클라, 지연 에뮬레이션)·ABomb 교체 겹침 여부는 PIE 검증 (체크리스트 17).
//
// ⚠️ 테스트 월드는 넷드라이버가 없어 HasAuthority()==true — TryPlaceBombPredicted 는 예측을
// 생략하는 리슨 호스트 분기를 탄다. 원격 클라의 로컬 검증·획득 로직은 내부 함수
// TryAcquirePredictedVisual 을 friend 로 직접 호출해 검증한다 (호출자는 이 함수가 false 면
// RPC 를 보내지 않으므로, false 검증 = RPC 생략 검증).
//
// 월드를 직접 생성하므로 BeginPlay 는 돌지 않는다 — 그리드는 friend 로 손구성(결정론 시나리오),
// CMC 튜닝은 TryApplyMovementTuning 직접 호출 (기존 테스트 관례). 타이머 검증은
// GFrameCounter 증가 + TimerManager 수동 Tick 패턴 (BombTests 관례).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Bomb/Bomb.h"
#include "Gameplay/Bomb/PredictedBombVisual.h"
#include "Gameplay/Bomb/WaterSegment.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Voxel/VoxelWorld.h"
#include "Framework/CA3DRuleSet.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPredictedBombVisualTest, "CrazyArcade3D.Gameplay.PredictedBombVisual",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// ⚠️ 헬퍼 이름에 Pbv 접두사 — UBT 가 테스트 .cpp 들을 한 번역 단위로 합치면
	// 무명 네임스페이스가 병합되므로, BombTests.cpp 의 동명 헬퍼와 충돌한다 (C2084).
	// 수정 중인 파일은 병합에서 빠지는 탓에 git 커밋 전에는 안 드러난다 —
	// 이름은 모듈 전체에서 고유해야 한다 (mds/build.md "번역 단위 병합 빌드" 절).

	// 수동 타이머 진행 — Pending 활성화 틱 + 만료 틱 (BombTests·StatusComponentTests 의 GFrameCounter 관례).
	void PbvAdvanceTimers(UWorld* World, float Seconds)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(KINDA_SMALL_NUMBER); // Pending → Active
		++GFrameCounter;
		World->GetTimerManager().Tick(Seconds);            // 만료
	}

	int32 CountPredictedVisuals(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<APredictedBombVisual> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	int32 PbvCountBombs(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ABomb> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	int32 PbvCountWaterSegments(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AWaterSegment> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

bool FPredictedBombVisualTest::RunTest(const FString& Parameters)
{
	// 넷드라이버 없는 게임 월드 → HasAuthority()==true → 룰셋은 기본(CDO) 폴백 경로.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	// UFUNCTION RPC(ServerPlaceBomb)는 AActor::ProcessEvent 를 타는데, ProcessEvent 는
	// AreActorsInitialized()==false 인 월드에서 이벤트를 조용히 버린다 — 액터 초기화만 켠다
	// (World->BeginPlay() 는 부르지 않아 BeginPlay 미실행 전제는 유지) — BombTests 관례.
	World->InitializeActorsForPlay(FURL());

	const UCA3DRuleSet* Rules = GetDefault<UCA3DRuleSet>();

	// ── 손그리드 9×9×4 — z=0 전체 Floor 바닥판 (구멍 없음 — 발판 거부는 BombTests 소관) ──
	AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
	if (!TestNotNull(TEXT("AVoxelWorld 스폰"), VoxelWorld))
	{
		World->DestroyWorld(false);
		return false;
	}
	VoxelWorld->CellSize = 100.f;
	VoxelWorld->Grid.Init(FIntVector(9, 9, 4));
	for (int32 X = 0; X < 9; ++X)
	{
		for (int32 Y = 0; Y < 9; ++Y)
		{
			VoxelWorld->Grid.Set(FIntVector(X, Y, 0), EBlockType::Floor);
		}
	}
	VoxelWorld->bGridInitialized = true;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACA3DCharacter* Owner = World->SpawnActor<ACA3DCharacter>(
		ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("설치자 캐릭터 스폰"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}
	Owner->TryApplyMovementTuning(); // VoxelWorld 캐시 — GetFootCell·설치 경로의 전제 (friend)
	UStatusComponent* Status = Owner->GetStatus();

	const float HalfHeight = 88.f; // 엔진 기본 캡슐 반높이 (ACA3DCharacter 생성자 주석)
	// 캡슐 바닥이 셀 (X,Y,Z) 중앙에 오도록 배치 — BombTests 와 동일 헬퍼.
	auto LocForFootCell = [HalfHeight](int32 X, int32 Y, int32 Z)
	{
		return FVector(X * 100.f + 50.f, Y * 100.f + 50.f, Z * 100.f + 50.f + HalfHeight);
	};
	Owner->SetActorLocation(LocForFootCell(4, 4, 1), false, nullptr, ETeleportType::TeleportPhysics);

	const FIntVector PlaceCell(4, 4, 1);

	// ─── 1. 로컬 검증 실패 분기 — 비주얼 획득 없음 = 호출자가 RPC 를 보내지 않는 분기 ───

	// ① 사망: Alive 가 아니면 거부.
	Status->LifeState = ELifeState::Dead;
	TestFalse(TEXT("① 사망: 로컬 검증 거부"), Owner->TryAcquirePredictedVisual(PlaceCell));
	TestEqual(TEXT("① 사망: 예측 목록 비어 있음"), Owner->PredictedBombVisuals.Num(), 0);
	TestEqual(TEXT("① 사망: 비주얼 액터 0개"), CountPredictedVisuals(World), 0);
	Status->LifeState = ELifeState::Alive;

	// ② 셀 점유(솔리드): Empty 가 아닌 칸(바닥판)은 거부.
	TestFalse(TEXT("② 솔리드 셀: 로컬 검증 거부"), Owner->TryAcquirePredictedVisual(FIntVector(4, 4, 0)));
	TestEqual(TEXT("② 솔리드 셀: 비주얼 액터 0개"), CountPredictedVisuals(World), 0);

	// ③ 개수 초과(확정분): ActiveBombCount 가 이미 상한 — 리슨 호스트 값 기준의 초과 거부.
	Status->ActiveBombCount = Status->MaxBombCount; // 기본 1
	TestFalse(TEXT("③ 개수 초과(확정분): 로컬 검증 거부"), Owner->TryAcquirePredictedVisual(PlaceCell));
	TestEqual(TEXT("③ 개수 초과: 비주얼 액터 0개"), CountPredictedVisuals(World), 0);
	Status->ActiveBombCount = 0;

	// ─── 2. 통과 — 비주얼 1개 획득 + Cell 기록 ───
	TestTrue(TEXT("④ 통과: 로컬 검증 성공"), Owner->TryAcquirePredictedVisual(PlaceCell));
	TestEqual(TEXT("④ 통과: 예측 목록 1개"), Owner->PredictedBombVisuals.Num(), 1);
	TestEqual(TEXT("④ 통과: 비주얼 액터 1개 획득"), CountPredictedVisuals(World), 1);

	APredictedBombVisual* Visual =
		Owner->PredictedBombVisuals.Num() > 0 ? Owner->PredictedBombVisuals[0].Get() : nullptr;
	if (!TestNotNull(TEXT("④ 통과: 비주얼 포인터 유효"), Visual))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("④ 통과: Cell 매칭 키 기록"), Visual->Cell, PlaceCell);
	TestFalse(TEXT("④ 통과: 표시 상태 (숨김 아님)"), Visual->IsHidden());

	// ⑤ 같은 셀 연타: 이미 예측이 떠 있으면 거부 (서버 응답 대기 중 중복 방지).
	TestFalse(TEXT("⑤ 같은 셀 연타: 거부"), Owner->TryAcquirePredictedVisual(PlaceCell));
	TestEqual(TEXT("⑤ 같은 셀 연타: 목록 그대로 1개"), Owner->PredictedBombVisuals.Num(), 1);

	// ⑥ 개수 초과(예측치): ActiveBombCount 0 + 예측 1 >= MaxBombCount 1 → 다른 셀도 거부.
	TestFalse(TEXT("⑥ 개수 초과(예측치): 거부"), Owner->TryAcquirePredictedVisual(FIntVector(5, 4, 1)));
	TestEqual(TEXT("⑥ 개수 초과(예측치): 목록 그대로 1개"), Owner->PredictedBombVisuals.Num(), 1);

	// ─── 3. 타이머 부재 (불변식 3 실증) — 퓨즈 2배 시간이 지나도 혼자 아무 일도 안 한다 ───
	TestFalse(TEXT("⑦ 타이머 없음: 틱 자체가 꺼진 클래스 (bCanEverTick=false)"),
		Visual->PrimaryActorTick.bCanEverTick);
	PbvAdvanceTimers(World, Rules->BombFuseTime * 2.f); // 진짜 폭탄이면 이미 폭발했을 시간
	TestTrue(TEXT("⑦ 타이머 없음: 시간 경과 후에도 비주얼 생존 (혼자 안 터짐)"), IsValid(Visual));
	TestFalse(TEXT("⑦ 타이머 없음: 여전히 표시 중"), Visual->IsHidden());
	TestEqual(TEXT("⑦ 타이머 없음: 예측 목록 불변"), Owner->PredictedBombVisuals.Num(), 1);
	TestEqual(TEXT("⑦ 타이머 없음: 폭발 부산물(물줄기) 0개"), PbvCountWaterSegments(World), 0);
	TestEqual(TEXT("⑦ 타이머 없음: 폭탄 액터 0개 (판정 개입 없음)"), PbvCountBombs(World), 0);

	// ─── 4. 서버 거부 — ClientRejectBomb 이 같은 셀 비주얼만 반납 ───
	Owner->ClientRejectBomb_Implementation(FIntVector(8, 8, 1)); // 다른 셀 거부는 no-op
	TestEqual(TEXT("⑧ 다른 셀 거부: 목록 그대로 1개"), Owner->PredictedBombVisuals.Num(), 1);

	Owner->ClientRejectBomb_Implementation(PlaceCell);
	TestEqual(TEXT("⑧ 같은 셀 거부: 목록에서 제거"), Owner->PredictedBombVisuals.Num(), 0);
	TestTrue(TEXT("⑧ 같은 셀 거부: 풀 반납 (파괴 아님 — 재사용 대기)"), IsValid(Visual));
	TestTrue(TEXT("⑧ 같은 셀 거부: 숨김 상태로 보관"), Visual->IsHidden());

	// ─── 5. 풀 재획득 — Cell 잔존 상태 오염 없음 ───
	const FIntVector NewCell(5, 5, 1);
	TestTrue(TEXT("⑨ 재획득: 로컬 검증 성공"), Owner->TryAcquirePredictedVisual(NewCell));
	if (TestEqual(TEXT("⑨ 재획득: 목록 1개"), Owner->PredictedBombVisuals.Num(), 1))
	{
		APredictedBombVisual* Reused = Owner->PredictedBombVisuals[0].Get();
		TestTrue(TEXT("⑨ 재획득: 풀이 같은 인스턴스 재사용"), Reused == Visual);
		TestEqual(TEXT("⑨ 재획득: Cell 을 새 값으로 덮어씀 (잔존값 오염 없음)"), Reused->Cell, NewCell);
		TestFalse(TEXT("⑨ 재획득: 표시 복원"), Reused->IsHidden());
	}

	// 옛 셀로 반납 시도 — Cell 이 갱신됐으므로 매칭 실패가 정답 (잔존값이 새 나가면 여기서 지워진다).
	Owner->ReleasePredictedVisualAt(PlaceCell);
	TestEqual(TEXT("⑩ 옛 셀 반납: 매칭 실패 — 목록 그대로 1개"), Owner->PredictedBombVisuals.Num(), 1);
	Owner->ReleasePredictedVisualAt(NewCell);
	TestEqual(TEXT("⑩ 새 셀 반납: 목록 비움"), Owner->PredictedBombVisuals.Num(), 0);
	TestTrue(TEXT("⑩ 새 셀 반납: 숨김 상태로 보관"), Visual->IsHidden());

	// ─── 6. 리슨 호스트(권한 있음) — 예측 생략, 바로 서버 경로 (겹침 방지) ───
	Owner->TryPlaceBombPredicted();
	TestEqual(TEXT("⑪ 리슨 호스트: 예측 비주얼 생성 안 함"), Owner->PredictedBombVisuals.Num(), 0);
	TestEqual(TEXT("⑪ 리슨 호스트: ServerPlaceBomb 직행 — 진짜 폭탄 1개"), PbvCountBombs(World), 1);
	TestEqual(TEXT("⑪ 리슨 호스트: ActiveBombCount 점유"), Status->ActiveBombCount, 1);

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
