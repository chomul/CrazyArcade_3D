#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptInterface.h"
#include "Voxel/VoxelGrid.h"
#include "Voxel/VoxelRenderer.h" // 인터페이스 자체가 가벼워 헤더 include 허용
#include "Gameplay/Item/ItemTypes.h" // FItemPlacement — **데이터 타입 참조까지만** 허용된 기존 예외
                                     // (MapGenerator.h:5 와 같은 근거). 지형은 아이템 "액터" 를 몰라야
                                     // 하므로 AItemPickup 참조·스폰은 절대 여기 들어오지 않는다.
#include "VoxelWorld.generated.h"

class UHISMVoxelRenderer;

// 복셀 지형 액터. 레벨에 1개 배치. bReplicates = true.
//
// 리플리케이션 흐름 (불변식 1 — 서버·클라가 같은 함수를 통과한다):
//   [서버] ServerDestroyBlocks → ApplyDestruction + Multicast
//                                                      └─▶ [클라] ApplyDestruction
// 알려진 함정: 파괴 Multicast가 OnRep_Seed보다 먼저 도착할 수 있다.
// 그리드 초기화 전에 받은 파괴 셀은 PendingDestroyQueue에 쌓고 OnRep_Seed 직후 flush.
UCLASS()
class CRAZYARCADE3D_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	// ─── 조회 (서버·클라 공통, O(1)) ───
	EBlockType GetBlock(const FIntVector& C) const { return Grid.Get(C); }
	bool IsSolid(const FIntVector& C) const { return Grid.IsSolid(C); }
	const FVoxelGrid& GetGrid() const { return Grid; }   // Propagate 등 읽기 전용 접근

	// 그리드가 생성됐는가 — 클라의 프리뷰 계산(ABomb)이 OnRep_Seed 선후 관계를 확인하는 용도.
	bool IsGridInitialized() const { return bGridInitialized; }

	// 그리드 변경(파괴 적용) 알림 — 서버·클라 각자 ApplyDestruction 직후 로컬 브로드캐스트.
	// 소비처 예: ABomb 위험 프리뷰 갱신 (퓨즈 중 지형이 바뀌면 표시 범위가 실폭발과 어긋나므로 재계산).
	// Voxel 은 구독자가 누구인지 모른다 — "게임 규칙을 몰라야 함" 의존 규칙 유지.
	DECLARE_MULTICAST_DELEGATE(FOnGridChanged);
	FOnGridChanged OnGridChanged;

	// ─── 좌표 변환 (셀 크기를 아는 유일한 곳) ───
	FIntVector WorldToCell(const FVector& W) const;
	FVector    CellToWorld(const FIntVector& C) const;      // 셀 중심
	FVector    CellToWorldFloor(const FIntVector& C) const; // 셀 바닥면 중심

	// 반올림하지 않은 실수 셀 좌표. 격자 레이 마칭(VoxelRay::GatherSolidCells)이 셀 경계를
	// 정확히 계산하려면 소수부가 필요하다 — WorldToCell 로 잘라내면 시작점이 셀 중앙으로
	// 튀어 첫 칸을 건너뛰거나 없는 칸을 넣는다.
	FVector WorldToCellFloat(const FVector& W) const;

	// 가림 페이드용 — 렌더러에 셀 페이드 값을 전달한다 (0 = 평소, 1 = 최대로 사라짐).
	// 렌더러가 없으면(데디 서버) false. 반환값의 의미는 IVoxelRenderer::SetCellFade 주석 참조.
	// 여기서 값을 해석하지 않는다 — 지형은 "왜" 페이드하는지 몰라야 한다.
	bool SetCellFade(const FIntVector& Cell, float Value);

	// 진단용 되읽기 — 인스턴스에 실제로 들어간 값. 없으면 -1 (IVoxelRenderer::GetCellFade).
	float GetCellFade(const FIntVector& Cell) const;

	// ─── 서버 전용 ───
	// 레거시 단일 인자 경로 — 크기는 룰셋 MapSize (기존 테스트·단독 실행의 기준 경로, Task 22).
	void ServerInitFromSeed(uint32 InSeed);
	// 정식 경로 (Task 22) — GameMode 가 인원별 티어로 고른 크기를 명시해 호출한다.
	void ServerInitFromSeed(uint32 InSeed, const FIntVector& InSize);
	void ServerDestroyBlocks(const TArray<FIntVector>& Cells);

	// 생성기가 반환한 스폰 셀 — 서버 GameMode(ChoosePlayerStart)가 소비한다 (Task 09).
	// 결정론 생성이라 클라에도 같은 값이 만들어지지만 소비처는 서버뿐이다.
	const TArray<FIntVector>& GetSpawnCells() const { return SpawnCells; }

	// 생성기가 반환한 아이템 배치 — **데이터로 보관만 한다** (Task 23).
	// 지형은 "여기 아이템이 숨겨져 있다"는 사실만 알고, 그게 무슨 액터가 되는지·언제 스폰되는지는
	// 모른다. 스폰은 Gameplay(UExplosionSubsystem)가 파괴 시점에 한다 — 폴더 의존 규칙 유지.
	const TArray<FItemPlacement>& GetItemPlacements() const { return ItemPlacements; }

	// 서버: 그 셀에 숨겨진 아이템을 꺼내 목록에서 제거한다 (있으면 true).
	// "조회 + 제거"가 한 함수인 이유: 노출은 셀당 정확히 1회여야 한다. 조회만 제공하면
	// 호출부마다 제거를 잊을 수 있고, 그러면 같은 셀이 다시 파괴될 때(서든데스 등) 아이템이
	// 복제된다. 클라는 이 함수를 부르지 않는다 — 클라 목록은 생성 결과 그대로 남는다.
	bool ConsumeItemPlacement(const FIntVector& Cell, EItemType& OutType);

	// ⚠️ 임시값 — 확정은 Task 10 튜닝에서. 임의 변경 금지, 질문할 것.
	UPROPERTY(EditAnywhere, Category="Voxel")
	float CellSize = 100.f;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 맵 생성 시드. 클라는 OnRep으로 서버와 동일한 맵을 결정론적으로 재생성한다.
	UPROPERTY(ReplicatedUsing=OnRep_Seed)
	uint32 Seed = 0;

	UFUNCTION() void OnRep_Seed();

	// 맵 크기 — 서버가 결정해 복제한다 (Task 22 인원별 티어). ZeroValue = 아직 미결정.
	// 클라가 로컬 인원수로 크기를 유추하면 **중간 접속자가 다른 맵을 만든다** (접속 시점
	// 인원 ≠ 매치 시작 시점 인원 — Multicast 함정과 같은 계열). 크기는 Seed 와 마찬가지로
	// 서버 결정·복제 값 하나만 믿는다.
	UPROPERTY(ReplicatedUsing=OnRep_GridSize)
	FIntVector GridSize = FIntVector::ZeroValue;

	UFUNCTION() void OnRep_GridSize();

	// 파괴 이력 — **중간 접속자를 위한 것**이다.
	// Multicast RPC 는 "그 순간 접속해 있는" 클라에게만 간다. 그래서 이력이 없으면 늦게 들어온
	// 클라는 시드로 만든 원본 지형만 갖는다 (2026-08-02 데디 실측: 서버가 15칸 부순 뒤 접속한
	// 클라가 그 15칸을 멀쩡한 벽으로 봄 — 서버는 통과시키는데 클라는 막히고, 그 반대도 난다).
	// 프로퍼티 복제는 **접속 시점에 현재 값 전체**를 보내주므로, 이력을 배열로 들고 있는 것만으로
	// 중간 접속이 해결된다. Multicast 는 그대로 둔다 — 즉시성(같은 프레임 반영)은 그쪽이 낫다.
	UPROPERTY(ReplicatedUsing=OnRep_DestroyedCells)
	TArray<FIntVector> DestroyedCells;

	UFUNCTION() void OnRep_DestroyedCells();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnBlocksDestroyed(const TArray<FIntVector>& Cells);

