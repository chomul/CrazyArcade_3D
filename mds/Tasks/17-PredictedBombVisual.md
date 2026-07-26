# Task 17 — PredictedBombVisual

> 선행: Task 14, 16 · 후행: 1주차 마감 게이트(2인 PIE)
> 체크리스트: `mds/Checklists/17-PredictedBombVisual.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `APredictedBombVisual` |
| 부모 클래스 | `AActor` + `IPooledActor` 구현 |
| 역할 | 폭탄 설치의 **로컬 예측 비주얼**. 시각 전용 — 타이머·판정·연쇄 없음(불변식 3). 서버가 거부하면 이펙트만 지우면 되므로 상태 불일치가 원천 불가능 |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Bomb/PredictedBombVisual.h/.cpp`
- (수정) `CA3DCharacter.cpp` — 설치 입력 시 로컬 검증 + 비주얼 스폰, `ClientRejectBomb` 핸들러
- (에디터) `Content/Blueprints/BP_PredictedBombVisual` — BP_Bomb과 같은 메시(구분 어려워야 정상)

## 구현 명세

```cpp
// PredictedBombVisual.h
// 클라 전용 예측 비주얼. bReplicates = false. 로직 0.
// 여기에 타이머·폭발 코드를 넣는 순간 불변식 3이 깨진다 — 리뷰에서 무조건 반려.
UCLASS()
class APredictedBombVisual : public AActor, public IPooledActor
{
    GENERATED_BODY()
public:
    FIntVector Cell;   // 어느 셀의 예측인지 — 서버 확정/거부 때 매칭 키

    virtual void OnAcquiredFromPool() override;  // 메시 표시
    virtual void OnReleasedToPool() override;    // 메시 숨김 (돌던 것 없음 확인)
};
```

**흐름 배선 (데이터 흐름 3.1 — 클라 쪽)**

```
[클라] 설치 입력
  ├─ 로컬 검증: ActiveBombCount 예측치 < MaxBombCount, 발밑 셀 Empty, Alive
  ├─ 풀에서 APredictedBombVisual 획득, Cell 기록
  └─ ServerPlaceBomb(Cell)                     // Task 16의 RPC
[서버 확정] ABomb 복제 도착 → ABomb::BeginPlay(클라)에서 같은 Cell의 비주얼 반납
[서버 거부] ClientRejectBomb(Cell) → 같은 Cell의 비주얼 반납 (그게 전부)
```

- 로컬에 떠 있는 예측 비주얼 목록은 캐릭터(로컬)나 컨트롤러가 `TArray`로 관리 — 매칭은 Cell 기준.
- 예측 비주얼은 풀링 대상 (클라 시각 요소).

## 검증 원칙

- 공통 원칙 + 아래.
- PIE(Listen + 클라 1, Network Emulation로 지연 ≥100ms 권장):
  - 클라에서 설치 → **즉시** 비주얼 표시 → 잠시 후 진짜 ABomb으로 교체 (겹침·깜빡임 없음, 비주얼 2개 동시 표시 없음).
  - 서버 거부 상황(개수 초과 연타) → 예측 비주얼만 사라지고 아무 일도 없음.
  - 예측 비주얼이 시간이 지나도 **혼자 터지지 않는가** (타이머 없음 증명).
- **1주차 마감 게이트 — Listen Server PIE 2인**: 두 클라이언트가 폭탄·폭발·파괴를 주고받은 뒤 양쪽 지형(그리드 해시 로그)이 동일한가 (구조 설계서 5장 11번).

## 응답 원칙

- 공통 원칙.
- 2인 테스트의 그리드 일치 여부를 **해시/덤프 비교 결과**로 보고한다 — "잘 되는 것 같음"은 불충분.
- 지연 에뮬레이션 값을 명시한다.
