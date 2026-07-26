# 3D 크레이지 아케이드 — 기술 구조 설계서

> `crazy-arcade-3d-gdd-v2.md`의 구현 대응 문서. GDD가 "무엇을"이라면 이 문서는 "어떻게 배치할 것인가".
> 인터뷰(2026-07-27)로 확정된 12개 구조 결정을 기록.

---

## 0. 확정된 구조 결정 요약

| # | 결정 사항 | 선택 | 근거 |
|---|---|---|---|
| 1 | 모듈 경계 | 단일 모듈 + 도메인 폴더 | 3주 일정에서 빌드 설정 오버헤드 최소화. 나중에 플러그인으로 추출 가능 |
| 2 | 그리드 소유 | `AVoxelWorld` 액터 | 리플리케이션이 자연스럽고 데디 서버에서 렌더러만 생략 가능 |
| 3 | 폭탄 표현 | `ABomb` 액터 per 폭탄 | 최대 40개 수준. 킥(비정수 위치) 처리가 자연스러움 |
| 4 | 튜닝 값 | `UDataAsset` + GameMode 참조 | 룰셋 프리셋 다중화 가능 |
| 5 | 폭발 동기화 | 영향 셀 목록 결과 전송 | 불일치 가능성 0, 자가 교정 |
| 6 | 폭발 로직 | `UExplosionSubsystem` | 연쇄 프레임 분산이 한 곳에 모임 |
| 7 | 풀링 | 제네릭 풀 서브시스템 | 물줄기·파편·데칼·아이템 통일 API |
| 8 | 플레이어 상태 | Character의 `UStatusComponent` | 봇과 코드 경로 동일 |
| 9 | 서버 권한 시점 | **1주차부터 분리** | 2주차 "이전" 리스크 제거 |
| 10 | 맵 생성 | `IMapGenerator` 인터페이스 + 구현체 2개 | 폴백↔절차 교체가 1줄 |
| 11 | C++/BP | C++ 베이스 + BP 서브클래스 | BP에 로직 금지 |
| 12 | 봇 | 순수 C++ FSM AIController | BT 학습 비용 회피 |

---

## 1. 폴더 구조

```
CrazyArcade3D/
├── CrazyArcade3D.uproject
├── Config/
│   ├── DefaultEngine.ini
│   ├── DefaultGame.ini
│   └── DefaultInput.ini
├── Content/
│   ├── Blueprints/       # C++ 클래스의 BP 서브클래스만
│   ├── Data/             # DA_Rules_*, DA_BlockSet_*
│   ├── Maps/             # L_Lobby, L_Arena
│   ├── Meshes/ Materials/ Niagara/ Audio/ UI/
└── Source/
    ├── CrazyArcade3D.Target.cs
    ├── CrazyArcade3DEditor.Target.cs
    ├── CrazyArcade3DServer.Target.cs      # ⬅ 데디 서버 타깃, 1주차에 미리 생성
    └── CrazyArcade3D/
        ├── CrazyArcade3D.Build.cs
        ├── CrazyArcade3D.h/.cpp
        │
        ├── Voxel/                 # 지형: 데이터 + 권한 + 렌더링
        │   ├── VoxelTypes.h           # EBlockType, FIntVector 헬퍼, 상수
        │   ├── VoxelGrid.h/.cpp       # 순수 데이터 구조 (UObject 아님)
        │   ├── VoxelWorld.h/.cpp      # AActor — 소유·권한·리플리케이션
        │   ├── VoxelRenderer.h        # UInterface — 렌더링 추상화
        │   └── HISMVoxelRenderer.h/.cpp
        │
        ├── MapGen/                # 맵 생성: 전부 순수 함수
        │   ├── MapGenerator.h         # IMapGenerator 인터페이스
        │   ├── FallbackMapGenerator.h/.cpp
        │   ├── ProcMapGenerator.h/.cpp    # 3주차
        │   └── MapValidator.h/.cpp        # 검증 순수 함수 모음
        │
        ├── Gameplay/
        │   ├── Bomb/
        │   │   ├── Bomb.h/.cpp                # ABomb — 서버 권한
        │   │   ├── PredictedBombVisual.h/.cpp # 클라 전용, 로직 없음
        │   │   ├── ExplosionSubsystem.h/.cpp  # 전파 + 연쇄 스케줄링
        │   │   └── ExplosionTypes.h           # FExplosionResult 등
        │   ├── Character/
        │   │   ├── CA3DCharacter.h/.cpp
        │   │   ├── StatusComponent.h/.cpp     # 스탯 + 갇힘/사망
        │   │   └── CA3DPlayerController.h/.cpp
        │   ├── Item/
        │   │   ├── ItemTypes.h                # EItemType
        │   │   └── ItemPickup.h/.cpp
        │   └── SuddenDeath/
        │       └── SuddenDeathSubsystem.h/.cpp    # 3주차
        │
        ├── Framework/
        │   ├── CA3DGameMode.h/.cpp        # 서버 전용 매치 진행
        │   ├── CA3DGameState.h/.cpp       # 복제되는 매치 상태
        │   ├── CA3DPlayerState.h/.cpp
        │   ├── CA3DRuleSet.h/.cpp         # UPrimaryDataAsset
        │   └── CA3DGameInstance.h/.cpp    # EOS 세션 (2주차)
        │
        ├── Core/
        │   ├── PoolSubsystem.h/.cpp       # 제네릭 액터 풀
        │   └── PooledActor.h              # IPooledActor 인터페이스
        │
        ├── AI/
        │   └── BotController.h/.cpp       # C++ FSM (2주차)
        │
        └── UI/
            ├── CA3DHUD.h/.cpp
            └── MatchWidget.h/.cpp         # UUserWidget 베이스, 레이아웃은 BP
```

