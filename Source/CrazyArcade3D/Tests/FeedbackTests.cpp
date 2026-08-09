// CA3DFeedback(사운드·이펙트 큐) 자동화 테스트.
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Gameplay.Feedback"로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다:
//   ① 에셋이 전부 미지정이어도 모든 큐 호출이 안전한가 (지금 프로젝트의 실제 상태다)
//   ② 미지정 경고가 큐당 한 번만 나가는가 (도배 방지)
//   ③ 큐가 룰셋의 **제 필드**를 읽는가 (10개짜리 switch 의 복붙 사고 방지)
//   ④ 릴레이 Multicast 가 CA3DFeedback::Play 를 지나는가 (재생 경로 단일화)
//   ⑤ "한 사건에 한 번" — 블록 파괴 20칸 = 큐 1회, 생존 상태 전이 = 전이당 1회
//
// **검증하지 못하는 것 (PIE·데디 exe 몫)**:
//   · 데디 서버 가드 — 에디터 프로세스에서는 IsRunningDedicatedServer() 가 항상 false 다.
//     PIE 데디 모드로도 재현되지 않는다 (CLAUDE.md 알려진 함정). 진짜 서버 exe 로만 확인된다.
//     여기서 대신 보장하는 것은 "가드를 통과하는 지점이 Play 하나뿐" 이라는 구조다 — ④ 가
//     릴레이 경로를, 나머지 항목이 호출부 경로를 각각 Play 계수기로 확인한다.
//   · 실제 소리·이펙트가 나는가, 감쇠·Concurrency 가 의도대로 걸리는가 — 전부 에셋 영역.
//   · 설치음 중복(예측 ↔ 서버 확정)은 원격 클라가 있어야 재현된다 — 리슨+클라 PIE 몫.

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Engine/World.h"
#include "Gameplay/CA3DFeedback.h"
#include "Gameplay/Character/CA3DCharacter.h"
#include "Gameplay/Character/StatusComponent.h"
#include "Framework/CA3DRuleSet.h"
#include "Voxel/VoxelWorld.h"
#include "Voxel/VoxelGrid.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "EngineUtils.h"

#if WITH_AUTOMATION_TESTS

namespace CA3DFeedbackTest
{
	constexpr int32 CueCount = static_cast<int32>(ECA3DCue::Count);

	// "BombPlace" 처럼 접두사 없는 짧은 이름. 룰셋 필드 이름(BombPlaceSound/BombPlaceFX)의 재료다.
	static FString ShortCueName(int32 Value)
	{
		FString Name = StaticEnum<ECA3DCue>()->GetNameStringByValue(Value);
		int32 Scope = INDEX_NONE;
		if (Name.FindLastChar(TEXT(':'), Scope))
		{
			Name = Name.RightChop(Scope + 1);
		}
		return Name;
	}

