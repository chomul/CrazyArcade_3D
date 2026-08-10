# CA3DFeedback (+ CueRelay · FeedbackSubsystem)

> `Gameplay/CA3DFeedback.h/.cpp` · namespace + AInfo + UWorldSubsystem — 큐 10종

## 역할
- `Play`: 큐(사건) → 룰셋 에셋(사운드+FX) → 재생의 단일 경로
- `ServerBroadcast` + `ACA3DCueRelay`: 서버발 큐를 전 클라에 방송(Unreliable)
- `UCA3DFeedbackSubsystem`: `OnGridChanged` 구독 → 블록 파괴음

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

## 연결
[19-ExplosionFXRelay.md](Bomb/19-ExplosionFXRelay.md) · [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) · [22-CA3DRuleSet.md](../Framework/22-CA3DRuleSet.md)

## Q&A
아직 없음
