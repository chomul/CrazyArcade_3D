# ACA3DCharacter

> `Source/CrazyArcade3D/Gameplay/Character/CA3DCharacter.h/.cpp` · ACharacter

사람·봇 **공용** 캐릭터. 행동의 진입점: 이동(`Move`/`DoJump`) · 폭탄 설치
(`TryPlaceBombPredicted`) · 니들(`TryUseNeedle`) · 킥(`ServerTryKickBomb`) ·
사망 적용(`ApplyDeathState`) · 발밑 셀(`GetFootCell`).

## 왜 이렇게 했는가

- **왜 사람과 봇이 같은 클래스·같은 진입점인가** — 봇 전용 경로를 만들면 검증이 두 벌이
  된다(예: 봇만 죽은 채로 폭탄을 놓는 버그). 봇 컨트롤러는 `Move`/`DoJump`/
  `TryPlaceBombPredicted`만 부른다 — 사람이 못 하는 것은 봇도 못 한다.
- **`GetFootCell()`이 판정의 중심** — "발판만이 안전하다"(GDD 2.3)는 위치가 아니라
  **발밑 셀**로 판정한다. 다른 층 점프 = 셀이 바뀌어 회피, 제자리 점프 = 공중이어도
  셀 그대로라 피격. 폭발 피격·킥 대상 탐색·봇 경로가 전부 이 함수를 쓴다.
- **이동 수치는 전부 룰셋 계수 × CellSize 파생** — 이동 4칸/초, 점프 정점 1.4칸,
  스텝 0.3칸. 셀 크기가 바뀌어도 게임 감각이 유지된다. 코드에 매직 넘버 금지.
- **`ApplyDeathState`가 단일 지점 (서버 `ServerKill` ↔ 클라 `OnRep_Life` 공통)** —
  캡슐 NoCollision(유령 방해 금지) · `MOVE_None`(시체 낙하 방지 — 컬리전을 껐으므로) ·
  **액터 단위** 숨김(메시 하나만 끄면 BP가 추가한 메시가 남아 시체가 보인다 — 실제 사고).
  컬리전·이동 모드는 복제되지 않는 값이라 서버 가드로 감싸면 죽은 본인 화면이 깨진다 —
  그래서 양쪽이 같은 함수를 각자 실행한다.
  폰은 파괴·풀링하지 않는다 — 부활은 이 함수가 바꾼 것을 되돌리면 된다.
- **`Move`는 Dead만 차단, `DoJump`는 Alive만 통과** — 갇힘 중 미세 이동은 허용하되
  점프 탈출은 금지(사용자 확정). 조건이 서로 다른 게 의도다. 가드를 컨트롤러가 아니라
  캐릭터에 둔 이유: 봇도 같은 경로를 지나게.
- **`GetViewRotation()` 오버라이드** — 카메라 yaw는 컨트롤러 로컬 값이라 복제되지 않는다.
  이 폰을 보는 로컬 컨트롤러가 있으면 그 각(내 폰·관전 대상 공통 가지), 없으면(원격·봇)
  복제된 `CamYawIndex` → 스냅 각. 관전 카메라가 눕지 않는 이유.
- **`GetContactReach` 공용 접촉 공식** — 킥 거리 판정과 갇힌 상대 터뜨리기가 한 벌.
  두 벌이면 "킥은 되는데 터뜨리기는 안 되는 거리"가 생긴다.
- **공중 설치 = -Z 스캔으로 내려 찾은 셀 (잠정 규칙)** — 발밑이 비면 아래로 첫 솔리드
  바로 위 Empty 셀에 설치. 그리드 밖이면 RPC 자체를 안 보낸다.

## 네트워크 표면
RPC 3개: `ServerPlaceBomb` · `ClientRejectBomb` · `ServerUseNeedle`.
자체 복제 없음 — 상태는 `UStatusComponent`, 이동은 CMC 기본 복제.

## 연결
- 상태: [24-StatusComponent.md](24-StatusComponent.md) · 예측: [18-PredictedBombVisual.md](../Bomb/18-PredictedBombVisual.md) ·
  입력: [26-CA3DPlayerController.md](26-CA3DPlayerController.md) · 봇: [37-BotController.md](../../AI/37-BotController.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
