#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/NetSerialization.h" // FVector_NetQuantize — Multicast 인자 타입
#include "CA3DFeedback.generated.h"

class UCA3DRuleSet;
class USoundBase;
class UNiagaraSystem;
class AVoxelWorld;

// ─────────────────────────────────────────────────────────────────────────────
// 소리·이펙트의 **단일 재생 경로** (아트/사운드 패스).
//
// 이 파일에 세 가지가 함께 있는 이유는 SuddenDeathSubsystem.h 와 같다 — 셋 다 "한 사건에
// 소리와 이펙트를 한 번 낸다" 하나를 위해서만 존재하고 서로 외에는 아무도 쓰지 않는다:
//   ECA3DCue / CA3DFeedback  — 큐 정의 + 재생 (데디 가드가 사는 유일한 곳)
//   ACA3DCueRelay            — 클라 도달 경로가 없는 큐를 위한 Multicast 소유 액터
//   UCA3DFeedbackSubsystem   — AVoxelWorld::OnGridChanged 구독자 + 릴레이 캐시
//
// **Voxel 은 이 파일을 모른다.** "블록 파괴 소리" 를 AVoxelWorld::ApplyDestruction 안에 넣으면
// 지형이 사운드·룰셋을 알게 되어 폴더 의존 규칙(Voxel→없음)이 무너진다. 그래서 지형은
// OnGridChanged 로 "무엇이 바뀌었다" 만 알리고, 소리를 낼지는 Gameplay 쪽이 정한다.
// ─────────────────────────────────────────────────────────────────────────────

// 재생 단위는 "에셋" 이 아니라 **사건**이다. 큐 하나가 소리와 이펙트를 함께 들고 있어
// 호출 한 번으로 둘 다 나간다 — 한 사건에 소리와 이펙트가 따로 노는 사고를 구조적으로 막는다.
UENUM()
enum class ECA3DCue : uint8
{
	BombPlace,        // 폭탄이 눈앞에 생긴 순간 (설치자는 예측 비주얼, 남은 서버 확정)
	Explosion,        // 물줄기가 퍼지는 순간 — 연쇄 1단계당 1회 (셀마다가 아니다)
	BlockBreak,       // 블록이 부서진 순간 — 파괴 1묶음당 1회 (20칸이 함께 부서져도 1회)
	ItemPickup,       // 아이템 획득
	Trapped,          // 물방울에 갇힘
	Escape,           // 니들로 탈출
	Death,            // 사망
	Kick,             // 폭탄을 차 보낸 순간
	SuddenDeathWarn,  // 서든데스 낙하 예고 — 웨이브당 1회
	MatchEnd,         // 매치 종료 (우승·무승부 공통)

	Count UMETA(Hidden)
};

namespace CA3DFeedback
{
	// 큐 하나를 낸다 — 소리와 이펙트를 함께.
	//
	// ⚠️ **데디 서버 가드는 이 함수 최상단 한 곳에만 있다.** 호출부마다 흩어 놓으면 언젠가
	// 하나가 빠지고, 그 빠진 하나는 데디 exe 로만 잡히는 종류의 버그가 된다 (CLAUDE.md 함정 —
	// PIE 데디는 에디터 프로세스라 IsRunningDedicatedServer() 가 false).
	//
	// 에셋이 비어 있으면 **조용히 no-op** 이다 — 지금처럼 전부 미지정인 상태에서 로그가
	// 도배되면 안 된다. 대신 미지정 큐를 큐당 처음 한 번만 Verbose 로 남겨 진단 가치를 지킨다.
	//
	// Location 은 3D 음향(GDD 5장)의 위치다. 매치 종료처럼 위치가 없는 큐는 어디를 넘겨도
	// 상관없다 — 감쇠(Attenuation)를 지정하지 않은 사운드 에셋은 2D 로 재생된다.
	// 감쇠·동시 재생 상한은 **에셋 쪽 일**이다 (GDD 7.4 의 "동종 사운드 최대 4~5개" 는
	// Sound Concurrency 에셋이 처리한다 — C++ 로 또 세면 두 벌이 된다).
	void Play(const UWorld* World, ECA3DCue Cue, const FVector& Location);

	// 서버 → 전 클라. **클라에 도달하는 자연 경로가 없는 큐 전용** (ItemPickup·Kick·MatchEnd).
	// 리슨 호스트에서는 Multicast 가 로컬에서도 실행되므로 호스트도 정확히 한 번 듣는다.
	// 클라에서 부르면 no-op — 이 함수는 서버 경로에서만 의미가 있다.
	void ServerBroadcast(UWorld* World, ECA3DCue Cue, const FVector& Location);

	// 큐 → 룰셋 필드 조회 (순수 함수 — 부작용 0). Play 가 쓰는 **유일한** 매핑이라
	// 자동화 테스트가 같은 함수로 "큐마다 제 필드를 읽는가" 를 검증한다.
	void ResolveCueAssets(const UCA3DRuleSet& Rules, ECA3DCue Cue,
	                       USoundBase*& OutSound, UNiagaraSystem*& OutFX);

