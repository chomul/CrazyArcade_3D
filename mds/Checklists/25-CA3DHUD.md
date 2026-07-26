# Checklist 25 — CA3DHUD

> 대응 Task: `mds/Tasks/25-CA3DHUD.md`
> **PIE·데디 실행으로 확인하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 게임 상태 변경·서버 RPC 호출 없음 (읽기 전용 — UI 의존 규칙)
- [ ] `MatchWidgetClass`는 BP 지정 프로퍼티
- [ ] 범위 제외 항목(화면 밖 위협 인디케이터 등) 미구현 확인

## 에디터 연결
- [ ] `BP_CA3DGameMode`의 `HUDClass` 지정

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [ ] PIE: 매치 시작 시 위젯 표시
- [ ] 매치 종료 → 결과 화면 전환
- [ ] **`CrazyArcade3DServer` 실제 실행 로그에 HUD/위젯 생성 흔적 없음** (PIE만으로는 이 항목 미검증)
