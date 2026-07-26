# Checklist 24 — SuddenDeathSubsystem

> 대응 Task: `mds/Tasks/24-SuddenDeathSubsystem.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 낙하 지점·시각 결정이 서버 전용, 클라는 `MulticastWarnDrop` 수신만
- [ ] 파괴가 `ServerDestroyBlocks` 단일 경로 경유 (불변식 1)
- [ ] 마커 표시에 데디 가드 + 풀 사용
- [ ] 시작 시각·예고 시간·외곽 가중 룰셋 참조, 신규 수치도 룰셋에 추가됨
- [ ] 매치 종료 시 타이머 정리 (`ServerStop`)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] `SuddenDeathStart`(테스트용 단축값) 도달 → 낙하 시작
- [ ] 예고 마커 → 정확히 그 셀 파괴 (마커≠낙하 셀이면 실패)
- [ ] 예고→낙하 지연이 `DropWarningTime`과 일치, 보고 피할 수 있음
- [ ] 외곽 가중: 낙하 300회 로그 — 외곽 비율이 `OuterWeightBias` 경향과 부합
- [ ] 구멍 낙사 → `EDeathCause::SuddenDeath`
- [ ] 장시간 진행 시 맵 자연 축소 → 매치 종결
- [ ] (Listen+클라) 마커·파괴가 전 클라 동일
