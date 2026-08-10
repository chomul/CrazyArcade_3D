# CameraYawSnap (namespace)

> `Source/CrazyArcade3D/Core/CameraYawSnap.h/.cpp` · 순수 함수 + 구조 상수

90도 스냅 카메라 각의 **단일 출처**. `NumSteps=4` · `StepDeg=90` ·
`StepsToIndex` / `IndexToYawDeg` / `YawDegToIndex` / `YawDegToIndexWithHysteresis`.

## 왜 이렇게 했는가

- **사용처가 3곳이라 단일 출처가 필수** — ① 컨트롤러 Q/E 누적 스텝 → 인덱스
  ② `ACA3DCharacter::GetViewRotation`(복제 인덱스 → 각) ③ 봇 이동 방향 스냅.
  한쪽만 바뀌면 "내가 보는 각"과 "관전자가 나를 볼 때의 각"이 어긋나는데, 이는
  **관전에 들어가야만 보이는** 버그라 발견이 매우 늦다 — 이 파일이 존재하는 이유.
- **왜 룰셋(튜닝 값)이 아니라 코드 상수(구조 상수)인가** — 스텝 수가 런타임에 바뀌면
  이미 복제된 `CamYawIndex`의 **의미**가 달라진다(인덱스 2 = 180도였다가 90도가 되는 식).
  값이 아니라 좌표계 정의라서 코드에 박는다. 실제로 45도→90도 변경(2026-08-09)이
  `NumSteps` 8→4 **하나**로 끝났다 — 파생값(스텝 크기·인덱스 범위·히스테리시스 경계)이
  전부 이 상수에서 나오기 때문.
- **`IndexToYawDeg`가 `Index % NumSteps`로 접는 이유** — 악의적 클라·버전 불일치로
  범위 밖 인덱스가 와도 안전하게 정규화.
- **음수 나머지 보정(`((s%N)+N)%N`)** — C++의 `%`는 음수에서 부호를 유지한다.
  Q를 여러 번 누른 음수 스텝도 올바른 인덱스로.
- **히스테리시스(`YawDegToIndexWithHysteresis`)** — 봇 전용. 이동 방향이 스냅 경계에
  걸치면 인덱스가 매 틱 튄다. 임계에 여유각(12도)을 더해 경계 진동을 막는다.
- **`FRotator::ClampAxis`를 안 쓰는 이유** — UE5 FRotator는 double 기반이라
  float↔double 왕복이 생긴다. 자체 `Fmod` 정규화로 통일.

## 연결
- 사용처: [26-CA3DPlayerController.md](../Gameplay/Character/26-CA3DPlayerController.md) ·
  [23-CA3DCharacter.md](../Gameplay/Character/23-CA3DCharacter.md) · [37-BotController.md](../AI/37-BotController.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
