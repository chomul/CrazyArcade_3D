# ACA3DHUD

> `Source/CrazyArcade3D/UI/CA3DHUD.h/.cpp` · AHUD (클라 전용)

매치 위젯의 수명 관리 + 캔버스 텍스트 폴백. 시각 전용 — 게임 상태 무접촉.

## 역할

- **매치 위젯의 수명 관리**: 생성 → 뷰포트 부착 → 결과 화면 전환 위임(`ShowResult`).
- WBP가 없을 때 값을 캔버스 텍스트로 직접 그리는 **폴백**(개발용, non-Shipping).
- 표시 내용의 가공은 하지 않는다 — 전부 `UMatchWidget`의 순수 함수 소관.

## 왜 이렇게 했는가

- **HUD의 일은 "위젯을 만들고 보여주는 것"뿐** — 표시 내용·가공은 전부 `UMatchWidget`.
  수명(생성·뷰포트 부착·결과 전환)과 내용을 분리해야 WBP 교체가 자유롭다.
- **캔버스 폴백이 있는 이유** — WBP(에디터 작업)를 만들기 전에도 값이 보여야 개발이
  막히지 않는다. 실제로 **에디터 작업 0으로 스크린샷 검증**이 됐다. 폴백은
  `#if !UE_BUILD_SHIPPING` 안에만 존재 — 출시 빌드에 디버그 표시가 남지 않는다.
- **폴백이 위젯의 static 순수 함수를 그대로 통과한다** — 폴백 전용 표시 로직을 만들면
  두 벌이 된다. `FormatElapsedTime`·`FormatStatLine` 등을 공유하므로 폴백과 위젯이
  항상 같은 값을 보여준다.
- **cvar `ca3d.DebugHUD` 3모드(-1 자동/0 끄기/1 강제)** — 자동(위젯 없을 때만)이 기본,
  강제는 위젯과 폴백을 비교 검증할 때.
- **데디 이중 방어** — 엔진(`AGameModeBase`)이 데디에서 HUD를 스폰하지 않지만,
  `BeginPlay` 최상단에 `IsRunningDedicatedServer()` 가드를 한 겹 더(GDD 7.4).

## 연결
- 내용물: [39-MatchWidget.md](39-MatchWidget.md) · 배선: `ACA3DGameMode` 생성자 `HUDClass`

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
