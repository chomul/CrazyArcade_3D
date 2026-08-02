# Checklist 25 — CA3DHUD

> 대응 Task: `mds/Tasks/25-CA3DHUD.md`
> **PIE·데디 실행으로 확인하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과
- [x] `CrazyArcade3DServer` 빌드 통과
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] 게임 상태 변경·서버 RPC 호출 없음 (읽기 전용 — UI 의존 규칙)
- [x] `MatchWidgetClass`는 BP 지정 프로퍼티 (`EditDefaultsOnly`. 미지정이 정상 경로 = 캔버스 폴백)
- [x] 범위 제외 항목(화면 밖 위협 인디케이터·갇힌 플레이어 표시) 미구현 확인

## 에디터 연결
- [x] `HUDClass` 지정 — **C++ 기본값**(`ACA3DGameMode` 생성자)으로 배선.
      BP_CA3DGameMode 는 이 값을 오버라이드하지 않으므로 에디터 작업 없이 상속받는다
- [ ] (선택) `BP_CA3DHUD` 생성 후 `MatchWidgetClass` = WBP_Match 지정 — WBP 제작 후

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [x] **캔버스 폴백이 실제로 그려진다** (2026-08-03) — 클라 실행 스크린샷으로 확인:
      `생존 1  0:00` / `폭탄 0/1  범위 1  속도 x1.00  니들 X  킥 X`, 한글 글리프 정상.
      로그 `ACA3DHUD: MatchWidgetClass 미지정 — 캔버스 텍스트 폴백으로 표시`
- [ ] PIE: 매치 시작 시 **위젯** 표시 (WBP_Match 제작 후)
- [ ] 매치 종료 → 결과 화면 전환
- [x] **`CrazyArcade3DServer` 실제 실행 로그에 HUD/위젯 생성 흔적 없음** (2026-08-03) —
      쿡·스테이징 서버 + 봇 4인 75초 실행에서 `HUD`/`MatchWidget`/`UserWidget`/`LogSlate`/`LogUMG`
      매치 0건, Error·Warning 0건
