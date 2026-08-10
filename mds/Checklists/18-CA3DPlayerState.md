# Checklist 18 — CA3DPlayerState

> 대응 Task: `mds/Tasks/18-CA3DPlayerState.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-30 · 번역 단위 병합 강제 `-ForceUnity -DisableAdaptiveUnity`)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-30 · 동일)
- [x] 프로젝트 파일 재생성 실행 (2026-07-30)

## 코드 검증 (정적)
- [x] `ColorIndex`/`FinalRank`/`bAlive` 복제 등록 (자동화 테스트가 `GetLifetimeReplicatedProps` 등록까지 확인)
- [x] 상태 갱신은 서버(GameMode 경유)만 — PlayerState 에 판정 로직 없음 (순수 데이터 3필드, `NotifyPlayerDeath`/`ResolvePendingDeaths` 최상단 `HasAuthority` 가드)
- [x] 전적 저장 코드 없음 (GDD 6.3 — 매치 종료 시 값이 함께 사라진다)

## 동작 검증 — 자동화 (2026-07-30 · `CrazyArcade3D.Framework.PlayerState` 헤드리스 통과)
- [x] 동시 사망 순위 규칙 — **사용자 확정(공동 등수 + 무승부)대로 동작**. 로그 근거:
      순차 4인 = `4등 → 3등 → 2등 + 우승`(최종 1·2·3·4) / 동시 2명 = `공동 2등, 생존 3→1, 우승자 확정`(1·2·2·4)
      / 동시 3명 = `공동 2등, 생존 4→1` / **무승부** = `공동 2등, 생존 2→0 · 매치 종료: 무승부(우승자 없음)`
- [x] `AliveCount` 가 동시 사망 인원만큼 정확히 감소 (3명 동시 → 4→1)
- [x] 최소 인원 게이트 — 참가 1명이면 종료 판정 없음 (`MinPlayersForMatchEnd` 기본 2)
- [x] 종료된 매치에 추가 사망 통지 → 상태 불변 (중복 종료 방지)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 접속 순서대로 `ColorIndex` 배정, 전 클라 동일하게 보임
- [ ] (3인) 1명 사망 → 전 클라에서 `bAlive=false`·`FinalRank`·`AliveCount` 복제
- [ ] 최후 1인 → 매치 종료, 우승자 `FinalRank=1` (헤드리스로는 통과 — 실제 복제 경로 미확인)
- [ ] 사망자가 관전 상태로 남고 매치는 계속 (유령 방해 없음)
      → ⚠️ **미구현**: `ELifeState::Spectating` 전환·입력 차단이 아직 없다 (`ServerKill` 은 `Dead` 까지).
      사망 처리 방향(이동 불가 + 캐릭터 파괴/풀 반납, 부활 여지)은 `mds/tasks.md` 메모 참조 — 별도 Task 필요

## 보류 → 해소됨
- [x] **중도 이탈(`Logout`)** — 2026-08-10 사용자 확정: **"이탈 = 그 자리에서 탈락(순위 부여) + 결과 화면에 (탈주)"**.
      참가 인원에서는 빼지 않는다. 구현·검증은 `mds/Checklists/35-MatchLeave.md`