**폴더 간 의존 규칙 (컴파일러가 강제하진 않지만 지킬 것)**

```
MapGen  ──▶ Voxel        (그리드를 만들어 반환)
Voxel   ──▶ (없음)        ⬅ 게임 규칙을 몰라야 함
Gameplay ──▶ Voxel, Core
Framework ──▶ 전부
UI      ──▶ Framework (읽기 전용)
Core    ──▶ (없음)
```

> `Voxel`이 `Gameplay`를 참조하는 순간 구조가 무너집니다. 지형은 "누가 왜 부쉈는지" 몰라야 하고, 그래야 맵 생성기·에디터 툴·다른 모드에서 재사용됩니다.

---

## 2. 핵심 타입

### 2.1 Voxel — 데이터 (`VoxelGrid.h`)

```cpp
UENUM()
enum class EBlockType : uint8
{
    Empty       = 0,
    Floor       = 1,   // 바닥 (파괴 여부는 룰셋)
    Destructible= 2,   // 파괴 가능
    Immortal    = 3,   // 불멸
};

// UObject 아님. 값 타입. 서버/클라 양쪽에서 동일하게 사용.
struct FVoxelGrid
{
    FIntVector  Size = FIntVector(21, 21, 4);
    TArray<uint8> Blocks;        // Size.X*Y*Z, 1바이트/칸

    void Init(FIntVector InSize);

    FORCEINLINE bool IsValid(const FIntVector& C) const;
    FORCEINLINE int32 Index(const FIntVector& C) const
    { return C.X + C.Y * Size.X + C.Z * Size.X * Size.Y; }

    // 범위 밖은 Empty 반환 — 호출부의 경계 검사 부담 제거
    FORCEINLINE EBlockType Get(const FIntVector& C) const;
    FORCEINLINE void Set(const FIntVector& C, EBlockType T);

    bool IsSolid(const FIntVector& C) const;   // 서 있을 수 있는가
    bool BlocksExplosion(const FIntVector& C) const;
};
```

**설계 포인트**
- `UObject`가 아니므로 GC·리플리케이션 오버헤드 0. 21×21×4 = 1,764바이트.
- 범위 밖 조회가 `Empty`를 반환하도록 하면 폭발 전파 루프의 경계 처리가 사라집니다.
- 좌표 변환(`FIntVector` ↔ `FVector`)은 여기 두지 말고 `VoxelWorld`에 (셀 크기를 알아야 하므로).

### 2.2 Voxel — 액터 (`VoxelWorld.h`)

