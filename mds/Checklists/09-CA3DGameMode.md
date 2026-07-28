# Checklist 09 — CA3DGameMode

> 대응 Task: `mds/Tasks/09-CA3DGameMode.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] 클라 쪽 코드가 GameMode를 참조하지 않는다 (서버 전용 클래스) — grep 확인: 참조는 테스트·주석뿐
- [x] 고정 시드 모드(재현용) 존재 — `bUseFixedSeed`/`FixedSeed` (EditDefaultsOnly)
- [x] `Rules`는 `EditDefaultsOnly` — BP는 에셋 지정만
- [x] 스폰 위치 계산이 `CellToWorldFloor` 사용 + Pawn CDO 반높이 보정 (수치 하드코딩 없음)

## 자동화 테스트 (헤드리스)
- [x] `CrazyArcade3D.Framework.GameMode` 통과 (2026-07-29) — Rules 세팅·MatchStartServerTime 기록·GameMode 주도 그리드 초기화·스폰 셀 8개·8인 상이 배정·9번째 재순환. 기존 4종 테스트 회귀 없음

## 에디터 연결
- [x] `BP_CA3DGameMode`에 `DA_Rules_Default` 지정 (2026-07-29)
- [x] `L_Arena` World Settings → GameMode Override 지정 (2026-07-29)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [x] PIE 시작 → 맵 생성 + Pawn이 스폰 셀 위에 스폰 (2026-07-29 · 사용자 PIE 확인)
- [x] 고정 시드 → 매번 같은 맵 / 시드 변경 → (폴백이라 동일 맵이어도 시드 로그 변경 확인) (2026-07-29 · 사용자 PIE 확인)
- [x] (Listen+클라 1) `GameState->Rules` 복제 확인 (Checklist 08 잔여 항목 여기서 처리) (2026-07-29 · 사용자 PIE 확인)
- [x] 2인 PIE에서 서로 다른 스폰 셀 배정 (2026-07-29 · 사용자 PIE 확인)
