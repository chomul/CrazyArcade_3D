# Checklist 08 — CA3DGameState

> 대응 Task: `mds/Tasks/08-CA3DGameState.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**
> 복제 실검증은 Task 09(GameMode 세팅) 이후에만 가능 — 그 전엔 미검증 유지.

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] `Rules`가 `UPROPERTY(Replicated) TObjectPtr<UCA3DRuleSet>` — 에셋 포인터 복제
- [x] `GetLifetimeReplicatedProps`에 전 복제 프로퍼티 등록 (Rules·AliveCount·MatchStartServerTime 3종)
- [x] 로직 없음 — 데이터 보관만 (갱신은 GameMode, 소비는 UI)
- [x] 매직 넘버 없음

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [x] (Listen+클라 1, Task 09 이후) 클라 `GameState->Rules != nullptr` (2026-07-29 · 사용자 PIE 확인)
- [x] 클라에서 읽은 `BombFuseTime` 등이 `DA_Rules_Default` 값과 일치 (2026-07-29 · 클라 맵 재생성이 복제 룰셋 기반으로 정상 동작)
- [ ] `AliveCount`·`MatchStartServerTime` 복제 확인 — AliveCount 갱신 로직 자체가 Task 18에서 생기므로 그때 실검증
