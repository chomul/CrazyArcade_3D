# Checklist 15 — ExplosionSubsystem

> 대응 Task: `mds/Tasks/15-ExplosionSubsystem.md`
> **실제로 실행(자동화 테스트)하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**
> `RequestDetonate` 연쇄 실검증은 Task 16 이후 — 그 전엔 미검증 유지.

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] `Propagate`가 **static + 부작용 없음** — 멤버·전역 접근 없음, 입력만 읽음 (불변식 2)
- [x] 6방향(±X±Y±Z) 전파 규칙이 GDD 2.2와 일치 (Immortal 차단 / Destructible 부수고 멈춤 / Floor 룰 분기 / 연쇄 후 전파 계속)
- [x] `RequestDetonate`·`ProcessChainStep` 서버 가드 — Task 16에서 구현 완료 (2026-07-29 · 진입점 RequestDetonate 가 `Bomb->HasAuthority()` 가드, ProcessChainStep 은 private — 서버 경로에서만 도달)
- [x] `ChainStepDelay` 룰셋 참조 — 소비처(ProcessChainStep 다음 단계 타이머) Task 16에서 구현 (2026-07-29)
- [x] 시그니처 확장(bFloorDestructible, BombCells)이 문서 보고와 일치

## 동작 검증 — Propagate 유닛 테스트 (실행 필수 — 미실행 시 미검증)
> 2026-07-29 · `CrazyArcade3D.Gameplay.ExplosionSubsystem` 헤드리스 통과 — 아래 전 항목
- [x] 빈 그리드: Range만큼 6방향 전파, WaterCells 수 일치 (1+6×Range)
- [x] Immortal 차단: 그 방향 즉시 멈춤, 셀 미포함
- [x] Destructible: BrokenCells 포함 + 그 방향 멈춤
- [x] Floor: `bFloorDestructible` true/false 분기 동작
- [x] **층간(±Z) 전파** 확인 (+Z 파괴·-Z 차단 케이스)
- [x] BombCells 위 폭탄 → ChainedCells 검출 + 전파는 계속 (원점 폭탄은 연쇄 제외)
- [x] 같은 입력 2회 → 같은 출력 (순수성 — 입력 그리드 불변까지 확인)

## 동작 검증 — 연쇄 (Task 16 이후 PIE)
- [x] `RequestDetonate` → `ChainStepDelay` 간격 단계 처리 (로그 타임스탬프) — 2026-07-30 PIE: 4단 연쇄의 단계 간격 **78 / 70 / 74ms** (룰셋 `ChainStepDelay` 0.07초와 일치)
