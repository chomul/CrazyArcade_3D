# AExplosionFXRelay

> `Gameplay/Bomb/ExplosionFXRelay.h/.cpp` · AInfo · bAlwaysRelevant

## 역할
- 물줄기 셀 목록을 전 클라에 방송(`MulticastWaterCells`, **Reliable**)
- 수신 시: 풀에서 물줄기 획득 + 폭발 큐 1회. 판정 없음

## 주요 함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `MulticastWaterCells(Cells)` (Reliable) | 수신: 데디 가드 → 폭발 큐 1회 → 셀마다 풀에서 WaterSegment + StartLinger | **Reliable** — 물줄기 누락은 규칙 이해를 깨뜨림. 셀 목록 하나만 보내고 액터는 각 클라가 로컬 스폰(셀당 액터 복제보다 훨씬 쌈) |

## 멀티 처리
서버 → 전 클라 **단방향 방송 전용.** 판정은 이미 서버가 끝냈고, 이 액터는 "어디에 물줄기를
그릴지"만 나른다. `bAlwaysRelevant` — 거리 컬링으로 방송이 빠지면 안 됨

## 왜
- **왜 릴레이가 필요?** → RPC는 액터만 가능. 후보 소거:
  서브시스템(액터 아님) · ABomb(직후 Destroy) · VoxelWorld(의존 규칙) → 전용 AInfo
- **왜 Reliable?** → 물줄기 누락 = 규칙 이해 붕괴("물줄기 없었는데 죽었다").
  소리 릴레이(Unreliable)와 대비 — 신뢰성도 "빠지면 뭐가 깨지나"로 결정

## 연결
송신: [16-ExplosionSubsystem.md](16-ExplosionSubsystem.md) · 표시: [20-WaterSegment.md](20-WaterSegment.md) · 같은 패턴: [31-CA3DFeedback.md](../31-CA3DFeedback.md)

## Q&A
아직 없음
