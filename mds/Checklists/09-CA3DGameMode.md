# Checklist 09 — CA3DGameMode

> 대응 Task: `mds/Tasks/09-CA3DGameMode.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 클라 쪽 코드가 GameMode를 참조하지 않는다 (서버 전용 클래스)
- [ ] 고정 시드 모드(재현용) 존재 — GDD 4.2 안전장치 2
- [ ] `Rules`는 `EditDefaultsOnly` — BP는 에셋 지정만
- [ ] 스폰 위치 계산이 `CellToWorldFloor` 사용 (수치 하드코딩 없음)

## 에디터 연결
- [ ] `BP_CA3DGameMode`에 `DA_Rules_Default` 지정
- [ ] `L_Arena` World Settings → GameMode Override 지정

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] PIE 시작 → 맵 생성 + Pawn이 스폰 셀 위에 스폰
- [ ] 고정 시드 → 매번 같은 맵 / 시드 변경 → (폴백이라 동일 맵이어도 시드 로그 변경 확인)
- [ ] (Listen+클라 1) `GameState->Rules` 복제 확인 (Checklist 08 잔여 항목 여기서 처리)
- [ ] 2인 PIE에서 서로 다른 스폰 셀 배정
