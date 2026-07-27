# Checklist 04 — FallbackMapGenerator

> 대응 Task: `mds/Tasks/04-FallbackMapGenerator.md`
> **실제로 실행하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-28)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-28)
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] `UObject` + `IMapGenerator` 구현
- [x] 랜덤·float 미사용 (하드코딩 정수 레이아웃, Seed는 `(void)Seed`로 무시 명시)
- [x] 레이아웃에 계단식 접근로가 코드상 존재 — 접근 (8,9,1) → 1단 (9,9,1) → 2단 스택 (10,9,1~2), 좌표 주석 있음

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [x] `Generate` 성공(true) + 그리드 로그 덤프가 의도한 레이아웃과 일치 (층별 ASCII 덤프 확인)
- [x] z=0 전체 Floor(441칸), 외곽 Immortal 벽(z=1~2 두 층) 확인
- [x] 스폰 8개 — 전부 `Empty` 칸 && 아래 칸 `IsSolid` && 탈출로 2방향 이상
- [x] 스폰 간 최소 거리 확보 — 최소 쌍 맨해튼 거리 9 (로그 출력)
- [x] 두 번 호출 결과가 비트 단위 동일 (결정론, 다른 Seed로 호출해 Seed 무시도 함께 증명)
- [x] 2층 이상 블록이 존재하고 접근로 경로가 로그로 확인됨 (z=2 에 (10,9) Immortal)

## 검증 기록

- 2026-07-28 · 전 항목 검증 완료.
  - 빌드: Editor 22초 / Server 35초 — 둘 다 `Result: Succeeded`
  - 동작: 자동화 테스트 `CrazyArcade3D.MapGen.FallbackMapGenerator` 헤드리스 실행 → `Test Completed. Result={Success}` (테스트 코드: `Source/CrazyArcade3D/Tests/FallbackMapGeneratorTests.cpp`)
  - 블록 개수: Empty 1008 / Floor 441 / Destructible 71 / Immortal 244 (총 1764)
  - 스폰: (1,1) (19,1) (1,19) (19,19) (10,1) (10,19) (1,10) (19,10) 전부 z=1
  - 참고: 헤드리스 로그의 `LogAutomationTest: Error: Condition failed` 15건은 스타트업 스모크 스윕에서 엔진 저수준 테스트(`LowLevelTestAdapter.h`의 CHECK, 한국어 로캘 관련)가 낸 것으로 본 프로젝트 테스트와 무관 — 명시 실행 결과는 오류 0건.