```cpp
UCLASS()
class AVoxelWorld : public AActor
{
    GENERATED_BODY()
public:
    // ─── 조회 (서버·클라 공통, O(1)) ───
    EBlockType GetBlock(const FIntVector& C) const { return Grid.Get(C); }
    bool IsSolid(const FIntVector& C) const { return Grid.IsSolid(C); }

    // ─── 좌표 변환 ───
    FIntVector WorldToCell(const FVector& W) const;
    FVector    CellToWorld(const FIntVector& C) const;   // 셀 중심
    FVector    CellToWorldFloor(const FIntVector& C) const;

    // ─── 서버 전용 ───
    void ServerInitFromSeed(uint32 InSeed);            // 맵 생성 + 렌더 빌드
    void ServerDestroyBlocks(const TArray<FIntVector>& Cells);

    UPROPERTY(EditAnywhere, Category="Voxel")
    float CellSize = 100.f;

protected:
    UPROPERTY(ReplicatedUsing=OnRep_Seed)
    uint32 Seed = 0;

    UFUNCTION() void OnRep_Seed();   // 클라: 시드 받으면 동일 맵 생성

    UFUNCTION(NetMulticast, Reliable)
    void MulticastOnBlocksDestroyed(const TArray<FIntVector>& Cells);

private:
    FVoxelGrid Grid;                          // 서버·클라 모두 보유
    TScriptInterface<IVoxelRenderer> Renderer; // 클라에서만 생성 (데디는 nullptr)

    void ApplyDestruction(const TArray<FIntVector>& Cells);  // 공통 경로
};
```

**권한 흐름**

```
[서버] ServerDestroyBlocks(Cells)
   ├─▶ ApplyDestruction(Cells)              # 서버 자기 그리드 갱신
   └─▶ MulticastOnBlocksDestroyed(Cells)
          └─▶ [클라] ApplyDestruction(Cells)  # 동일 함수 재사용
                 ├─ Grid.Set(c, Empty)
                 └─ Renderer->RemoveBlock(c) + 주변 6칸 재검사
```

서버와 클라가 **같은 `ApplyDestruction`을 통과**하는 게 핵심입니다. 경로가 갈라지면 반드시 어긋납니다.

> ⚠️ `OnRep_Seed`와 `MulticastOnBlocksDestroyed`의 **순서 보장**이 필요합니다. 시드보다 파괴 이벤트가 먼저 도착할 수 있으므로, 클라는 그리드 초기화 전에 받은 파괴 셀을 큐에 쌓아뒀다가 `OnRep_Seed` 직후 flush 해야 합니다. 이건 실제로 자주 터지는 버그이므로 처음부터 넣으세요.

### 2.3 Voxel — 렌더러 (`VoxelRenderer.h`)

```cpp
UINTERFACE() class UVoxelRenderer : public UInterface { GENERATED_BODY() };

class IVoxelRenderer
{
    GENERATED_BODY()
public:
    virtual void BuildFromGrid(const FVoxelGrid& Grid) = 0;
    virtual void RemoveBlock(const FIntVector& Cell, const FVoxelGrid& Grid) = 0;
    virtual void Clear() = 0;
};
```

`HISMVoxelRenderer`는 표면 추출(6면 중 하나라도 `Empty`에 접하면 인스턴스화)을 구현하고, 셀→인스턴스 인덱스 맵을 유지합니다.

**GDD 7.1의 "그리디 메싱으로 승격"이 이 인터페이스 하나 교체로 끝납니다.** `AVoxelWorld`는 어떻게 그리는지 전혀 모릅니다.

> HISM 인스턴스 제거 시 `RemoveInstance`는 마지막 인덱스를 그 자리로 당겨오므로, 셀→인덱스 맵을 반드시 함께 갱신해야 합니다. 여기서 인덱스가 꼬이면 엉뚱한 블록이 사라집니다.

### 2.4 폭발 (`ExplosionSubsystem.h`)

