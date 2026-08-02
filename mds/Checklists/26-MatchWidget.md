# Checklist 26 — MatchWidget

> 대응 Task: `mds/Tasks/26-MatchWidget.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과
- [x] `CrazyArcade3DServer` 빌드 통과
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] 데이터 출처가 GameState/PlayerState/StatusComponent **읽기 전용**
      (쓰기·서버 RPC·`Server*` 호출 0줄. UI→Gameplay 는 GDD 5장 ① 의 출처가 StatusComponent 뿐이라 불가피한 예외)
- [x] WBP(BP)에 로직 없음 — 값 갱신은 전부 C++ (WBP 는 아직 미제작)
- [x] `NativeTick`에 무거운 조회 없음 — GameState·폰·StatusComponent 포인터 캐시,
      **폰이 바뀔 때만** 컴포넌트 재해석, 스냅샷 비교로 값이 바뀐 프레임에만 문자열 생성
- [x] 바인딩 필드가 명세(생존자·시간·아이템·서든데스 경고·결과)와 일치.
      ⚠️ **`BindWidget` → `BindWidgetOptional` 로 변경** — 필수 바인딩이면 이름이 전부 맞을 때까지
      WBP 자체가 컴파일되지 않아 첫 제작이 막힌다. 대신 `NativeConstruct` 가 미바인딩 이름을 한 줄로 경고한다
- [x] 자동화 테스트 `CrazyArcade3D.UI.MatchWidget` 통과 — 시간 포맷 경계·스탯 문자열(Cap 포함)·
      순위 정렬(공동 등수 묶임)·무승부 규약·종료 전 결과 비어 있음·HUDClass 배선

## 에디터 연결
- [ ] `Content/UI/WBP_Match` 생성 (UMatchWidget 서브클래스)
- [ ] `BP_CA3DHUD` 생성 후 `MatchWidgetClass` = WBP_Match 지정
      (그 전까지는 캔버스 폴백이 같은 값을 그린다 — 같은 static 순수 함수를 통과하므로 표시가 어긋나지 않는다)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] 아이템 획득 → 수치·아이콘 즉시 갱신
- [ ] 사망 발생 → 생존자 수 갱신
- [ ] 매치 타이머 진행 표시
- [ ] 서든데스 발동 → 경고 표시 / 이전엔 숨김 — **Task 24 대기**. 지금은 상태 출처가 없어 항상 Collapsed 이고
      `UMatchWidget::UpdateSuddenDeathWarning(bool)` 호출 한 줄이 유일한 연결 지점이다
- [ ] 매치 종료 → 결과 화면 순위 표시 → 로비 복귀 동선 동작 (로비는 Task 19 보류 중)
- [ ] 갇힘/사망 시 내 HUD 상태 표현 확인 (관전 전환 포함) — 현재는 폰이 사라지면 아이템 패널을 숨기고
      값 대신 `-` 를 표시한다. 별도 상태 텍스트는 만들지 않았다 (GDD 5장 HUD 3요소 밖)
