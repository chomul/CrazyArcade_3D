#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Bomb.generated.h"

class ACA3DCharacter;
class AVoxelWorld;
class UBoxComponent;
class UStaticMeshComponent;

// 서버 권한 폭탄 (Task 16). bReplicates = true — 클라에는 액터 리플리케이션으로 존재만 복제.
// 타이머·판정·연쇄는 100% 서버 소유 (불변식 3의 서버 쪽 절반) — 클라 예측 표시는
// APredictedBombVisual(Task 17)이 담당하고 서버 거부 시 이펙트만 지운다.
//
// **풀링 금지** (설계서 2.8) — 퓨즈 타이머·bDetonated·소유자 등 서버 권한 상태를 가진
// 액터는 재사용 시 상태 오염 위험이 이득보다 크다. 스폰/Destroy 로만 수명 관리.
UCLASS()
class CRAZYARCADE3D_API ABomb : public AActor
{
	GENERATED_BODY()

public:
	ABomb();

	// 서버: 스폰 직후 호출. 소유자·범위·셀 기록 + ActiveBombCount 점유 + 서브시스템 등록 +
	// Rules->BombFuseTime 타이머 시작. Cell·Range 를 첫 복제 전에 확정해야 하므로
	// SpawnActorDeferred → ServerArm → FinishSpawning 순서로 부른다 (ACA3DCharacter::ServerPlaceBomb).
	void ServerArm(ACA3DCharacter* InOwner, int32 InRange, const FIntVector& InCell);

	FIntVector GetCell() const { return Cell; }
	int32      GetRange() const { return Range; }
	bool       IsDetonated() const { return bDetonated; }
	ACA3DCharacter* GetOwnerChar() const { return OwnerChar; }

	// 서버: 폭발의 단일 진입점 — 남은 퓨즈를 무시하고 연쇄 큐(ExplosionSubsystem)에 합류.
	// bDetonated 플래그로 중복 폭발 방지 (같은 폭탄이 두 연쇄 단계에 들어갈 수 없다).
	// 퓨즈 만료(OnFuseExpired)도 이 함수를 탄다 — bDetonated 기록 경로를 하나로 유지.
	void ServerForceDetonate();

	// 서버: 실제 폭발 처리 시(ProcessChainStep) 소유자 슬롯 반환 + 서브시스템 등록 해제.
	// EndPlay 가 안전망으로 한 번 더 부른다 — bSlotReturned 로 정확히 1회 보장.
	void ServerReleaseSlot();

	// ─── 킥 (2026-08-06 사용자 확정: 방향키로 밀기 — 별도 키 없음) ───
	//
	// 서버: 이 폭탄을 Direction(±X 또는 ±Y 의 단위 정수 벡터)으로 미끄러뜨린다.
	// 시작 조건이 안 맞으면(이미 미끄러지는 중·폭발 예약됨·바로 앞이 막힘) 조용히 무시한다 —
	// 호출자(ACA3DCharacter::ServerTryKickBomb)가 폰이 밀고 있는 동안 **매 틱** 부르기 때문에
	// 여기서 실패를 로그로 떠들면 프레임마다 스팸이 된다.
	//
	// **이동은 100% 서버 권한이다** (불변식 3 의 정신) — 클라 예측 없이 액터 복제로 따라간다.
	// 예측을 넣으면 "내 화면에서만 다른 칸까지 굴러간 폭탄" 이 생기고, 그 어긋남은 폭발 원점이
	// 달라지는 것으로 이어져 위험 프리뷰(5장 9번)까지 함께 무너진다.
	void ServerStartKick(const FIntVector& Direction);

	bool IsKicking() const { return bKicking; }

	// 발판 없는 칸으로 밀려 떨어지는 중인가 (2026-08-09 사용자 확정 — 아래 낙하 절 참조).
	bool IsFalling() const { return bFalling; }

