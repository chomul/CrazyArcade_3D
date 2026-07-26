# Task 22 — ProcMapGenerator (3주차)

> 선행: Task 03, 04, 21 · 후행: 데모 맵 다양화
> ⚠️ 일정 리스크 1순위 (GDD 11장) — 미완이어도 폴백으로 데모 가능해야 한다
> 체크리스트: `mds/Checklists/22-ProcMapGenerator.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UProcMapGenerator` |
| 부모 클래스 | `UObject` + `IMapGenerator` 구현 |
| 역할 | 시드 기반 **결정론적** 절차 맵 생성 — 서버가 시드 4바이트만 보내면 모든 클라가 같은 맵을 재생성한다(불변식 4). 생성→검증(Task 21)→리롤 루프 |

## 생성 파일

- `Source/CrazyArcade3D/MapGen/ProcMapGenerator.h/.cpp`
- (수정) `AVoxelWorld::ServerInitFromSeed` — 생성기 인스턴스 1줄 교체 (구조 결정 10)

## 구현 명세

```cpp
// ProcMapGenerator.h
// 결정론 계약(불변식 4): 같은 Seed + 같은 Rules ⇒ 항상 같은 출력.
// 내부에서 허용되는 것: FRandomStream(Seed), 정수 연산, TArray, 고정 순서 루프.
// 금지: float 연산, FMath::Rand/FRand, TMap/TSet "순회", 액터 이터레이션.
// 리눅스 데디 ↔ 윈도우 클라에서 시드 재현이 깨지면 진단이 매우 어렵다.
UCLASS()
class UProcMapGenerator : public UObject, public IMapGenerator
{
    GENERATED_BODY()
public:
    virtual bool Generate(uint32 Seed, const UCA3DRuleSet* Rules,
                          FVoxelGrid& OutGrid,
                          TArray<FIntVector>& OutSpawns,
                          TArray<FItemPlacement>& OutItems) override;

private:
    // 생성 단계 — 전부 FRandomStream& 을 받는 private 헬퍼로 분리 (단계별 테스트 가능):
    // 1) 지형 베이스: 평지 + 산·협곡 (정수 높이맵)
    // 2) 계단식 접근로 보장 — 모든 층은 1블록 점프로 도달 가능해야 함 (GDD 2.1)
    // 3) Immortal 골격 + Destructible 채움 (밀도는 스트림 롤)
    // 4) 스폰 8개 배치 (외곽 우선, 최소 거리)
    // 5) 아이템 배치 — Destructible 안에, Rules->ItemDropRate. 니들은 낮은 확률 (GDD 3장)
    //
    // 리롤 루프: Generate 내부에서 FMapValidator::Validate 불통과 시
    // 파생 시드(Seed + attempt)로 재생성. 시도 상한(예: 16회) 초과 시 false 반환
    // → 호출부(VoxelWorld)가 FallbackMapGenerator로 폴백 (GDD 4.2 안전장치 3).
};
```

**주의**
- 시도 상한·밀도 등 수치는 프로퍼티로 노출 (매직 넘버 금지).
- 리롤도 결정론이어야 한다 — 파생 시드 규칙을 고정식으로 (예: `Seed * 7919 + attempt`).

## 검증 원칙

- 공통 원칙 + 아래.
- **결정론 (핵심)**: 같은 시드로 5회 생성 → 그리드 해시 전부 동일. 다른 시드 10개 → 서로 다른 맵 + 전부 `Validate` 통과 로그.
- 리롤 로그: 시드별 시도 횟수·실패 사유(`OutReason`) 출력 — 리롤률이 지나치게 높으면(예: >50%) 생성 규칙부터 보정.
- 폴백 전환: 강제로 항상 실패시켜 `FallbackMapGenerator` 폴백이 실제로 작동하는지 1회 확인.
- PIE: 절차 맵에서 이동·점프·폭발·낙하가 폴백 맵과 동일하게 동작.
- (가능하면) 리눅스 서버 ↔ 윈도우 클라 시드 재현 확인 — 미실시면 미검증으로.

## 응답 원칙

- 공통 원칙.
- 결정론 검증을 **해시 값**으로 보고한다.
- 리롤률 통계를 보고하고, 튜닝이 필요한 수치는 질문한다.
- 미완이어도 폴백으로 게임이 도는 상태인지를 항상 명시한다 (리스크 1순위 관리).