```cpp
struct FExplosionResult
{
    TArray<FIntVector> WaterCells;    // 물줄기가 채우는 칸
    TArray<FIntVector> BrokenCells;   // 파괴되는 블록
    TArray<ABomb*>     ChainedBombs;  // 유발된 폭탄 (서버 전용)
};

UCLASS()
class UExplosionSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // 서버: 폭탄 하나가 터짐 → 연쇄 큐에 투입
    void RequestDetonate(ABomb* Bomb);

    // 순수 함수 — 서버·클라·봇 AI가 모두 호출 가능, 부작용 없음
    static FExplosionResult Propagate(
        const FVoxelGrid& Grid, const FIntVector& Origin, int32 Range);

private:
    TArray<ABomb*> PendingChain;    // 다음 단계에 터질 폭탄
    FTimerHandle   ChainTimer;
    void ProcessChainStep();        // 0.05~0.1초마다 1단계 (GDD 7.3)
};
```

`Propagate`가 `static` + 순수 함수인 게 중요합니다:
- 봇 AI가 "여기 폭탄 놓으면 어디까지 터지나"를 부작용 없이 조회
- 위험 구역 프리뷰 데칼이 같은 함수로 계산 → **프리뷰와 실제가 절대 어긋나지 않음**
- 유닛 테스트 가능

**6방향 전파 규칙 (GDD 2.2)**
```
for dir in [+X,-X,+Y,-Y,+Z,-Z]:
    for step in 1..Range:
        cell = Origin + dir*step
        if Grid.Get(cell) == Immortal:      break          # 막힘
        if Grid.Get(cell) == Destructible:  Broken+=cell; break  # 부수고 멈춤
        if Grid.Get(cell) == Floor:
            if Rules.bFloorDestructible: Broken+=cell
            break
        Water += cell
        if BombAt(cell):  Chained += bomb    # 연쇄, 전파는 계속
```

### 2.5 상태 컴포넌트 (`StatusComponent.h`)

```cpp
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class UStatusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // ─── 아이템 스탯 (GDD 3장) ───
    UPROPERTY(ReplicatedUsing=OnRep_Stats) int32 MaxBombCount = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Stats) int32 BombRange    = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Stats) float MoveSpeedMul = 1.f;
    UPROPERTY(Replicated) bool bHasNeedle = false;
    UPROPERTY(Replicated) bool bHasKick   = false;

    // ─── 생존 상태 (GDD 2.3) ───
    UPROPERTY(ReplicatedUsing=OnRep_Life) ELifeState LifeState = ELifeState::Alive;
    // Alive / Trapped / Dead / Spectating

    int32 ActiveBombCount = 0;   // 서버 전용, 복제 불필요

    // 서버 전용 진입점
    void ServerApplyItem(EItemType Item);
    void ServerTrap();       // 물방울에 갇힘
    void ServerEscape();     // 니들 소모
    void ServerKill(EDeathCause Cause);
};
```

**왜 컴포넌트인가**: 봇(`ABotController`가 조종하는 `ACA3DCharacter`)이 실제 플레이어와 **완전히 같은 코드 경로**를 타게 됩니다(GDD 8장 2주차 요구사항). 상태를 Character 멤버로 두면 결국 같지만, 컴포넌트로 빼두면 v2에서 고유 능력 캐릭터가 들어올 때 Character 클래스가 비대해지지 않습니다.

### 2.6 룰셋 (`CA3DRuleSet.h`)

```cpp
UCLASS(BlueprintType)
class UCA3DRuleSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // 폭탄
    UPROPERTY(EditAnywhere, Category="Bomb") float BombFuseTime = 3.f;
    UPROPERTY(EditAnywhere, Category="Bomb") float WaterLingerTime = 1.f;
    UPROPERTY(EditAnywhere, Category="Bomb") float ChainStepDelay = 0.07f;
    UPROPERTY(EditAnywhere, Category="Bomb") int32 MaxBombCountCap = 8;
    UPROPERTY(EditAnywhere, Category="Bomb") int32 MaxBombRangeCap = 6;

    // 생존
    UPROPERTY(EditAnywhere, Category="Life") float TrappedDuration = 4.f;
    UPROPERTY(EditAnywhere, Category="Life") float TrappedMoveSpeed = 60.f;
    UPROPERTY(EditAnywhere, Category="Life") float SpawnInvulnTime = 0.f;

    // 맵
    UPROPERTY(EditAnywhere, Category="Map") bool bFloorDestructible = false;
    UPROPERTY(EditAnywhere, Category="Map") float ItemDropRate = 0.3f;
    UPROPERTY(EditAnywhere, Category="Map") FIntVector MapSize{21,21,4};

    // 서든데스
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float SuddenDeathStart = 150.f;
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float DropWarningTime = 1.5f;
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float OuterWeightBias = 2.f;
};
```

