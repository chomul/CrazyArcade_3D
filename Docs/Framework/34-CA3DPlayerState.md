# ACA3DPlayerState

> `Source/CrazyArcade3D/Framework/CA3DPlayerState.h/.cpp` · APlayerState

참가자 1명의 복제 데이터. **로직 0** — `ColorIndex` · `FinalRank` · `bAlive` ·
`CamYawIndex` · `bLeftMatch` 5필드.

## 왜 이렇게 했는가

- **왜 폰이 아니라 PlayerState인가** — 폰은 죽으면 숨겨지고 언젠가 파괴될 수 있지만,
  순위·색·생존 여부는 **매치가 끝날 때까지** 남아야 한다(결과 화면이 읽는다).
  PlayerState는 컨트롤러 수명을 따라가는 엔진 표준 자리다.
- **`OnDeactivated`를 오버라이드해 Super를 안 부른다** — 엔진 기본은 접속 종료 시
  PlayerState 파괴. 파괴되면 결과 화면에서 탈주자가 사라진다. 남겨서 `bLeftMatch`로 표시.
- **봇도 이 클래스를 갖는다(`bWantsPlayerState`)** — 승패 판정(`ResolvePendingDeaths`)이
  PlayerArray만 보므로 사람과 봇을 구분할 필요가 없어진다.
- **`FinalRank` 쓰기가 `ResolvePendingDeaths` 한 곳뿐** — 순위는 서버 승패 판정의 출력이다.
  다른 곳에서 쓰면 공동 등수 공식이 무의미해진다. 0 = 아직 생존(미확정)을 겸한다.
- **`CamYawIndex`(uint8 0~3)가 여기 있는 이유** — 카메라 yaw는 컨트롤러 로컬 값이라
  복제되지 않는데, 관전자는 대상의 각을 알아야 한다. 폰이 아니라 PlayerState에 둔 것은
  폰 리스폰과 무관하게 유지되는 표현 값이기 때문. 4바이트도 아깝다 — 인덱스 1바이트만 복제.
- **`bAlive`가 `LifeState`와 별도인 이유** — `LifeState`(StatusComponent)는 폰에 붙어 있고
  Trapped 같은 폰 상태를 담는다. 승패·관전이 필요한 건 "판정상 살아 있나" 하나뿐이라
  폰 없이도 읽히는 자리에 최소 형태로 둔다.

## 연결
- 쓰기: [35-CA3DGameMode.md](35-CA3DGameMode.md)(판정) · [26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md)(CamYaw RPC) ·
  읽기: [39-MatchWidget.md](../UI/39-MatchWidget.md)(순위) · 관전 대상 선정([26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md))

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
