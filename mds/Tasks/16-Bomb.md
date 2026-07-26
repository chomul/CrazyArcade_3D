# Task 16 — Bomb

> 선행: Task 09, 10, 12, 14, 15 · 후행: Task 17(예측 비주얼 교체), 20(킥·봇)
> 체크리스트: `mds/Checklists/16-Bomb.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `ABomb` |
| 부모 클래스 | `AActor` |
| 역할 | **서버 권한** 폭탄. 타이머·판정·연쇄는 100% 서버 소유(불변식 3의 서버 쪽 절반). 클라에는 액터 리플리케이션으로 존재만 복제. **풀링 금지** |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Bomb/Bomb.h/.cpp`
- (수정) `CA3DCharacter.h/.cpp` — `ServerPlaceBomb` RPC 추가 (설치 경로 배선)
- (에디터) `Content/Blueprints/BP_Bomb` — 메시·이펙트 지정

## 구현 명세

```cpp
// Bomb.h
// 서버 권한 폭탄. bReplicates = true. 풀링하지 않는다 —
// 서버 권한 상태를 가진 액터는 재사용 시 상태 오염 위험이 이득보다 크다.
UCLASS()
class ABomb : public AActor
{
    GENERATED_BODY()
public:
    // 서버: 스폰 직후 호출. 소유자·범위 기록, Rules->BombFuseTime 타이머 시작.
    void ServerArm(ACA3DCharacter* InOwner, int32 InRange, const FIntVector& InCell);

    FIntVector GetCell() const { return Cell; }
    int32      GetRange() const { return Range; }

    // 서브시스템이 연쇄 유발 시 호출 — 남은 퓨즈를 무시하고 이번 연쇄 단계에 합류.
    // bDetonated 플래그로 중복 폭발 방지.
    void ServerForceDetonate();

    virtual void BeginPlay() override;  // 클라: 같은 셀의 APredictedBombVisual 제거(Task 17에서 배선)
                                        //       + 위험 프리뷰 데칼 표시(아래 참조)

protected:
    UPROPERTY(Replicated) FIntVector Cell;      // 설치 셀 (그리드 판정용)
    UPROPERTY(Replicated) int32      Range = 1; // 클라 프리뷰 계산에 필요해 복제

private:
    UPROPERTY() TObjectPtr<ACA3DCharacter> OwnerChar; // 서버 전용 — 폭발 시 ActiveBombCount 반환
    FTimerHandle FuseTimer;                            // 서버 전용 — 클라는 타이머를 모른다 (불변식 3)
    bool bDetonated = false;
    void OnFuseExpired();   // 서버: ExplosionSubsystem->RequestDetonate(this)
};
```

**설치 경로 (Character에 추가 — 데이터 흐름 3.1의 서버 쪽)**

```cpp
// CA3DCharacter.h 에 추가
// 클라→서버: 발밑 셀에 폭탄 설치 요청. 서버가 권위 검증 후 ABomb 스폰.
UFUNCTION(Server, Reliable) void ServerPlaceBomb(FIntVector Cell);
// 서버→해당 클라: 설치 거부 통보 — 예측 비주얼 제거용 (핸들러 구현은 Task 17).
UFUNCTION(Client, Reliable) void ClientRejectBomb(FIntVector Cell);
```

서버 검증: `ActiveBombCount < MaxBombCount` && 셀 Empty && 그 셀에 폭탄 없음 && `LifeState == Alive`. 성공 → `ABomb` 스폰 + `ServerArm` + `ActiveBombCount++`. 실패 → `ClientRejectBomb`.

**위험 프리뷰 데칼 (클라 시각 — 구조 설계서 5장 9번)**
- `BeginPlay`(클라, 데디 가드)에서 `Propagate(Grid, Cell, Range, ...)` 호출 → `WaterCells`에 풀(Task 14)에서 데칼 획득·배치. 폭발/파괴 시 반납.
- 프리뷰가 실폭발과 **같은 함수**를 쓰므로 표시와 실제가 어긋날 수 없다 — 별도 계산 로직을 만들면 구조 위반.

**폭발 연출 (클라)**: 서브시스템의 물줄기 Multicast 수신 → 풀에서 물줄기 세그먼트 FX 획득 → `WaterLingerTime` 후 반납. 위쪽 분수·아래쪽 폭포 연출(GDD 5장)은 BP/나이아가라 — 3주차 아트 패스로 미룸.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: 설치 키 → 3초(룰셋 값) 후 6방향 폭발 → 블록 파괴 + 렌더 갱신(주변 6칸 노출) → 파괴된 바닥 위에 있던 캐릭터 낙하.
- 물줄기 피격 → `ServerTrap` (제자리 점프로는 못 피하고, 다른 발판에 올라가면 회피 — "발판만이 안전하다").
- **연쇄**: 폭탄 2개 인접 설치 → 하나 터지면 `ChainStepDelay` 간격으로 연쇄. 폭탄 10개 연쇄 시 `stat unit` 스파이크 없음 (구조 설계서 5장 8번).
- 프리뷰 데칼이 실제 폭발 범위와 100% 일치 (5장 9번).
- `MaxBombCount` 초과 설치가 거부되는가.

## 응답 원칙

- 공통 원칙.
- 이 Task는 검증 항목이 많다 — 체크리스트 항목별로 PIE 확인/미검증을 구분해 보고한다.
- 연쇄 중복 폭발 방지(bDetonated)가 실제로 동작하는지(같은 폭탄이 두 단계에 안 들어가는지) 명시한다.