**⚠️ 복제 주의**: 룰셋은 GameMode(서버 전용)가 소유하지만, 클라도 폭탄 타이머 표시·프리뷰 계산에 필요합니다. `GameState`에 `UPROPERTY(Replicated) UCA3DRuleSet* Rules;`로 **에셋 포인터를 복제**하세요. UE는 에셋 참조를 경로로 복제하므로 값 전체가 아니라 참조만 오갑니다. GDD 10장의 "몸으로 결정할 값"들을 클라가 모르면 프리뷰가 어긋납니다.

### 2.7 맵 생성 (`MapGenerator.h`)

```cpp
UINTERFACE() class UMapGenerator : public UInterface { GENERATED_BODY() };

class IMapGenerator
{
    GENERATED_BODY()
public:
    // 결정론적: 같은 Seed + 같은 Rules ⇒ 항상 같은 Grid
    virtual bool Generate(uint32 Seed, const UCA3DRuleSet* Rules,
                          FVoxelGrid& OutGrid,
                          TArray<FIntVector>& OutSpawns,
                          TArray<FItemPlacement>& OutItems) = 0;
};
```

- `FallbackMapGenerator` — Seed 무시, 하드코딩 레이아웃 반환. **1주차 첫날 것.**
- `ProcMapGenerator` — `FRandomStream(Seed)` + 정수 연산만. **3주차.**
- `MapValidator` — `static bool Validate(const FVoxelGrid&, const TArray<FIntVector>& Spawns, FString& OutReason)`. GDD 4.2 체크리스트 5개를 각각 독립 함수로 분리해 테스트 가능하게.

교체는 `AVoxelWorld::ServerInitFromSeed`에서 생성기 인스턴스 한 줄만 바꾸면 끝입니다.

> **결정론 함정**: `float` 연산, `FMath::Rand()`, `TMap` 순회, 액터 이터레이션 순서는 전부 플랫폼/실행마다 다를 수 있습니다. 생성기 안에서는 `FRandomStream`과 정수 연산만, 컨테이너는 `TArray`만 쓰세요. 리눅스 데디 서버 ↔ 윈도우 클라 조합에서 이게 깨지면 진단이 매우 어렵습니다.

### 2.8 풀 (`PoolSubsystem.h`)

```cpp
UCLASS()
class UPoolSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void Prewarm(TSubclassOf<AActor> Class, int32 Count);
    AActor* Acquire(TSubclassOf<AActor> Class, const FTransform& T);
    void    Release(AActor* Actor);

    template<typename T>
    T* Acquire(TSubclassOf<T> Class, const FTransform& X)
    { return Cast<T>(Acquire(TSubclassOf<AActor>(Class), X)); }

private:
    TMap<UClass*, TArray<AActor*>> Free;
};

// 풀링 대상 액터가 구현
class IPooledActor
{
public:
    virtual void OnAcquiredFromPool() = 0;   // 초기화
    virtual void OnReleasedToPool() = 0;     // 정리 (타이머/FX 정지 필수)
};
```

**풀링 대상**: 물줄기 세그먼트(~700, 최대 부하 기준), 파편 FX, 위험 구역 데칼, 아이템 픽업.
**풀링 제외**: `ABomb`(최대 40개, 서버 전용 상태를 들고 있어 재사용 시 오염 위험이 이득보다 큼).

풀은 **클라 시각 요소 전용**입니다. 서버 권한 상태를 가진 액터를 풀링하면 리플리케이션과 얽혀 디버깅이 어려워집니다.

---

## 3. 데이터 흐름

### 3.1 폭탄 설치 (로컬 예측 — GDD 6.1)

