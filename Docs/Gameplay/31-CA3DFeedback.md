# CA3DFeedback (+ ACA3DCueRelay · UCA3DFeedbackSubsystem)

> `Source/CrazyArcade3D/Gameplay/CA3DFeedback.h/.cpp` · namespace + AInfo + UWorldSubsystem

사운드·이펙트 재생 경로. `ECA3DCue` 10종(BombPlace·Explosion·BlockBreak·ItemPickup·
Trapped·Escape·Death·Kick·SuddenDeathWarn·MatchEnd), 재생은 `Play` **단일 경로**.

## 역할

- **`CA3DFeedback::Play`**: 큐(사건) → 룰셋 에셋(사운드+FX) 해석 → 재생의 단일 경로.
  데디 가드·미지정 no-op·재생까지 전부 여기서.
- **`ServerBroadcast`**: 서버에서 발생한 큐를 전 클라에 방송하는 진입점.
- **`ACA3DCueRelay`**: 그 방송의 Multicast 스피커(Unreliable).
- **`UCA3DFeedbackSubsystem`**: `OnGridChanged` 구독 → 블록 파괴음 — Voxel에 소리를
  넣지 않기 위한 구독자.

## 왜 이렇게 했는가

- **재생 단위가 에셋이 아니라 "사건"** — 큐 하나가 사운드+나이아가라를 함께 든다.
  한 호출로 둘 다 — 둘이 따로 놀면 소리만 나고 이펙트가 없는 반쪽 사건이 생긴다.
- **데디 가드가 `Play` 한 곳에만 있다** — 호출부마다 흩어 놓으면 언젠가 하나가 빠지고,
  그 버그는 데디 exe로만 잡힌다. 가드를 한 곳에 모으면 빠질 곳이 없다.
- **에셋 미지정은 조용히 no-op + 큐당 Verbose 1회** — 에셋이 아직 없는 개발 단계에서
  로그 도배 금지. "왜 소리가 안 나지?"는 Verbose 로그 한 줄로 확인 가능.
- **동시 재생 상한을 C++로 세지 않는다** — Sound Concurrency **에셋**의 일(GDD 7.4).
  C++로 또 세면 상한 규칙이 두 벌이 된다.
- **`ACA3DCueRelay`(Unreliable Multicast)가 필요한 이유** — 클라 도달 경로가 없는 큐
  (ItemPickup·Kick·MatchEnd) 전용:
  - `AItemPickup`·`ABomb`은 사건 직후 `Destroy()` — 자기 RPC 전송 미보장
  - `ABomb::bKicking`은 비복제 — 클라가 스스로 판정하면 보간 지연으로 오탐
  - GameMode는 클라에 아예 없다
  **Unreliable**인 이유: 순수 시각이라 한 발 빠져도 판정 무영향
  (물줄기 `AExplosionFXRelay`가 Reliable인 것과 대비 — 신뢰성도 "빠지면 뭐가 깨지나"로 결정).
- **설치음 중복 방지** — 설치자는 예측 시점(즉시), 남·리슨 호스트는 서버 확정 시점.
  `ReleasePredictedVisualAt`의 bool("방금 내 예측을 회수했는가") 하나로 가른다.
  소리만 RTT만큼 늦으면 예측이 지연을 감추는 목적 자체가 무너진다.
- **`UCA3DFeedbackSubsystem`이 `OnGridChanged`를 구독** — 파괴음을 Voxel에 넣지 않기 위한
  장치. 델리게이트 인자는 "실제로 비워진 셀"뿐 — Voxel은 여전히 게임 규칙을 모른다.
- **`SuddenDeathImpact` 큐를 일부러 안 만들었다** — 서든데스 낙하가 폭탄과 같은 함수
  (`ServerApplyExplosionAt`)를 지나므로, 별도 큐를 만들면 한 발에 두 소리가 겹치거나
  안 겹치게 하려고 적용 경로를 갈라야 한다.

## 연결
- 릴레이 패턴 짝: [19-ExplosionFXRelay.md](Bomb/19-ExplosionFXRelay.md) · 파괴음 출처: [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) ·
  에셋 슬롯: [22-CA3DRuleSet.md](../Framework/22-CA3DRuleSet.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
