# Checklist 26 — MatchWidget

> 대응 Task: `mds/Tasks/26-MatchWidget.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 데이터 출처가 GameState/PlayerState/StatusComponent **읽기 전용**
- [ ] WBP(BP)에 로직 없음 — 레이아웃·애니메이션만
- [ ] `NativeTick`에 무거운 조회 없음 (타이머 표기 수준)
- [ ] `BindWidget` 필드가 명세(생존자·시간·아이템·서든데스 경고·결과)와 일치

## 에디터 연결
- [ ] `Content/UI/WBP_Match` 생성 (UMatchWidget 서브클래스), BindWidget 이름 일치로 컴파일 통과
- [ ] `BP_CA3DHUD`(또는 HUD 기본값)에 WBP_Match 지정

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 아이템 획득 → 수치·아이콘 즉시 갱신
- [ ] 사망 발생 → 생존자 수 갱신
- [ ] 매치 타이머 진행 표시
- [ ] 서든데스 발동 → 경고 표시 / 이전엔 숨김
- [ ] 매치 종료 → 결과 화면 순위 표시 → 로비 복귀 동선 동작
- [ ] 갇힘/사망 시 내 HUD 상태 표현 확인 (관전 전환 포함)
