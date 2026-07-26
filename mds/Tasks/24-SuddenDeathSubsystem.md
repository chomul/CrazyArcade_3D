# Task 24 — SuddenDeathSubsystem (3주차)

> 선행: Task 06, 09, 15 · 후행: 데모 매치 페이싱 튜닝
> 체크리스트: `mds/Checklists/24-SuddenDeathSubsystem.md`

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `USuddenDeathSubsystem` |
| 부모 클래스 | `UWorldSubsystem` |
| 역할 | 서든데스 — 하늘에서 물폭탄 랜덤 낙하 → 블록 파괴 → 구멍 → 낙사로 매치 자연 축소 (GDD 2.4). 낙하 지점 결정은 서버, 클라는 예고 마커만 |

## 생성 파일

- `Source/CrazyArcade3D/Gameplay/SuddenDeath/SuddenDeathSubsystem.h/.cpp`
- (수정) `CA3DGameMode` — `Rules->SuddenDeathStart` 도달 시 `ServerStart()` 호출

## 구현 명세

```cpp
// SuddenDeathSubsystem.h
// 서든데스 낙하 스케줄러. 모든 결정은 서버 — 낙하 지점·시각은
// NetMulticast(Cell, Delay)로 예고만 전달한다 (권한 매트릭스 마지막 행).
UCLASS()
class USuddenDeathSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // 서버: GameMode가 매치 시각 SuddenDeathStart에 호출. 낙하 루프 시작.
    void ServerStart();
    void ServerStop();   // 매치 종료 시 정리

protected:
    // 전 클라: 낙하 예고 — 대상 셀 위에 마커/그림자 표시 (1~2초, Rules->DropWarningTime).
    // 시각 전용 — 최상단 데디 가드. 마커는 풀(Task 14)에서.
    UFUNCTION(NetMulticast, Reliable)
    void MulticastWarnDrop(FIntVector Cell, float Delay);

private:
    FTimerHandle DropTimer;
    FRandomStream Stream;    // 서버 전용 — 낙하 지점 롤. 클라 재현 불필요(결과만 전송)

    // 서버: 주기마다 1회 —
    // 1) 외곽 가중 랜덤 셀 선정 (Rules->OuterWeightBias — 중심 거리 비례 가중치, 정수 연산)
    // 2) MulticastWarnDrop(Cell, Rules->DropWarningTime)
    // 3) DropWarningTime 후: Propagate 기반 소형 폭발 또는 해당 기둥 파괴 →
    //    VoxelWorld->ServerDestroyBlocks + 피격 판정 (EDeathCause::SuddenDeath)
    void ProcessDrop();
};
```

**주의**
- 낙하 주기·1회 파괴 규모는 룰셋에 새 프로퍼티로 추가 (매직 넘버 금지). 기본값은 제안 후 질문.
- 낙하 파괴도 반드시 `ServerDestroyBlocks` 단일 경로(불변식 1) — 그리드 직접 수정 금지.
- HUD 서든데스 경고(GDD 5장)는 GameState 플래그로 노출 → Task 26이 소비.

## 검증 원칙

- 공통 원칙 + 아래.
- PIE: `SuddenDeathStart` 도달(테스트용 짧은 값) → 예고 마커 → 정확히 그 셀 파괴.
- 예고→낙하 지연이 `DropWarningTime`과 일치 (마커 보고 피할 수 있는가 — 필수 요건).
- 외곽 가중: 낙하 300회 시뮬레이션 로그로 외곽:중앙 비율이 `OuterWeightBias` 경향과 맞는가.
- 구멍으로 낙사 → `EDeathCause::SuddenDeath` 집계.
- 장시간 방치 시 맵이 자연 축소되어 매치가 끝나는가 (페이싱은 튜닝 항목으로 보고만).

## 응답 원칙

- 공통 원칙.
- 룰셋에 추가한 프로퍼티 목록과 제안 기본값을 보고하고 확정을 질문한다.
- 외곽 가중 시뮬레이션 수치를 보고에 포함한다.
