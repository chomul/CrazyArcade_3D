# CA3DFeedback (+ CueRelay · FeedbackSubsystem)

> `Gameplay/CA3DFeedback.h/.cpp` · namespace + AInfo + UWorldSubsystem — 큐 10종

## 역할
- `Play`: 큐(사건) → 룰셋 에셋(사운드+FX) → 재생의 단일 경로
- `ServerBroadcast` + `ACA3DCueRelay`: 서버발 큐를 전 클라에 방송(Unreliable)
- `UCA3DFeedbackSubsystem`: `OnGridChanged` 구독 → 블록 파괴음

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `ECA3DCue` 10종 | BombPlace·Explosion·BlockBreak·ItemPickup·Trapped·Escape·Death·Kick·SuddenDeathWarn·MatchEnd | |
| `Play(World, Cue, Location)` | 단일 재생 경로 | **재생은 항상 로컬** — 소리·FX는 전송 대상이 아니라 각 클라가 자기 사건 인지 시점에 냄. 데디 가드가 여기 한 곳 |
| `ResolveCueAssets(Rules, Cue, ...)` | 큐 → 룰셋 필드 매핑 | |
| `ServerBroadcast(World, Cue, Location)` | 서버발 큐 방송 (클라면 no-op) | **클라가 자연히 알 수 없는 사건만** 방송(아이템·킥·매치 종료) — 복제·Multicast로 이미 아는 사건(폭발·사망)은 각자 냄 |
| `CueRelay::MulticastCue` (Unreliable) | 수신 → Play 한 줄 | **Unreliable** — 순수 시각·청각. 한 발 빠져도 판정 무영향 (물줄기 Reliable과 대비) |
| `FeedbackSubsystem::OnGridChanged` | 파괴음 1회 | 로컬 구독 — 파괴는 이미 Multicast로 오므로 소리를 또 보낼 필요 없음. 각 머신의 ApplyDestruction이 방아쇠 |

## 왜
- **왜 재생 단위가 "사건"?** → 큐 하나 = 사운드+FX 쌍. 따로 놀면 반쪽 사건
- **왜 데디 가드가 Play 한 곳뿐?** → 호출부에 흩어지면 언젠가 하나 빠짐 —
  그 버그는 데디 exe로만 잡힘
- **왜 미지정 no-op + Verbose 1회?** → 에셋 없는 개발 단계 로그 도배 금지
- **왜 동시 재생 상한을 C++로 안 세나?** → Sound Concurrency 에셋의 일. 두 벌 금지
- **왜 CueRelay?** → 클라 도달 경로 없는 큐(아이템·킥·매치 종료) 전용:
  액터는 직후 Destroy(전송 미보장) · bKicking 비복제 · GameMode는 클라에 없음
- **왜 Unreliable?** → 순수 시각 — 한 발 빠져도 판정 무영향 (물줄기 Reliable과 대비)
- **설치음 중복 방지** → 설치자는 예측 시점, 남·호스트는 확정 시점.
  `ReleasePredictedVisualAt`의 bool 하나로 가름
- **왜 파괴음이 구독?** → Voxel에 소리를 넣지 않기 위해. Voxel은 여전히 게임을 모름
- **왜 SuddenDeathImpact 큐 없음?** → 낙하가 폭탄과 같은 함수 — 별도 큐면 소리 겹침

## 멀티 처리
**소리·이펙트는 전송하지 않고, "사건을 알게 되는 것"만 전송한다.** 파괴·사망처럼 복제로
이미 아는 사건은 각 클라가 로컬로 재생하고, 자연 도달 경로가 없는 사건 3종만
Unreliable Multicast로 알린다. 설치음만 예외적으로 두 시점(예측/확정)을 가르는 장치가 있다.

## 연결
[19-ExplosionFXRelay.md](Bomb/19-ExplosionFXRelay.md) · [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) · [22-CA3DRuleSet.md](../Framework/22-CA3DRuleSet.md)

## Q&A
아직 없음