	// 그리드에서 Destructible 셀을 최대 MaxCount개 (VoxelWorldTests 의 같은 헬퍼).
	static TArray<FIntVector> FindDestructibleCells(const FVoxelGrid& Grid, int32 MaxCount)
	{
		TArray<FIntVector> Result;
		for (int32 Z = 0; Z < Grid.Size.Z && Result.Num() < MaxCount; ++Z)
		{
			for (int32 Y = 0; Y < Grid.Size.Y && Result.Num() < MaxCount; ++Y)
			{
				for (int32 X = 0; X < Grid.Size.X && Result.Num() < MaxCount; ++X)
				{
					const FIntVector Cell(X, Y, Z);
					if (Grid.Get(Cell) == EBlockType::Destructible)
					{
						Result.Add(Cell);
					}
				}
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFeedbackTest, "CrazyArcade3D.Gameplay.Feedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFeedbackTest::RunTest(const FString& Parameters)
{
	using namespace CA3DFeedbackTest;

	// 넷드라이버 없는 게임 월드 → SpawnActor 결과는 항상 HasAuthority()==true.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	// ─── ② 미지정 경고는 큐당 한 번 ──────────────────────────────────────────
	// ① 보다 먼저 한다 — ① 이 모든 큐를 한 번씩 지나면서 플래그를 전부 세우기 때문이다.
	CA3DFeedback::ResetMissingWarnings();

	for (int32 Index = 0; Index < CueCount; ++Index)
	{
		TestFalse(FString::Printf(TEXT("② 초기화 직후 미경고 — %s"), *ShortCueName(Index)),
			CA3DFeedback::HasWarnedMissing(static_cast<ECA3DCue>(Index)));
	}

	CA3DFeedback::Play(World, ECA3DCue::BombPlace, FVector::ZeroVector);
	TestTrue(TEXT("② 미지정 큐 1회 호출 → 경고 기록됨"),
		CA3DFeedback::HasWarnedMissing(ECA3DCue::BombPlace));

	// 같은 큐를 다시 불러도 플래그는 그대로 — 로그가 한 번만 나갔다는 뜻이다
	// (로그 자체와 이 플래그가 **같은 조건**을 쓴다: CA3DFeedback.cpp 의 GWarnedMissingCue).
	CA3DFeedback::Play(World, ECA3DCue::BombPlace, FVector::ZeroVector);
	TestTrue(TEXT("② 재호출 후에도 경고 플래그 유지 (재경고 없음)"),
		CA3DFeedback::HasWarnedMissing(ECA3DCue::BombPlace));

	// 플래그는 큐마다 독립이어야 한다 — 하나로 뭉치면 첫 큐 이후 전부 조용해져 진단 가치가 없다.
	TestFalse(TEXT("② 다른 큐는 아직 미경고 (큐별 독립 플래그)"),
		CA3DFeedback::HasWarnedMissing(ECA3DCue::Explosion));

	// ─── ① 전 큐 호출이 미지정 상태에서 안전 ──────────────────────────────────
	// 지금 프로젝트에는 사운드·나이아가라 에셋이 하나도 없다 — 그 상태가 기본 동작이어야 한다.
	CA3DFeedback::ResetPlayCount();
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 Index = 0; Index < CueCount; ++Index)
		{
			CA3DFeedback::Play(World, static_cast<ECA3DCue>(Index), FVector(100.0, 200.0, 300.0));
		}
	}
	TestEqual(TEXT("① 전 큐 2회씩 호출 — 크래시·에러 없이 전부 통과"),
		CA3DFeedback::GetPlayCount(), CueCount * 2);

	// 월드가 없어도 안전해야 한다 (정리 중 호출 경로).
	CA3DFeedback::ResetPlayCount();
	CA3DFeedback::Play(nullptr, ECA3DCue::Death, FVector::ZeroVector);
	TestEqual(TEXT("① 월드 null 은 조용한 no-op"), CA3DFeedback::GetPlayCount(), 0);

	// ─── ③ 큐 ↔ 룰셋 필드 매핑 ────────────────────────────────────────────────
	// 큐 이름에서 필드 이름을 만들어(리플렉션) 값을 넣고, ResolveCueAssets 가 **그 값**을
	// 돌려주는지 본다. switch 를 테스트에 베껴 쓰지 않으므로 복붙 사고를 실제로 잡는다.
	{
		UCA3DRuleSet* Rules = NewObject<UCA3DRuleSet>();
		TArray<USoundWave*> Sounds;
		Sounds.SetNum(CueCount);

		for (int32 Index = 0; Index < CueCount; ++Index)
		{
			const FString Short = ShortCueName(Index);

			FObjectProperty* SoundProp = CastField<FObjectProperty>(
				UCA3DRuleSet::StaticClass()->FindPropertyByName(FName(*(Short + TEXT("Sound")))));
			FObjectProperty* FXProp = CastField<FObjectProperty>(
				UCA3DRuleSet::StaticClass()->FindPropertyByName(FName(*(Short + TEXT("FX")))));

			if (!TestNotNull(FString::Printf(TEXT("③ 룰셋에 %sSound 필드 존재"), *Short), SoundProp)
				|| !TestNotNull(FString::Printf(TEXT("③ 룰셋에 %sFX 필드 존재"), *Short), FXProp))
			{
				continue;
			}

			// 나이아가라 쪽은 값 비교 대신 타입만 본다 — UNiagaraSystem 인스턴스를 손으로
			// 만들면 에디터 전용 초기화가 함께 돌아 테스트가 무거워진다. 이름 규약이 지켜지고
			// 타입이 맞으면 switch 가 짝지어 쓰는 두 줄 중 한쪽이 검증되는 것으로 충분하다.
			TestEqual(FString::Printf(TEXT("③ %sFX 타입이 UNiagaraSystem"), *Short),
				FXProp->PropertyClass->GetFName(), FName(TEXT("NiagaraSystem")));

			Sounds[Index] = NewObject<USoundWave>();
			SoundProp->SetObjectPropertyValue_InContainer(Rules, Sounds[Index]);
		}

		for (int32 Index = 0; Index < CueCount; ++Index)
		{
			USoundBase* OutSound = nullptr;
			UNiagaraSystem* OutFX = nullptr;
			CA3DFeedback::ResolveCueAssets(*Rules, static_cast<ECA3DCue>(Index), OutSound, OutFX);

			TestTrue(FString::Printf(TEXT("③ 큐 %s 가 제 필드를 읽는다"), *ShortCueName(Index)),
				OutSound != nullptr && OutSound == static_cast<USoundBase*>(Sounds[Index]));
			TestNull(FString::Printf(TEXT("③ 큐 %s 이펙트 미지정 → nullptr"), *ShortCueName(Index)),
				OutFX);
		}
	}

	// ─── ④ 릴레이 Multicast 가 Play 를 지난다 ─────────────────────────────────
	{
		UCA3DFeedbackSubsystem* Feedback = World->GetSubsystem<UCA3DFeedbackSubsystem>();
		if (!TestNotNull(TEXT("④ UCA3DFeedbackSubsystem 자동 생성"), Feedback))
		{
			World->DestroyWorld(false);
			return false;
		}

		ACA3DCueRelay* Relay = Feedback->ResolveRelay();
		if (!TestNotNull(TEXT("④ 서버에서 릴레이 lazy 스폰"), Relay))
		{
			World->DestroyWorld(false);
			return false;
		}
		TestTrue(TEXT("④ 릴레이가 복제 액터 (Multicast 가 클라에 도달하려면 필수)"),
			Relay->GetIsReplicated());
		TestTrue(TEXT("④ 릴레이가 bAlwaysRelevant (컬링으로 큐를 놓치지 않게)"),
			static_cast<bool>(Relay->bAlwaysRelevant));

		// 두 번째 호출은 캐시를 쓴다 — 사건마다 액터를 새로 만들면 릴레이가 무한히 늘어난다.
		TestTrue(TEXT("④ 릴레이 캐시 — 두 번째 호출도 같은 액터"), Feedback->ResolveRelay() == Relay);
		int32 RelayCount = 0;
		for (TActorIterator<ACA3DCueRelay> It(World); It; ++It)
		{
			++RelayCount;
		}
		TestEqual(TEXT("④ 월드에 릴레이는 하나뿐"), RelayCount, 1);

		CA3DFeedback::ResetPlayCount();
		Relay->MulticastCue_Implementation(ECA3DCue::ItemPickup, FVector(10.0, 20.0, 30.0));
		TestEqual(TEXT("④ Multicast 수신 = Play 정확히 1회 (자체 재생 경로 없음)"),
			CA3DFeedback::GetPlayCount(), 1);

		// ⚠️ **ServerBroadcast 의 검증 범위는 여기까지다.**
		// 자동화 테스트 월드에서는 RPC 래퍼를 불러도 엔진이 `_Implementation` 을 태우지 않는다
		// (2026-08-09 실측: Role=Authority · RemoteRole=SimulatedProxy · NetMode=Standalone 이라
		//  콜스페이스가 Local 인데도 본문이 안 돈다 — 넷드라이버 없는 월드의 ProcessEvent 사정).
		// 그래서 "우리 코드가 릴레이를 정확히 한 번 부르는가"만 센다. 그 뒤 디스패치는 엔진의
		// 일이고, 실제 도달(호스트가 두 번 듣지 않는가 포함)은 **리슨+클라 PIE 로만 닫힌다.**
		// 처음엔 PlayCount 로 검증하려다 이 사실에 걸렸고, 엔진 동작을 흉내 내는 대신
		// 관찰 지점을 우리 쪽으로 당겼다.
		CA3DFeedback::ResetPlayCount();
		CA3DFeedback::ResetRelayBroadcastCount();
		CA3DFeedback::ServerBroadcast(World, ECA3DCue::Kick, FVector::ZeroVector);
		TestEqual(TEXT("④ ServerBroadcast 1회 = 릴레이 Multicast 호출 1회"),
			CA3DFeedback::GetRelayBroadcastCount(), 1);

		// 클라에서 부르면 아무 일도 하지 않아야 한다 (릴레이는 서버 소유).
		CA3DFeedback::ResetRelayBroadcastCount();
		CA3DFeedback::ServerBroadcast(nullptr, ECA3DCue::Kick, FVector::ZeroVector);
		TestEqual(TEXT("④ World 없음 — 릴레이 호출 없음"), CA3DFeedback::GetRelayBroadcastCount(), 0);
	}

	// ─── ⑤a 블록 파괴: 20칸이 함께 부서져도 큐는 1회 ──────────────────────────
	{
		AVoxelWorld* VoxelWorld = World->SpawnActor<AVoxelWorld>();
		if (!TestNotNull(TEXT("⑤a AVoxelWorld 스폰"), VoxelWorld))
		{
			World->DestroyWorld(false);
			return false;
		}
		VoxelWorld->ServerInitFromSeed(1234u);

		// 월드 BeginPlay 는 테스트 월드에서 돌지 않는다 — 구독 배선을 직접 태운다
		// (StatusComponentTests 의 TryApplyMovementTuning 수동 호출과 같은 관례).
		UCA3DFeedbackSubsystem* Feedback = World->GetSubsystem<UCA3DFeedbackSubsystem>();
		Feedback->OnWorldBeginPlay(*World);
		TestTrue(TEXT("⑤a 구독 대상이 레벨의 AVoxelWorld"), Feedback->CachedVoxelWorld.Get() == VoxelWorld);
		TestTrue(TEXT("⑤a OnGridChanged 구독 성립"), Feedback->GridChangedHandle.IsValid());

		const TArray<FIntVector> Cells = FindDestructibleCells(VoxelWorld->GetGrid(), 20);
		if (!TestTrue(TEXT("⑤a 파괴 대상 셀 확보 (2칸 이상)"), Cells.Num() >= 2))
		{
			World->DestroyWorld(false);
			return false;
		}

		CA3DFeedback::ResetPlayCount();
		VoxelWorld->ServerDestroyBlocks(Cells);
		TestEqual(
			FString::Printf(TEXT("⑤a %d칸 파괴 = 블록 파괴 큐 1회 (셀마다가 아니다)"), Cells.Num()),
			CA3DFeedback::GetPlayCount(), 1);

		// 이미 비어 있는 셀만 넘기면 그리드가 바뀌지 않는다 → 사건이 없으므로 큐도 없다.
		CA3DFeedback::ResetPlayCount();
		VoxelWorld->ServerDestroyBlocks(Cells);
		TestEqual(TEXT("⑤a 같은 셀 재파괴 — 변경 없음이므로 큐 없음"),
			CA3DFeedback::GetPlayCount(), 0);

		// 빈 목록도 마찬가지 (ApplyDestruction 이 Num()==0 에서 브로드캐스트하지 않는다).
		CA3DFeedback::ResetPlayCount();
		VoxelWorld->ServerDestroyBlocks(TArray<FIntVector>());
		TestEqual(TEXT("⑤a 빈 파괴 목록 — 큐 없음"), CA3DFeedback::GetPlayCount(), 0);

		// 구독 해제까지 확인한다 — 남으면 죽은 월드의 서브시스템이 지형 알림을 계속 받는다.
		Feedback->Deinitialize();
		TestFalse(TEXT("⑤a Deinitialize 후 구독 해제"), Feedback->GridChangedHandle.IsValid());
	}

	// ─── ⑤b 생존 상태: 전이당 정확히 1회 ─────────────────────────────────────
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACA3DCharacter* Character = World->SpawnActor<ACA3DCharacter>(
			ACA3DCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		UStatusComponent* Status = Character ? Character->GetStatus() : nullptr;
		if (!TestNotNull(TEXT("⑤b StatusComponent 확보"), Status))
		{
			World->DestroyWorld(false);
			return false;
		}

		// 갇힘 (Alive → Trapped)
		CA3DFeedback::ResetPlayCount();
		Status->ServerTrap();
		TestEqual(TEXT("⑤b 갇힘 = 큐 1회"), CA3DFeedback::GetPlayCount(), 1);

		// 이미 갇혀 있으면 ServerTrap 이 되돌아간다 — 전이가 없으므로 큐도 없다.
		CA3DFeedback::ResetPlayCount();
		Status->ServerTrap();
		TestEqual(TEXT("⑤b 중복 갇힘 — 큐 없음"), CA3DFeedback::GetPlayCount(), 0);

		// 클라 경로(OnRep)를 서버에서 흉내 내도 같은 상태면 큐가 늘지 않는다 —
		// 리슨 호스트에서 Server* 와 OnRep 이 둘 다 도는 사고가 나도 두 번 들리지 않는다.
		CA3DFeedback::ResetPlayCount();
		Status->OnRep_Life();
		TestEqual(TEXT("⑤b 같은 상태로 OnRep 재진입 — 큐 없음"), CA3DFeedback::GetPlayCount(), 0);

		// 탈출 (Trapped → Alive)
		Status->bHasNeedle = true;
		CA3DFeedback::ResetPlayCount();
		Status->ServerEscape();
		TestEqual(TEXT("⑤b 니들 탈출 = 큐 1회"), CA3DFeedback::GetPlayCount(), 1);

		// 사망 (Alive → Dead)
		CA3DFeedback::ResetPlayCount();
		Status->ServerKill(EDeathCause::Water);
		TestEqual(TEXT("⑤b 사망 = 큐 1회"), CA3DFeedback::GetPlayCount(), 1);

		CA3DFeedback::ResetPlayCount();
		Status->ServerKill(EDeathCause::Fall);
		Status->OnRep_Life();
		TestEqual(TEXT("⑤b 중복 사망 통지·OnRep — 큐 없음"), CA3DFeedback::GetPlayCount(), 0);
	}

	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