	// 막힘 박스의 실제 반경(cm, 스케일 반영). 킥 접촉 판정이 "캡슐 반지름 + 이 값" 으로
	// 사거리를 만든다 — 호출부가 룰셋 계수 × CellSize 를 다시 곱하지 않게 한다 (공식 중복 금지).
	float GetBlockingExtent() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;   // 클라(+리슨 호스트): 위험 프리뷰 데칼 표시
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// 메시 제자리 회전(시각) + 킥 미끄러짐(서버 권한 이동). 데디에서는 평소 틱을 끄지만
	// 킥이 시작되면 ServerStartKick 이 다시 켠다 — 이동은 시각이 아니라 물리·판정이다.
	virtual void Tick(float DeltaSeconds) override;

	// 설치 셀 (그리드 판정용). 클라 프리뷰 계산에 필요해 복제.
	// **킥으로 바뀐다** — 폭발 전파의 원점이자 ExplosionSubsystem 레지스트리 조회 키이므로
	// 설치(ServerArm) 이후의 갱신은 ServerSetCell 단일 경로로만 한다 (직접 대입 금지).
	UPROPERTY(ReplicatedUsing=OnRep_Cell)
	FIntVector Cell = FIntVector::ZeroValue;

	// 클라: 킥으로 셀이 바뀌면 위험 데칼도 따라와야 표시와 실제가 일치한다 (설계서 5장 9번).
	UFUNCTION()
	void OnRep_Cell();

	// 폭발 전파 범위(칸). 클라 프리뷰 계산에 필요해 복제.
	UPROPERTY(Replicated)
	int32 Range = 1;

	// BP_Bomb 에서 메시 에셋만 지정 — BP 로직 금지. 데디 서버는 BeginPlay 에서 파괴 (불변식 5).
	// 제자리 회전은 이 컴포넌트에만 건다 (Gameplay/SpinVisual.h) — 아래 BlockingBox 는 안 돈다.
	UPROPERTY(VisibleAnywhere, Category="Bomb")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// 플레이어를 막는 컬리전 (2026-08-06 사용자 요청).
	//
	// ⚠️ **데디 서버에서도 파괴하지 않는다.** 막힘은 시각이 아니라 물리이고 CharacterMovement 는
	// 서버에서도 돈다 — 여기에 데디 가드를 넣으면 서버만 폭탄을 통과해 클라와 위치가 어긋난다.
	// AVoxelWorld 의 HISM 을 "시각 전용"으로 보고 데디에서 껐다가 서버에 바닥이 사라진 것과
	// 같은 함정이다 (CLAUDE.md 알려진 함정, AItemPickup::PickupSphere 주석).
	// PIE 로는 재현되지 않는다 — 에디터는 IsRunningDedicatedServer() == false.
	UPROPERTY(VisibleAnywhere, Category="Bomb")
	TObjectPtr<UBoxComponent> BlockingBox;

private:
	// 서버 전용 — 폭발 시 ActiveBombCount 반환 대상 (사망/파괴 시 null 가드).
	UPROPERTY()
	TObjectPtr<ACA3DCharacter> OwnerChar;

	// 프리뷰용 좌표 변환·그리드 접근 (클라·리슨). lazy 탐색·캐시.
	UPROPERTY()
	TObjectPtr<AVoxelWorld> CachedVoxelWorld;

	// 클라 시각 — 풀에서 획득한 위험 프리뷰 데칼. 폭발/파괴(EndPlay) 시 반납.
	UPROPERTY()
	TArray<TObjectPtr<AActor>> PreviewDecals;

	FTimerHandle FuseTimer;      // 서버 전용 — 클라는 타이머를 모른다 (불변식 3)
	bool bDetonated = false;     // 연쇄 중복 폭발 방지
	bool bSlotReturned = false;  // ActiveBombCount 반환 1회 보장

	// 그리드 변경 구독 (클라 시각) — 퓨즈 중 다른 폭발이 지형을 바꾸면 프리뷰를 재계산한다.
	// 안 하면 설치 시점 스냅샷이 남아 표시 범위 ≠ 실폭발 범위가 된다 (5장 9번 위반).
	FDelegateHandle GridChangedHandle;

	void OnFuseExpired();        // 서버: ServerForceDetonate → ExplosionSubsystem::RequestDetonate

