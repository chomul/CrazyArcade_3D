# Task 02 — CA3DRuleSet

> 선행: Task 01 · 후행: Task 03(생성기 시그니처), 08(복제), 09(소유)
> 체크리스트: `mds/Checklists/02-CA3DRuleSet.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UCA3DRuleSet` |
| 부모 클래스 | `UPrimaryDataAsset` |
| 역할 | 게임의 **모든 튜닝 값**을 담는 데이터 에셋. 코드에 매직 넘버를 두지 않기 위한 단일 출처. GameMode가 소유하고 GameState로 포인터 복제 |

## 생성 파일

- `Source/CrazyArcade3D/Framework/CA3DRuleSet.h/.cpp`
- (에디터) `Content/Data/DA_Rules_Default` — 이 클래스의 데이터 에셋 인스턴스

## 구현 명세

```cpp
// CA3DRuleSet.h
// 매치 규칙·튜닝 값의 단일 출처. 인스턴스를 여러 개 만들어 룰셋 프리셋으로 쓴다.
// GameMode(서버)가 소유하되 GameState에 에셋 "포인터"를 복제한다 —
// UE는 에셋 참조를 경로로 복제하므로 값 전체가 아니라 참조만 오간다.
UCLASS(BlueprintType)
class UCA3DRuleSet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    // ─── 폭탄 ───
    UPROPERTY(EditAnywhere, Category="Bomb") float BombFuseTime = 3.f;    // 설치→폭발 시간
    UPROPERTY(EditAnywhere, Category="Bomb") float WaterLingerTime = 1.f; // 물줄기 잔존 시간
    UPROPERTY(EditAnywhere, Category="Bomb") float ChainStepDelay = 0.07f;// 연쇄 1단계 간격
    UPROPERTY(EditAnywhere, Category="Bomb") int32 MaxBombCountCap = 8;   // 폭탄 개수 스택 상한 (값 미확정)
    UPROPERTY(EditAnywhere, Category="Bomb") int32 MaxBombRangeCap = 6;   // 폭발 범위 스택 상한 (값 미확정)

    // ─── 생존 ───
    UPROPERTY(EditAnywhere, Category="Life") float TrappedDuration = 4.f;  // 갇힘 지속 (3~5초 권장)
    UPROPERTY(EditAnywhere, Category="Life") float TrappedMoveSpeed = 60.f;// 갇힌 상태 미세 이동 속도
    UPROPERTY(EditAnywhere, Category="Life") float SpawnInvulnTime = 0.f;  // 스폰 무적 (유무 미확정)

    // ─── 맵 ───
    UPROPERTY(EditAnywhere, Category="Map") bool bFloorDestructible = false; // 바닥 파괴 허용 (미확정)
    UPROPERTY(EditAnywhere, Category="Map") float ItemDropRate = 0.3f;       // 아이템 배치율
    UPROPERTY(EditAnywhere, Category="Map") FIntVector MapSize{21,21,4};     // 그리드 크기

    // ─── 서든데스 ───
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float SuddenDeathStart = 150.f; // 발동 시각(초)
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float DropWarningTime = 1.5f;   // 낙하 예고 시간
    UPROPERTY(EditAnywhere, Category="SuddenDeath") float OuterWeightBias = 2.f;    // 외곽 낙하 가중
};
```

**에디터 연결 작업**
1. 빌드 후 에디터에서 `Content/Data/` 에 `DA_Rules_Default` 생성 (우클릭 → Miscellaneous → Data Asset → `CA3DRuleSet`).
2. 기본값 그대로 저장. 이후 Task 09에서 GameMode가 이 에셋을 참조한다.

## 검증 원칙

- 공통 원칙 + 아래.
- 위 기본값들은 GDD의 권장치일 뿐 **확정값이 아니다** — 값 변경은 코드 수정 없이 에셋에서만 가능해야 한다.
- 이후 어떤 Task에서도 이 값들을 코드에 하드코딩하지 않는지가 이 클래스의 존재 이유다.

## 응답 원칙

- 공통 원칙.
- 에디터에서 `DA_Rules_Default`를 실제로 만들었는지 여부를 명시한다 (에디터 미실행이면 "에셋 생성 미완 — 에디터 작업 필요"로 보고).