```
[클라] 입력
  ├─▶ 로컬 검증 (ActiveBombCount < MaxBombCount, 셀 비어있음)
  ├─▶ APredictedBombVisual 스폰    ← 시각만. 타이머·판정 없음
  └─▶ ServerPlaceBomb(Cell)  [Server RPC]
          │
       [서버] 검증 (권위)
          ├─ 실패 ─▶ ClientRejectBomb(Cell)  ─▶ [클라] 예측 비주얼 제거
          └─ 성공 ─▶ ABomb 스폰 (복제됨)
                       └─▶ [클라] BeginPlay
                              └─ 같은 셀의 예측 비주얼 제거
```

핵심: **예측 폭탄은 타이머를 돌리지 않습니다.** 서버 거부 시 이펙트만 지우면 되므로 상태 불일치가 원천적으로 불가능합니다(GDD 6.1의 ⚠️ 항목).

### 3.2 폭발 → 지형 변형

```
[서버] ABomb::OnFuseExpired
  └─▶ ExplosionSubsystem::RequestDetonate(Bomb)
        └─▶ PendingChain에 투입, 타이머 시작
              └─▶ ProcessChainStep()  (ChainStepDelay마다 1단계)
                    ├─ FExplosionResult R = Propagate(Grid, Cell, Range)
                    ├─ VoxelWorld->ServerDestroyBlocks(R.BrokenCells)
                    │     └─▶ Multicast ─▶ 전 클라 그리드+렌더 갱신
                    ├─ Multicast: 물줄기 셀 목록 ─▶ 클라가 풀에서 FX 획득
                    ├─ R.WaterCells 안의 캐릭터 판정 ─▶ StatusComponent::ServerTrap
                    ├─ R.WaterCells 안의 아이템 소멸
                    └─ R.ChainedBombs ─▶ 다음 단계 PendingChain으로
```

**"발판만이 안전하다"(GDD 2.3) 판정 위치**: 물줄기 셀 안의 캐릭터를 찾을 때, 캐릭터의 **발밑 셀**을 기준으로 판정합니다. 점프해서 다른 층으로 올라갔으면 발밑 셀이 달라져 회피 성공, 제자리 점프면 공중이라도 발밑 셀이 그대로여서 피격 — 이게 GDD의 규칙을 정확히 구현합니다.

> 여기서 결정해야 할 미결정 항목: 공중에 있을 때 "발밑 셀"의 정의. 캐릭터 캡슐 하단에서 아래로 레이 대신 `WorldToCell(ActorLocation - FVector(0,0,CapsuleHalfHeight))`로 잡으면 점프 정점에서 한 칸 위가 됩니다. 1주차에 몸으로 튜닝할 값입니다.

### 3.3 블록 파괴 → 낙하 → 낙사

CMC가 이미 처리합니다. `VoxelWorld`가 HISM 컬리전을 갱신하면 캐릭터는 자동으로 떨어집니다. 낙사는 `ACA3DCharacter::Tick`(서버)에서 `GetActorLocation().Z < KillZ` 검사 — 별도 시스템 불필요.

---

## 4. 권한 매트릭스

| 항목 | 서버 | 클라 | 전달 방식 |
|---|---|---|---|
| 그리드 초기 상태 | 생성 | 시드로 재생성 | `Replicated Seed` |
| 블록 파괴 | 결정 | 적용 | `NetMulticast(Cells)` |
| 폭탄 존재 | 소유 | 복제받음 | 액터 리플리케이션 |
| 폭탄 타이머 | 소유 | 예측 표시만 | 복제 안 함 |
| 폭발 결과 | 계산 | 적용 | `NetMulticast(Result)` |
| 캐릭터 이동 | 검증 | 예측 | CMC 기본 |
| 아이템 스탯 | 결정 | 복제받음 | `UPROPERTY(Replicated)` |
| 갇힘/사망 | 결정 | 복제받음 | `ReplicatedUsing` |
| 위험 구역 프리뷰 | — | 로컬 계산 | 전송 없음 (`Propagate` 재사용) |
| 카메라·HUD·FX | — | 로컬 | 전송 없음 |
| 서든데스 낙하 지점 | 결정 | 예고 표시 | `NetMulticast(Cell, Delay)` |