	// ─── 막힘 승격 (설치자가 자기 폭탄에 갇히지 않게) ───
	//
	// 폭탄은 **설치자 발밑에** 생긴다. 처음부터 Block 이면 설치하는 순간 자기 폭탄에 갇힌다.
	// 그래서 겹쳐 있는 폰이 전부 빠져나간 뒤에야 Block 으로 승격한다 (원작 크아와 같은 규칙).
	//
	// 서버·클라 양쪽이 각자 로컬 오버랩으로 판단한다 — 복제하지 않는다. 막힘은 CMC 가 매 틱
	// 로컬에서 쓰는 물리 상태라 "언제 막히기 시작했나"를 복제해도 어차피 로컬 형상이 결정한다.
	// 원격 폰은 보간 지연 때문에 승격 시점이 머신마다 몇 프레임 어긋날 수 있으나, **자기 폰**의
	// 겹침은 로컬에서 정확하므로 "내가 내 폭탄에 갇힌다" 는 사고는 일어나지 않는다.
	UFUNCTION()
	void OnBlockingBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnBlockingEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	// 겹친 폰이 하나도 없으면 Block 으로 승격 (1회). 이미 승격했으면 no-op.
	void PromoteToBlockingIfClear();

	// 박스 크기를 "룰셋 계수 × CellSize" 로 맞춘다 (매직 넘버 금지). VoxelWorld 가 없는
	// 테스트 월드에서는 생성자 임시값을 그대로 쓴다 — AItemPickup::ApplyCellScale 관례.
	void ApplyBlockingScale();

	// 아직 겹쳐 있는 폰들. 둘이 같은 칸에 겹쳐 설치할 수 있으므로 bool 이 아니라 집합이다 —
	// 하나가 나갔다고 막아버리면 남은 하나가 폭탄 안에 갇힌다.
	TSet<TWeakObjectPtr<AActor>> OverlappingPawns;

	bool bBlocking = false;          // Block 승격 1회 보장
	float SpinDegreesPerSecond = 0.f; // BeginPlay 에서 룰셋 1회 조회 (틱마다 GameState 조회 방지)

	// 클라(+리슨): 위험 프리뷰 데칼 — 실폭발과 **같은** Propagate 를 호출한다 (별도 계산 금지 —
	// 설계서 5장 9번 "표시와 실제가 구조적으로 일치"). 그리드/룰셋 미도착 시 next-tick 재시도.
	void TryShowDangerPreview();
	void RefreshDangerPreview(); // 기존 데칼 반납 후 재계산 (셀 변경·그리드 변경 공용)
	void ReleasePreviewDecals();

	// AVoxelWorld::OnGridChanged 바인딩 전용 껍데기 — 어떤 칸이 바뀌었는지는 프리뷰 재계산에
	// 필요 없다(Propagate 가 그리드 전체를 다시 읽는다). 시그니처를 맞추기 위한 한 줄이라
	// RefreshDangerPreview 를 직접 부르는 다른 호출부(OnRep_Cell·ServerSetCell)와 분리해 둔다.
	void HandleGridChanged(const TArray<FIntVector>& ChangedCells);

	// ─── 킥 미끄러짐 (서버 전용) ────────────────────────────────────────────
	//
	// 정지 판정을 **그리드로** 한다 (물리 스윕이 아니라). 폭발 전파(Propagate)와 같은 데이터를
	// 봐야 "여기서 멈춘다" 와 "여기까지 터진다" 가 어긋나지 않는다 — 스윕으로 판정하면
	// HISM 인스턴스 형상·컬리전 채널 같은 렌더링 쪽 사정이 게임 규칙에 새어 들어온다.
	// 단 **플레이어는 그리드에 없다** — 폰 위치를 셀로 바꿔(GetFootCell) 비교한다.

	// 서버: Cell 갱신의 단일 경로. 레지스트리·위험 데칼이 새 셀을 따라가게 만든다.
	void ServerSetCell(const FIntVector& NewCell);

	// 서버: 다음 칸(NextCell)에 들어갈 수 있는가 — 맵 밖·솔리드·다른 폭탄·산 플레이어면 false.
	bool CanKickInto(const FIntVector& NextCell) const;