private:
	FVoxelGrid Grid;

	// 생성기 출력 보관 — 스폰 셀은 GameMode(Task 09), 아이템 배치는 폭발 처리(Task 23)가 소비한다.
	// 둘 다 복제하지 않는다: 결정론 생성이라 클라도 같은 값을 스스로 만든다 (Seed 하나면 충분).
	TArray<FIntVector> SpawnCells;
	TArray<FItemPlacement> ItemPlacements;

	// 클라/리슨에서만 연결, 데디 서버는 nullptr (BeginPlay에서 분기).
	UPROPERTY()
	TScriptInterface<IVoxelRenderer> Renderer;

	// C++ 기본 렌더러 컴포넌트 (Task 07). 생성자 CreateDefaultSubobject로 CDO에 존재해야
	// BP_VoxelWorld 서브클래스에서 BlockMeshes 디폴트(메시 에셋)를 편집할 수 있다
	// (BeginPlay NewObject 컴포넌트는 BP 디폴트를 못 받는다).
	// 데디 서버는 BeginPlay에서 DestroyComponent — 시각 전용 (불변식 5).
	UPROPERTY(VisibleAnywhere, Category="Voxel")
	TObjectPtr<UHISMVoxelRenderer> HISMRendererComponent;

	// 그리드 초기화 전에 도착한 파괴 Multicast의 셀 보관 큐 (OnRep_Seed 직후 flush).
	TArray<FIntVector> PendingDestroyQueue;

	bool bGridInitialized = false;

	// 파괴 적용 순번 (인스턴스 로컬). 🏁 게이트용 자동 해시 로그의 대조 키 — 서버·클라가
	// 같은 파괴를 각자 적용하므로 같은 #N 끼리 해시가 같아야 한다 (설계서 5장 11번).
	int32 DestructionApplyCount = 0;

	// 서버·클라 공통 그리드 생성 경로. 성공 시 bGridInitialized + 렌더 빌드 + 큐 flush.
	void InitGridFromSeed();

	// 파괴의 단일 경로 (불변식 1). 서버든 클라든 반드시 이 함수로만 들어온다.
	void ApplyDestruction(const TArray<FIntVector>& Cells);

	// 아직 적용되지 않은(= 그리드에 아직 남아 있는) 셀만 걸러낸다.
	// 같은 셀이 Multicast 와 복제 이력(DestroyedCells) 양쪽으로 들어오므로 필요하다 —
	// 거르지 않으면 같은 파괴가 두 번 적용돼 해시 로그 순번이 서버와 어긋난다.
	TArray<FIntVector> FilterUnapplied(const TArray<FIntVector>& Cells) const;

	friend class FVoxelWorldTest; // 자동화 테스트가 OnRep_Seed·Multicast 경로(파괴 선도착 큐)를 검증하기 위한 접근
	friend class FBombTest; // 자동화 테스트가 손그리드 구성(결정론 시나리오)을 위한 접근 (Task 16)
	friend class FPredictedBombVisualTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 17)
	friend class FHISMVoxelRendererTest; // BeginPlay가 돌지 않는 테스트 월드에서 Renderer 수동 배선용
	friend class FDeathHandlingTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 27)
	friend class FBotControllerTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 20)
	friend class FItemPickupTest; // 자동화 테스트가 손그리드·아이템 배치 주입을 위한 접근 (Task 23)
	friend class FSuddenDeathTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (Task 24)
	friend class FBombKickTest; // 자동화 테스트가 손그리드 구성을 위한 접근 (폭탄 킥)
};