**1주차부터 지킬 규칙**
```cpp
// 상태를 바꾸는 모든 함수 최상단
if (!HasAuthority()) return;

// 시각만 만지는 모든 함수 최상단
if (IsRunningDedicatedServer()) return;
```
이 두 줄을 습관화하면 2주차 "이전 작업"이 사라집니다.

---

## 5. 1주차 구현 순서

GDD 8장 1주차 체크리스트를 의존 순서로 재배열했습니다.

| 순서 | 작업 | 산출물 | 검증 방법 |
|---|---|---|---|
| 1 | 프로젝트 생성 + 서버 타깃 + 폴더 스캐폴딩 | 빌드 통과 | `CrazyArcade3DServer` 타깃이 컴파일되는가 |
| 2 | `FVoxelGrid` + `FallbackMapGenerator` | 데이터만 | 로그로 그리드 덤프 |
| 3 | `AVoxelWorld` + `HISMVoxelRenderer` | 눈에 보이는 맵 | 표면 추출 비율이 20~40%인가 |
| 4 | `ACA3DCharacter` 이동·점프 | 돌아다닐 수 있음 | **셀 크기·이동속도·점프 높이 튜닝** (GDD 10장) |
| 5 | `UPoolSubsystem` | 제네릭 풀 | 물줄기 200개 획득/반납 스트레스 |
| 6 | `ABomb` + `ExplosionSubsystem::Propagate` | 6방향 폭발 | 층간 전파가 맞는가 |
| 7 | 블록 파괴 → 렌더 갱신 → 낙하 | 지형이 무너짐 | 주변 6칸 재검사가 새 표면을 노출하는가 |
| 8 | 연쇄 폭발 프레임 분산 | "촤르륵" | 폭탄 10개 연쇄 시 `stat unit` 스파이크 없음 |
| 9 | 위험 구역 프리뷰 데칼 | 직관성 | `Propagate` 결과와 100% 일치하는가 |
| 10 | 갇힘 상태 + "발판만이 안전하다" | 코어 완성 | 점프 회피가 되는가 |
| 11 | Listen Server PIE 2인 테스트 | 권한 검증 | 두 클라의 지형이 동일한가 |

**⭐ 1번 작업 시점부터 `stat unit`을 켜두세요** (GDD 7.4). 그리고 **11번은 1주차 안에 반드시** — 여기서 어긋나는 게 있으면 2주차가 아니라 지금 고쳐야 비용이 쌉니다.

---

## 6. 이 구조가 방어하는 리스크

| GDD 11장 리스크 | 이 구조의 대응 |
|---|---|
| 절차 맵 생성 시간 소진 | `IMapGenerator` 인터페이스 — 3주차에 생성기가 미완이어도 폴백으로 데모 가능 |
| 지형 변형 리플리케이션 | 서버·클라가 `ApplyDestruction` 단일 경로 공유 + 결과 전송 방식으로 발산 불가 |
| 2~3주 일정 | 1주차 권한 분리로 2주차의 "이전" 작업이 통째로 제거됨 |
| 6방향 폭발 직관성 | 프리뷰가 `Propagate`를 재사용 → 표시와 실제가 구조적으로 일치 |
| 렌더링 성능 부족 | `IVoxelRenderer` 교체로 그리디 메싱 승격 (게임 코드 무수정) |

---

## 7. 아직 안 정한 것

구조가 아니라 **값**이거나 **나중 단계**라서 미룬 항목들입니다.

- **셀 크기 / 이동속도 / 점프 높이** — 1주차 4번 작업에서 몸으로 결정 (GDD 10장)
- **공중에서의 "발밑 셀" 정의** — 3.2절 참고, 1주차 10번에서 튜닝
- **카메라 입력 기준** — WASD가 월드 축 기준인지 카메라 기준인지. 45도 스냅 회전(GDD 5장)과 맞물리므로 4번 작업 때 둘 다 만들어보고 결정
- **EOS 세션 구조** — 2주차. `CA3DGameInstance` 자리만 잡아둠
- **서든데스 낙하 스케줄링** — 3주차. `SuddenDeathSubsystem` 자리만 잡아둠
- **아이템 최대 스택 상한** — 룰셋에 `MaxBombCountCap` / `MaxBombRangeCap`으로 노출은 해뒀고 값은 미정
