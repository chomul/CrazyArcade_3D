# Checklist 18 — CA3DPlayerState

> 대응 Task: `mds/Tasks/18-CA3DPlayerState.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `ColorIndex`/`FinalRank`/`bAlive` 복제 등록
- [ ] 상태 갱신은 서버(GameMode 경유)만 — PlayerState에 판정 로직 없음
- [ ] 전적 저장 코드 없음 (GDD: 저장 없음)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 접속 순서대로 `ColorIndex` 배정, 전 클라 동일하게 보임
- [ ] (3인) 1명 사망 → 전 클라에서 `bAlive=false`, `FinalRank=3`, `AliveCount=2`
- [ ] 최후 1인 → 매치 종료, 우승자 `FinalRank=1`
- [ ] 사망자가 관전 상태로 남고 매치는 계속 (유령 방해 없음)
- [ ] 동시 사망 순위 규칙 — **사용자 확정 후** 그 규칙대로 동작 (확정 전 미검증)
