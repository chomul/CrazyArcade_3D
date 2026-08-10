# UMatchWidget

> `Source/CrazyArcade3D/UI/MatchWidget.h/.cpp` · UUserWidget (Abstract, 레이아웃은 WBP)

HUD 3요소(생존/시간 · 내 스탯 · 서든데스 경고) + 결과 화면.
표시 가공·순위 정렬·무승부 판정은 전부 **static 순수 함수**.

## 왜 이렇게 했는가

- **왜 폴링인가 (OnRep 구독 대신)** — UI가 GameState의 OnRep을 잡으면 Framework→UI
  **역방향 의존**이 생긴다(폴더 의존 규칙 위반: UI는 Framework를 읽기만).
  대신 `NativeTick`이 읽고, 스냅샷 비교로 변한 것만 갱신한다.
- **`FMatchStatSnapshot` 비교로 재작업 차단** — 값이 같으면 문자열을 다시 만들지 않는다.
  float 비교는 `IsNearlyEqual(0.005)` — 복제 오차로 매 틱 "변했다" 판정이 나는 것 방지.
  StatusComponent 포인터는 폰이 바뀔 때만 재해석.
- **표시 가공이 static 순수 함수인 이유** — 위젯 인스턴스(뷰포트·월드) 없이 테스트된다.
  `BuildResultRows`(정렬·공동 등수)·`FormatResultRow`·`FormatLocalHeadline`(무승부/우승/내 순위)
  같은 "틀리기 쉬운 로직"이 자동화로 고정된다. HUD 캔버스 폴백도 같은 함수를 공유.
- **결과 화면을 매 틱 재확인하는 이유** — `bMatchEnded`(GameState)와 `FinalRank`(PlayerState)는
  **다른 액터라 복제 도착 순서 보장이 없다.** 한 번만 그리면 "끝났는데 순위가 0"인 프레임을
  굳힌다(실제 무승부 오표시 버그). 내부에서 본문 비교로 재작업은 전이 프레임에만.
- **`BindWidgetOptional`인 이유 (필수 아님)** — `BindWidget`(필수)이면 이름이 다 맞을
  때까지 WBP가 컴파일조차 안 돼 첫 제작이 막힌다. 대신 `NativeConstruct`가 미바인딩
  이름을 경고 한 줄로 알린다 — "경고 0건 = 전부 연결"이 검증 방법이 된다.
- **무승부 판정은 `MatchWinner == nullptr`** — 행(순위 목록)을 스캔해 1등이 있나 보는 게
  아니라 복제된 승자 포인터 하나로. 스캔 방식은 복제 타이밍에 취약하다.
- **로컬 대조는 이름이 아니라 포인터** — 봇 기본 이름이 중복될 수 있다.
- **UI→Gameplay(StatusComponent) 읽기 예외** — 내 스탯의 출처가 거기뿐이라 불가피.
  읽기만 하며 주석에 명시 — 예외는 숨기지 않고 문서화한다.

## 연결
- 읽기: [33-CA3DGameState.md](../Framework/33-CA3DGameState.md) · [34-CA3DPlayerState.md](../Framework/34-CA3DPlayerState.md) ·
  [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)(예외) · 수명: [38-CA3DHUD.md](38-CA3DHUD.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
