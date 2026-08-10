# AExplosionFXRelay

> `Source/CrazyArcade3D/Gameplay/Bomb/ExplosionFXRelay.h/.cpp` · AInfo, `bAlwaysRelevant`

물줄기 FX Multicast의 소유 액터. RPC 1개: `MulticastWaterCells(Cells)` (**Reliable**).

## 왜 이렇게 했는가

- **왜 릴레이가 필요한가 — RPC를 쏠 자격이 있는 액터가 없어서.**
  UHT는 RPC를 액터에만 허용한다. 후보를 하나씩 탈락시키면:
  - `UExplosionSubsystem` — 액터가 아님
  - `ABomb` — 폭발 직후 `Destroy()`라 RPC 전송 미보장
  - `AVoxelWorld` — 폴더 의존 규칙(Voxel은 FX·게임 규칙을 모른다)
  남는 답이 "전용 AInfo 하나". `AInfo`는 트랜스폼·렌더가 없는 최소 액터다.
- **왜 Reliable인가** — 물줄기 표시가 빠지면 플레이어의 규칙 이해가 어긋난다
  ("저기 물줄기가 없었는데 죽었다"). 같은 구조의 `ACA3DCueRelay`(소리)가 Unreliable인 것과
  대비되는 결정 — 신뢰성 등급도 "빠졌을 때 무엇이 깨지는가"로 정한다.
- **수신부에서 하는 일** — 데디 가드 → 폭발 큐 1회(셀 무게중심) → 풀에서 `AWaterSegment`
  획득 + `StartLinger`. 판정은 없다 — 갇힘 판정은 서버가 이미 끝냈고 이건 표시만.

## 연결
- 송신자: `ApplyExplosionCells`([16-ExplosionSubsystem.md](16-ExplosionSubsystem.md)) ·
  표시물: [20-WaterSegment.md](20-WaterSegment.md) · 같은 패턴: [31-CA3DFeedback.md](../31-CA3DFeedback.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
