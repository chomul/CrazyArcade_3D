# Task 12 — StatusComponent

> 선행: Task 03(ItemTypes), 10 · 후행: Task 15/16(피격·설치 검증), 23(아이템 적용)
> 체크리스트: `mds/Checklists/12-StatusComponent.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UStatusComponent` |
| 부모 클래스 | `UActorComponent` |
| 역할 | 캐릭터의 아이템 스탯 + 생존 상태(갇힘/사망). 컴포넌트로 분리해 **봇과 플레이어가 완전히 같은 코드 경로**를 타게 한다. 상태 변경은 전부 서버 전용 진입점 |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/Character/StatusComponent.h/.cpp`

## 구현 명세

```cpp
// StatusComponent.h
// 생존 상태 (GDD 2.3). Trapped: 물방울에 갇힘 — 니들로만 탈출, 미세 이동만 가능.
UENUM()
enum class ELifeState : uint8 { Alive, Trapped, Dead, Spectating };

UENUM()
enum class EDeathCause : uint8 { Water, Fall, SuddenDeath };

// 캐릭터에 부착되는 상태 컴포넌트. 스탯·생존 상태의 단일 출처.
// 모든 Server* 함수 최상단: if (!GetOwner()->HasAuthority()) return;
UCLASS(ClassGroup=(CA3D), meta=(BlueprintSpawnableComponent))
class UStatusComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // ─── 아이템 스탯 (GDD 3장 — Replicated 변수 5개면 충분) ───
    UPROPERTY(ReplicatedUsing=OnRep_Stats) int32 MaxBombCount = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Stats) int32 BombRange    = 1;
    UPROPERTY(ReplicatedUsing=OnRep_Stats) float MoveSpeedMul = 1.f;
    UPROPERTY(Replicated) bool bHasNeedle = false;
    UPROPERTY(Replicated) bool bHasKick   = false;

    // ─── 생존 상태 ───
    UPROPERTY(ReplicatedUsing=OnRep_Life) ELifeState LifeState = ELifeState::Alive;

    int32 ActiveBombCount = 0;   // 서버 전용 — 복제 불필요. 설치 +1 / 폭발 -1.

    // ─── 서버 전용 진입점 ───
    void ServerApplyItem(EItemType Item);   // 스탯 증가는 룰셋 Cap으로 클램프
    void ServerTrap();                      // Alive → Trapped. TrappedDuration 타이머 시작
    void ServerEscape();                    // Trapped + bHasNeedle → Alive. 니들 소모
    void ServerKill(EDeathCause Cause);     // → Dead. 타이머 정리. GameMode에 통지

protected:
    UFUNCTION() void OnRep_Stats();  // 클라: 이동속도 반영 등 시각/CMC 갱신
    UFUNCTION() void OnRep_Life();   // 클라: 갇힘 비주얼·관전 전환 (데디 가드)

private:
    FTimerHandle TrappedTimer;       // 만료 시 ServerKill(Water)
};
```

**규칙 연결**
- `ServerTrap` 중 이동 속도는 `Rules->TrappedMoveSpeed` — 캐릭터 CMC에 반영.
- 갇힘 시간·Cap은 전부 룰셋에서. 매직 넘버 금지.
- `ServerKill` → Task 10의 낙사 검사(`KillZ`)가 `ServerKill(Fall)` 호출로 연결된다.
- 갇힌 채 구멍에 빠지는 상황(미결정 항목)은 **막지 않고 그대로 둔다** — 정책 확정 시 수정.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE(Listen + 클라 1): 서버에서 `ServerApplyItem(Balloon)` 강제 호출 → 클라 `MaxBombCount` 복제 확인.
- `ServerTrap` → `TrappedDuration` 후 자동 사망하는가. 그 사이 `ServerEscape`(니들 보유 시)로 살아나는가.
- 낙사: 맵 밖 추락 → `ServerKill(Fall)` → `LifeState == Dead` 복제.
- 클라에서 Server* 함수를 불러도 상태가 안 바뀌는가 (권한 가드).

## 응답 원칙

- 공통 원칙.
- 상태 전이표(Alive↔Trapped→Dead)를 실제 검증한 경로만 체크하고 나머지는 미검증으로 남긴다.