	// 미지정 경고 1회 제한의 상태 — 진단·테스트용.
	void ResetMissingWarnings();
	bool HasWarnedMissing(ECA3DCue Cue);

#if !UE_BUILD_SHIPPING
	// "한 사건에 한 번" 을 자동화 테스트가 셀 수 있게 하는 계수기 (에셋 유무와 무관하게 증가).
	// 데디 가드에 걸린 호출은 세지 않는다 — 그것이 곧 가드가 도는지의 관찰 지점이다.
	int32 GetPlayCount();
	void ResetPlayCount();

	// ServerBroadcast 가 **릴레이까지 도달해 Multicast 를 부른** 횟수.
	//
	// PlayCount 와 따로 세는 이유: 자동화 테스트 월드에서는 RPC 래퍼를 불러도 엔진이
	// `_Implementation` 을 태우지 않는다 (2026-08-09 실측: Role=Authority · NetMode=Standalone ·
	// 콜스페이스 Local 인데도 본문이 안 돈다 — 넷드라이버 없는 월드의 ProcessEvent 사정이다).
	// 그래서 여기서 검증할 수 있는 것은 **우리 코드가 정확히 한 번 릴레이를 부르는가**까지고,
	// 그 뒤 디스패치는 엔진의 일이다. 실제 도달은 리슨+클라 PIE 로만 닫힌다.
	int32 GetRelayBroadcastCount();
	void ResetRelayBroadcastCount();
#endif
}

// 클라 도달 경로가 없는 큐를 위한 Multicast 소유 액터.
//
// Multicast 는 액터에만 가능한데 필요한 지점(AItemPickup·ABomb·ACA3DGameMode)은 각자
// 사정이 있다 — 아이템·폭탄은 그 사건 직후 Destroy 되므로 자기 액터의 Reliable RPC 전송이
// 보장되지 않고(AExplosionFXRelay.h 가 ABomb 을 탈락시킨 것과 같은 이유), GameMode 는
// 클라에 아예 존재하지 않는다. 그래서 릴레이 하나를 공유한다.
//
// **Unreliable 인 이유**: 순수 시각이라 한 발 빠져도 판정에 영향이 없다. 반대로
// AExplosionFXRelay 가 Reliable 인 것은 물줄기 표시가 빠지면 "저기 안 터졌나?" 로 읽혀
// 규칙 이해가 어긋나기 때문이다. 비용은 큐당 약 5바이트(enum 1 + 양자화 위치 ~4~10) ×
// 사건 빈도(획득 수십 회·킥 수십 회·종료 1회) — 한 매치 통틀어 수 KB 미만이다.
// bAlwaysRelevant: 컬링으로 큐를 놓치는 것은 어차피 소리 하나지만, 릴레이가 하나뿐이라
// relevancy 계산을 매 클라마다 도는 편이 오히려 비싸다 (AExplosionFXRelay 관례).
UCLASS()
class CRAZYARCADE3D_API ACA3DCueRelay : public AInfo
{
	GENERATED_BODY()

public:
	ACA3DCueRelay();

	// 서버 → 전 클라: 큐 하나. 데디 가드는 두지 않는다 — CA3DFeedback::Play 가 진다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCue(ECA3DCue Cue, FVector_NetQuantize Location);
};

// 그리드 변경 구독자 + 릴레이 캐시.
//
// **AVoxelWorld::OnGridChanged 를 Gameplay 쪽에서 구독하는 주체가 이 클래스다.**
// 월드 서브시스템인 이유: 구독 수명이 정확히 월드 수명과 같아야 하고(액터에 걸면 그 액터의
// 스폰 시점·파괴 시점이 곧 소리의 유무가 된다), 서버·클라 양쪽에 조건 없이 존재해야 한다.
UCLASS()
class CRAZYARCADE3D_API UCA3DFeedbackSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 지형 구독은 여기서 한 번. AVoxelWorld 는 레벨 배치 액터라 이 시점에 이미 존재한다 —
	// 없으면 그 맵에는 영영 없다는 뜻이므로 재시도하지 않는다 (없는 맵에서 매 틱 도는 것을 막는다).
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// 서버: 큐 릴레이 lazy 스폰·캐시 (UExplosionSubsystem::ResolveFXRelay 관례).
	ACA3DCueRelay* ResolveRelay();

private:
	// 파괴 1묶음 = 소리 1회. AVoxelWorld 는 파괴를 **묶음 단위로** 적용하므로(불변식 1의
	// ApplyDestruction) 이 콜백 자체가 이미 "몰아서 1회" 다 — 여기서 따로 합칠 필요가 없다.
	void OnGridChanged(const TArray<FIntVector>& ChangedCells);

	// 좌표 변환용 (CellSize 의 유일한 출처).
	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	UPROPERTY()
	TObjectPtr<ACA3DCueRelay> Relay;

	FDelegateHandle GridChangedHandle;

	friend class FFeedbackTest; // 자동화 테스트가 구독 배선을 수동으로 확인하기 위한 접근
};
