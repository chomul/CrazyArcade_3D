# Checklist 14 — PoolSubsystem

> 대응 Task: `mds/Tasks/14-PoolSubsystem.md`
> **실제로 실행하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] `Prewarm`/`Acquire`/`Release` + 템플릿 헬퍼 — 설계서 2.8과 일치
- [x] Free 리스트가 `UPROPERTY`로 GC 보호됨 (FPooledActorArray 래퍼)
- [x] `IPooledActor` 미구현 액터에 `ensure` (계약 위반 조기 발견 — 이중 Release도 ensure)
- [x] Release 시 숨김+컬리전 off+틱 off
- [x] `TMap` 순회 없음 (조회만)
- [x] `Core/` 폴더 의존 없음 유지

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [x] 200개 Acquire → 전부 Release → 재획득 반복 시 액터 총수 일정 (누수 없음) (2026-07-29 · 자동화 테스트 `CrazyArcade3D.Core.PoolSubsystem` — 200개×5회 반복, 중복 대여 검사 포함)
- [ ] `stat unit` — 반복 중 스파이크 없음 (PIE 영역 — 실사용 Task 16 이후 확인)
- [x] Prewarm 후 첫 Acquire가 신규 스폰 없이 반환 (2026-07-29 · 자동화 테스트 — 액터 총수 불변으로 검증)
- [x] Release된 액터가 보이지 않고 충돌하지 않음 (2026-07-29 · 자동화 테스트 — Hidden·컬리전·틱 상태 검사)
- [x] Acquire 시 `OnAcquiredFromPool`, Release 시 `OnReleasedToPool` 호출 (2026-07-29 · 자동화 테스트 — 호출 횟수·순서까지 검증)
