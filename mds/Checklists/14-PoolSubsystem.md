# Checklist 14 — PoolSubsystem

> 대응 Task: `mds/Tasks/14-PoolSubsystem.md`
> **실제로 실행하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `Prewarm`/`Acquire`/`Release` + 템플릿 헬퍼 — 설계서 2.8과 일치
- [ ] Free 리스트가 `UPROPERTY`로 GC 보호됨
- [ ] `IPooledActor` 미구현 액터에 `ensure` (계약 위반 조기 발견)
- [ ] Release 시 숨김+컬리전 off+틱 off
- [ ] `TMap` 순회 없음 (조회만)
- [ ] `Core/` 폴더 의존 없음 유지

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [ ] 200개 Acquire → 전부 Release → 재획득 반복 시 액터 총수 일정 (누수 없음)
- [ ] `stat unit` — 반복 중 스파이크 없음
- [ ] Prewarm 후 첫 Acquire가 신규 스폰 없이 반환 (로그)
- [ ] Release된 액터가 보이지 않고 충돌하지 않음
- [ ] Acquire 시 `OnAcquiredFromPool`, Release 시 `OnReleasedToPool` 호출 (로그)
