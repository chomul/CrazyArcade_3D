# Checklist 04 — FallbackMapGenerator

> 대응 Task: `mds/Tasks/04-FallbackMapGenerator.md`
> **실제로 실행하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `UObject` + `IMapGenerator` 구현
- [ ] 랜덤·float 미사용 (하드코딩 정수 레이아웃)
- [ ] 레이아웃에 계단식 접근로가 코드상 존재 (좌표 주석 확인)

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [ ] `Generate` 성공(true) + 그리드 로그 덤프가 의도한 레이아웃과 일치
- [ ] z=0 전체 Floor, 외곽 Immortal 벽 확인
- [ ] 스폰 8개 — 전부 `Empty` 칸 && 아래 칸 `IsSolid`
- [ ] 스폰 간 최소 거리 확보 (로그로 최소 쌍 거리 출력)
- [ ] 두 번 호출 결과가 비트 단위 동일 (결정론)
- [ ] 2층 이상 블록이 존재하고 접근로 경로가 로그로 확인됨
