# Task 09 — CA3DGameMode

> 선행: Task 02, 04, 06, 08 · 후행: Task 10(스폰 대상), 18(승패), 24(서든데스 발동)
> 체크리스트: `mds/Checklists/09-CA3DGameMode.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ACA3DGameMode` |
| 부모 클래스 | `AGameModeBase` |
| 역할 | **서버 전용** 매치 진행자. 룰셋 소유, 시드 결정, VoxelWorld 초기화, 스폰 배정. (승패 판정은 2주차 Task 18과 함께 확장) |

## 생성 파일

- `Source/CrazyArcade3D/Framework/CA3DGameMode.h/.cpp`
- (에디터) `Content/Blueprints/BP_CA3DGameMode` — DA_Rules_Default 지정
- (에디터) `L_Arena` World Settings → GameMode Override 지정

## 구현 명세

```cpp
// CA3DGameMode.h
// 서버에만 존재하는 매치 진행자. 클라는 이 클래스를 모른다.
UCLASS()
class ACA3DGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ACA3DGameMode();   // DefaultPawnClass·PlayerControllerClass·GameStateClass 지정

    // 매치 시작: 시드 결정 → VoxelWorld->ServerInitFromSeed → GameState에 Rules 세팅.
    virtual void BeginPlay() override;

    // 생성기가 반환한 스폰 셀에 플레이어를 배정한다.
    virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
    // 룰셋 프리셋 — BP 서브클래스에서 DA_Rules_Default 지정 (BP에 로직 금지).
    UPROPERTY(EditDefaultsOnly, Category="CA3D")
    TObjectPtr<UCA3DRuleSet> Rules;

private:
    UPROPERTY() TObjectPtr<AVoxelWorld> VoxelWorld;  // 레벨에서 탐색해 캐시
    TArray<FIntVector> SpawnCells;                   // 생성기 출력
    int32 NextSpawnIndex = 0;
};
```

**흐름**
1. `BeginPlay`(서버): 시드 결정(테스트 중엔 고정 시드 옵션 — 버그 재현용, GDD 4.2 안전장치 2) → `VoxelWorld->ServerInitFromSeed(Seed)` → 생성기 출력의 스폰 셀 보관 → `GameState->Rules = Rules`.
2. 스폰: `ChoosePlayerStart`에서 스폰 셀을 순서대로 배정. `CellToWorldFloor` + 캡슐 높이 보정.

**주의**
- 시드 결정에 `FMath::Rand()`를 써도 되는 유일한 곳이다 (시드 자체는 결정론 대상이 아님). 단, **고정 시드 모드**를 콘솔 변수나 프로퍼티로 반드시 남길 것.
- `SpawnInvulnTime`은 미확정 값 — 룰셋 기본 0으로 두고 로직은 보류.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 시작하면 맵이 생성되고 Pawn(임시 기본 폰이라도)이 스폰 셀 위에 서 있는가.
- 클라(Listen + 1): `GameState->Rules` 복제 확인 — Task 08의 미검증 항목을 여기서 마저 검증.
- 고정 시드 켰을 때 매번 같은 맵인가.

## 응답 원칙

- 공통 원칙.
- DefaultPawn은 Task 10 전까지 임시(엔진 기본 폰 등)일 수 있다 — 임시면 명시.
- 에디터 연결 작업(BP_GameMode·World Settings) 완료 여부를 항목별 보고.