	// 서버: 미끄러짐 진행 (Tick 에서만 호출). 셀 중심에 도달할 때마다 칸 단위 판정을 한다.
	void ServerUpdateKick(float DeltaSeconds);

	// 서버: 미끄러짐 종료 — 셀 중심으로 스냅하고 (낙하 중이 아니면) 데디에서 틱을 다시 끈다.
	void ServerStopKick();

	bool bKicking = false;                            // 미끄러지는 중
	FIntVector KickDirection = FIntVector::ZeroValue; // ±X 또는 ±Y 단위 정수 벡터
	int32 KickCellsRemaining = 0;                     // 남은 이동 칸 수 (룰셋 BombKickMaxCells 에서 시작)
	FVector KickTargetLocation = FVector::ZeroVector; // 다음 셀 중심의 월드 좌표
	float KickSpeed = 0.f;                            // cm/s — 시작 시 룰셋 × CellSize 로 1회 계산

	// ─── 낙하 (서버 전용 — 2026-08-09 사용자 확정: "절벽으로 밀어 떨어뜨리기") ──────
	//
	// 킥으로 **새 셀 중심에 도달한 순간** 그 칸 바로 아래가 솔리드가 아니면 떨어진다.
	// 이미 설치돼 가만히 있는 폭탄의 발판이 폭발로 사라졌을 때의 낙하는 **범위 밖**이다 —
	// 그건 "언제 무너지는가"(폭발 처리 순서·연쇄와 얽힌다)를 따로 정해야 하는 별개 규칙이다.
	//
	// 중력(CMC)이 아니라 **수직 등속**인 이유는 룰셋 BombFallSpeedCellsPerSec 주석에 있다:
	// 낙하도 킥과 같은 칸 단위 판정을 타야 하고, 가속을 넣으면 "프레임이 길 때 여러 칸"
	// 계산이 킥 쪽과 두 벌이 된다.

	// 서버: 낙하 시작. 수평 이동을 그 자리에서 끊는다 (포물선 금지 — 칸 정렬이 깨지면
	// Cell(폭발 원점)과 실제 위치가 영구히 어긋난다).
	void ServerStartFall();

	// 서버: 낙하 진행 (Tick 에서만 호출). 한 칸 내려갈 때마다 착지·이탈을 판정한다.
	void ServerUpdateFall(float DeltaSeconds);

	// 서버: 착지·중단. 셀 중심으로 스냅하고 데디에서는 틱을 되돌린다.
	// **남은 킥 거리는 쓰지 않는다** — 목적이 "떨어뜨린다"이고, 착지 후 계속 미끄러지면
	// 협곡 바닥에서 어디까지 굴러갈지 예측이 안 된다 (ServerStartFall 이 킥을 이미 끊었다).
	void ServerStopFall();

	// 그 칸이 발판 위에 있는가 — 바로 아래가 솔리드면 참. **그리드만 본다**: 다른 폭탄이나
	// 플레이어는 발판이 아니다 (그것을 발판으로 치면 아래 폭탄이 터지는 순간 위 폭탄이
	// 공중에 박힌다 — 그 뒤처리가 곧 "범위 밖"으로 미뤄 둔 규칙이다).
	// 그리드를 모르면 참을 돌려준다 — 모르는 채로 떨어뜨리지 않는다 (안전측).
	bool IsSupportedAt(const FIntVector& InCell) const;

	// 서버: 스스로 움직이는 동안에만 틱이 필요하다 — 켜고 끄는 판단을 여기 한 곳에 모은다.
	// 킥→낙하 전환 때 ServerStopKick 이 틱을 꺼버려 폭탄이 공중에 박히는 사고를 구조적으로 막는다.
	void ServerRefreshMovementTick();

	bool bFalling = false;                            // 떨어지는 중
	FVector FallTargetLocation = FVector::ZeroVector; // 한 칸 아래 셀 중심의 월드 좌표
	float FallSpeed = 0.f;                            // cm/s — 시작 시 룰셋 × CellSize 로 1회 계산

	friend class FBombTest; // 자동화 테스트가 퓨즈 타이머·플래그 검증을 위한 접근
	friend class FBombKickTest; // 자동화 테스트가 킥 상태·수동 틱 검증을 위한 접근
};
