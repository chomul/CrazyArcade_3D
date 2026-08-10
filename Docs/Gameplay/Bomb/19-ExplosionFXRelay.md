# AExplosionFXRelay

> `Gameplay/Bomb/ExplosionFXRelay.h/.cpp` · AInfo · bAlwaysRelevant

## 역할
- 물줄기 셀 목록을 전 클라에 방송(`MulticastWaterCells`, **Reliable**)
- 수신 시: 풀에서 물줄기 획득 + 폭발 큐 1회. 판정 없음

## 왜
- **왜 릴레이가 필요?** → RPC는 액터만 가능. 후보 소거:
  서브시스템(액터 아님) · ABomb(직후 Destroy) · VoxelWorld(의존 규칙) → 전용 AInfo
- **왜 Reliable?** → 물줄기 누락 = 규칙 이해 붕괴("물줄기 없었는데 죽었다").
  소리 릴레이(Unreliable)와 대비 — 신뢰성도 "빠지면 뭐가 깨지나"로 결정

## 연결
송신: [16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · 표시: [20-WaterSegment.md](20-WaterSegment.md) · 같은 패턴: [31-CA3DFeedback.md](../31-CA3DFeedback.md)

## Q&A
아직 없음
