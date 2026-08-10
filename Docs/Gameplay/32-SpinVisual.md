# CA3DSpin (SpinVisual.h)

> `Source/CrazyArcade3D/Gameplay/SpinVisual.h` · namespace 헬퍼

맵 위 물건(폭탄·예측 폭탄·아이템)의 제자리 회전. `ResolveDegreesPerSecond(World)` +
`ApplyYaw(Component, Delta, Speed)`.

## 역할

- 시각 컴포넌트를 **일정 속도로 제자리 회전**시킨다 — 속도는 룰셋에서 해석
  (`ResolveDegreesPerSecond`), 적용은 `ApplyYaw` 한 줄.
- 폭탄·예측 폭탄·아이템 셋이 공유하는 회전 규칙의 단일 출처.

## 왜 이렇게 했는가

- **세 클래스가 회전 속도 출처를 공유하는 이유** — `ABomb`·`APredictedBombVisual`·
  `AItemPickup`이 같은 룰셋 값(`PickupSpinDegreesPerSecond`)을 쓴다. 속도가 갈리면
  서버 확정 순간 예측→진짜 교체가 **각도 점프**로 보인다. 눈에 띄는 어긋남을 막으려고
  출처를 하나로 묶었다.
- **판정 컴포넌트는 돌지 않는다** — 회전은 메시(시각)에만. 컬리전 박스가 같이 돌면
  판정이 프레임마다 미세하게 달라진다.
- **데디·속도0·컴포넌트 없음은 전부 no-op** — 호출부가 조건 검사를 안 해도 되게
  헬퍼가 안전 조건을 다 삼킨다.
- **왜 컴포넌트가 아니라 namespace 함수인가** — 상태가 없다(속도는 룰셋, 각도는 대상
  컴포넌트에 누적). 상태 없는 로직에 UObject 비용을 치를 이유가 없다.

## 연결
- 사용자: [17-Bomb.md](Bomb/17-Bomb.md) · [18-PredictedBombVisual.md](Bomb/18-PredictedBombVisual.md) · [29-ItemPickup.md](Item/29-ItemPickup.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
