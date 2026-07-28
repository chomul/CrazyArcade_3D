// UPoolSubsystem 자동화 테스트 (Task 14).
// 에디터 세션 프론트엔드(Automation 탭) 또는
// -ExecCmds="Automation RunTests CrazyArcade3D.Core.PoolSubsystem"로 실행.
//
// 임시 테스트 액터는 Tests/PoolSubsystemTests.h 의 ACA3DPoolTestActor.
// stat unit 프레임 타임 계측은 PIE 영역이라 여기서 다루지 않는다 (체크리스트에 미검증으로 남김).

#include "Tests/PoolSubsystemTests.h" // UHT 규칙 — UCLASS를 정의한 짝 헤더가 첫 include 여야 한다
#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Core/PoolSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if WITH_AUTOMATION_TESTS

namespace CA3DPoolTest
{
	// 월드에 존재하는 테스트 액터 총수 — 누수(초과 스폰) 검출의 기준값.
	static int32 CountTestActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ACA3DPoolTestActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoolSubsystemTest, "CrazyArcade3D.Core.PoolSubsystem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPoolSubsystemTest::RunTest(const FString& Parameters)
{
	using namespace CA3DPoolTest;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("테스트 월드 생성"), World))
	{
		return false;
	}

	UPoolSubsystem* Pool = World->GetSubsystem<UPoolSubsystem>();
	if (!TestNotNull(TEXT("UPoolSubsystem 자동 생성"), Pool))
	{
		World->DestroyWorld(false);
		return false;
	}

	const TSubclassOf<AActor> TestClass = ACA3DPoolTestActor::StaticClass();

	// ─── Prewarm: 선스폰 개수 + 전부 Release 상태(숨김/컬리전 off/틱 off) + 콜백 1회 ───
	constexpr int32 PrewarmCount = 8;
	Pool->Prewarm(TestClass, PrewarmCount);
	TestEqual(TEXT("Prewarm 후 액터 총수"), CountTestActors(World), PrewarmCount);

	for (TActorIterator<ACA3DPoolTestActor> It(World); It; ++It)
	{
		TestTrue(TEXT("Prewarm 액터 숨김"), It->IsHidden());
		TestFalse(TEXT("Prewarm 액터 컬리전 off"), It->GetActorEnableCollision());
		TestFalse(TEXT("Prewarm 액터 틱 off"), It->IsActorTickEnabled());
		TestEqual(TEXT("Prewarm 시 OnReleasedToPool 1회"), It->ReleasedCount, 1);
		TestEqual(TEXT("Prewarm 시 OnAcquiredFromPool 0회"), It->AcquiredCount, 0);
	}

	// ─── Acquire 1개: 신규 스폰 없이 프리 리스트에서 + 상태 복원 + 콜백 순서 ───
	const FVector AcquireLoc(100.0, 200.0, 300.0);
	ACA3DPoolTestActor* Acquired = Cast<ACA3DPoolTestActor>(
		Pool->Acquire(TestClass, FTransform(AcquireLoc)));
	if (!TestNotNull(TEXT("Acquire 반환"), Acquired))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("Prewarm 후 첫 Acquire — 신규 스폰 없음 (액터 총수 불변)"),
		CountTestActors(World), PrewarmCount);
	TestFalse(TEXT("Acquire 후 표시됨"), Acquired->IsHidden());
	TestTrue(TEXT("Acquire 후 컬리전 on"), Acquired->GetActorEnableCollision());
	TestTrue(TEXT("Acquire 후 틱 on"), Acquired->IsActorTickEnabled());
	TestTrue(TEXT("Acquire 위치 세팅됨"), Acquired->GetActorLocation().Equals(AcquireLoc));
	TestEqual(TEXT("OnAcquiredFromPool 1회"), Acquired->AcquiredCount, 1);
	TestFalse(TEXT("Acquire 콜백 시점에 이미 표시 상태 (복원 → 콜백 순서)"),
		Acquired->bHiddenAtLastAcquire);
	TestEqual(TEXT("마지막 콜백 == Acquired"), Acquired->LastCallback, FName(TEXT("Acquired")));

	// ─── Release: 콜백 → 비활성화 순서 + Release 상태 ───
	Pool->Release(Acquired);
	TestTrue(TEXT("Release 후 숨김"), Acquired->IsHidden());
	TestFalse(TEXT("Release 후 컬리전 off"), Acquired->GetActorEnableCollision());
	TestFalse(TEXT("Release 후 틱 off"), Acquired->IsActorTickEnabled());
	TestEqual(TEXT("OnReleasedToPool 누적 2회 (Prewarm 1 + Release 1)"), Acquired->ReleasedCount, 2);
	TestFalse(TEXT("Release 콜백 시점엔 아직 숨기기 전 (콜백 → 비활성화 순서)"),
		Acquired->bHiddenAtLastRelease);
	TestEqual(TEXT("마지막 콜백 == Released"), Acquired->LastCallback, FName(TEXT("Released")));

	// ─── 스트레스: 200개 Acquire → 전부 Release → 재획득, 5회 반복 — 누수 없음 ───
	constexpr int32 StressCount = 200;
	constexpr int32 Iterations = 5;
	TArray<AActor*> Held;
	Held.Reserve(StressCount);

	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		Held.Reset();
		for (int32 Index = 0; Index < StressCount; ++Index)
		{
			AActor* Actor = Pool->Acquire(TestClass, FTransform::Identity);
			TestNotNull(TEXT("스트레스 Acquire 반환"), Actor);
			Held.Add(Actor);
		}

		// 첫 반복은 부족분(200-8)만 신규 스폰, 이후 반복은 전량 재사용 — 총수는 항상 200.
		TestEqual(*FString::Printf(TEXT("반복 %d: Acquire 후 액터 총수 == %d"), Iter, StressCount),
			CountTestActors(World), StressCount);

		// 같은 액터가 중복 대여되지 않았는가.
		const TSet<AActor*> Unique(Held);
		TestEqual(*FString::Printf(TEXT("반복 %d: 중복 획득 없음"), Iter),
			Unique.Num(), StressCount);

		for (AActor* Actor : Held)
		{
			if (Actor) // 스폰 실패 시 위 TestNotNull이 이미 실패 처리 — ensure 소음만 피한다.
			{
				Pool->Release(Actor);
			}
		}
		TestEqual(*FString::Printf(TEXT("반복 %d: Release 후 액터 총수 불변 (누수 없음)"), Iter),
			CountTestActors(World), StressCount);
	}

	// ─── 콜백 쌍 불변식: 전부 반납된 시점에 ΣReleased == ΣAcquired + Prewarm 수 ───
	// (Prewarm 액터는 Released가 1회 앞서고, Acquire에서 신규 스폰된 액터는 쌍이 맞는다.)
	{
		int32 TotalAcquired = 0;
		int32 TotalReleased = 0;
		for (TActorIterator<ACA3DPoolTestActor> It(World); It; ++It)
		{
			TotalAcquired += It->AcquiredCount;
			TotalReleased += It->ReleasedCount;
			TestTrue(TEXT("반납 상태 액터는 숨김"), It->IsHidden());
			TestFalse(TEXT("반납 상태 액터는 컬리전 off"), It->GetActorEnableCollision());
			TestFalse(TEXT("반납 상태 액터는 틱 off"), It->IsActorTickEnabled());
		}
		TestEqual(TEXT("ΣOnReleasedToPool == ΣOnAcquiredFromPool + Prewarm 수"),
			TotalReleased, TotalAcquired + PrewarmCount);
	}

	UE_LOG(LogCA3D, Display,
		TEXT("PoolSubsystem 스트레스: Prewarm %d, %d개 Acquire→Release x %d회 반복, 최종 액터 총수 %d"),
		PrewarmCount, StressCount, Iterations, CountTestActors(World));

	// ─── 정리 ───
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
