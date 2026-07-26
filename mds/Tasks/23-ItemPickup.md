# Task 23 — ItemPickup (3주차)

> 선행: Task 03, 12, 14, 16 · 후행: 데모 밸런스 패스
> 체크리스트: `mds/Checklists/23-ItemPickup.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `AItemPickup` |
| 부모 클래스 | `AActor` |
| 역할 | 맵에 놓인 아이템 1개. 획득(서버 판정 → `StatusComponent::ServerApplyItem`)과 폭발 소멸("적이 못 먹게 태우기" — GDD 3장)을 처리 |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Item/ItemPickup.h/.cpp`
- (수정) `ExplosionSubsystem::ProcessChainStep` — WaterCells 안 아이템 소멸 배선 (Task 15의 5번 단계)
- (수정) 블록 파괴 경로 — Destructible 파괴 시 숨은 아이템 노출 스폰
- (에디터) `Content/Blueprints/BP_ItemPickup_*` 5종 — 메시·아이콘 지정

## 구현 명세

```cpp
// ItemPickup.h
// 서버 권한 아이템 액터. bReplicates = true.
// 스폰 흐름: 맵 생성 시 FItemPlacement로 Destructible 블록 "안"에 예약돼 있다가
// 그 블록이 파괴되면 서버가 이 액터를 노출 스폰한다 (크아식 — GDD 3장).
UCLASS()
class AItemPickup : public AActor
{
    GENERATED_BODY()
public:
    // 서버: 스폰 직후 종류·셀 지정.
    void ServerInit(EItemType InType, const FIntVector& InCell);

    FIntVector GetCell() const { return Cell; }

    // 서버: 물줄기에 닿아 소멸. 획득과 달리 효과 없이 사라진다 — 심리전 요소.
    void ServerBurn();

protected:
    UPROPERTY(ReplicatedUsing=OnRep_Type) EItemType Type;
    UPROPERTY(Replicated) FIntVector Cell;

    // 서버: 캐릭터 오버랩 → Alive 확인 → StatusComponent->ServerApplyItem(Type) → 제거.
    UFUNCTION() void OnOverlap(AActor* OverlappedActor, AActor* OtherActor);

    UFUNCTION() void OnRep_Type();  // 클라: 종류별 메시/머티리얼 표시 (데디 가드)
};
```

**풀링 여부 (결정 필요)**: 설계서 2.8은 아이템 픽업을 풀링 대상에 넣었지만, 프로젝트 규칙은 "풀링은 클라 시각 요소만"이고 이 액터는 서버 권한·복제 상태를 가진다. **1차 구현은 스폰/디스트로이**로 하고, 개수가 문제 되면(수십 개 수준이라 가능성 낮음) 질문 후 풀링 전환한다.

**획득 규칙**: 갇힌(Trapped) 상태 획득 가능 여부는 미결정 — 1차는 Alive만 획득, 확정은 질문.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE(Listen + 클라 1): 블록 파괴 → 아이템 노출 → 획득 → 스탯 증가 복제 (풍선→개수, 물약→범위, 롤러→속도 체감).
- 니들: 갇힘 → 니들 사용 → 탈출, 니들 소모(1회성).
- 킥: `bHasKick` 획득 플래그만 검증 (차기 동작 자체가 미구현이면 미검증 명시).
- 물줄기에 닿은 아이템이 효과 없이 소멸하는가.
- 스택 상한: Cap 초과 획득 시 클램프되는가.

## 응답 원칙

- 공통 원칙.
- 아이템 5종별 검증 여부를 개별 보고한다 (킥의 실제 차기 구현 범위는 별도 확인 요청).
- 풀링/획득 규칙 등 위 "결정 필요" 항목의 선택지를 정리해 질문한다.
