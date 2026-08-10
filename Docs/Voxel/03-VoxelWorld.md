# AVoxelWorld

> `Source/CrazyArcade3D/Voxel/VoxelWorld.h/.cpp` · AActor (`bReplicates`, `bAlwaysRelevant`)

레벨에 1개 배치되는 지형 액터. `FVoxelGrid`를 소유하고 **권한·복제·파괴 단일 경로·좌표 변환**을 맡는다.

## 역할

- `FVoxelGrid`를 **소유**하고 엔진과 잇는다: 시드·크기·파괴 이력 **복제**, 파괴 **단일 경로 적용**(`ApplyDestruction`).
- 서버: 시드로 그리드 생성(`ServerInitFromSeed`) · 파괴 확정(`ServerDestroyBlocks`).
- 클라: OnRep으로 같은 그리드 재생성 + 늦게 온 파괴 따라잡기.
- 월드↔셀 좌표 변환(`WorldToCell / CellToWorld`), 스폰 셀·아이템 배치 데이터 보관, 변경 알림(`OnGridChanged`).

## 왜 이렇게 했는가

- **왜 그리드를 액터가 소유하나** — 리플리케이션은 액터 단위로만 가능하다.
  순수 데이터(`FVoxelGrid`)와 엔진 결합(복제·권한)을 나누되, 엔진 쪽 절반이 이 클래스다.
- **왜 지형을 데이터가 아니라 시드로 보내나** — 2,646셀을 복제하는 대신 `Seed`(uint32) 하나를
  복제하고 클라가 **같은 순수 함수**로 재생성한다. 대역폭 문제가 아니라 정합성 문제다:
  생성기가 결정론이면(불변식 4) "서버와 다른 지형"이 원천적으로 불가능하다.
- **왜 `GridSize`도 따로 복제하나** — 크기는 인원수 티어로 서버가 정한다. 클라가 로컬 인원수로
  유추하면 중간 접속자가 다른 크기의 맵을 만든다. 크기는 서버 결정·복제만.
- **`ApplyDestruction` 단일 경로 (불변식 1)** — 서버는 직접 호출 + `MulticastOnBlocksDestroyed`,
  클라는 Multicast 수신 후 **같은 함수** 호출. 경로가 두 벌이면 반드시 어긋난다.
  1주차 게이트에서 해시 30건 일치로 실증.
- **왜 `DestroyedCells` 복제 배열(파괴 이력)이 있나** — Multicast는 그 순간 접속한 클라에게만
  간다. 늦게 들어온 클라는 `OnRep_DestroyedCells`에서 `FilterUnapplied`로 미적용분만 따라잡는다.
  "새 상태를 Multicast로만 알리면 중간 접속자에게 영원히 안 간다"의 해법.
- **왜 선도착 파괴 큐(`PendingDestroyQueue`)가 있나** — 복제 순서상 파괴 Multicast가
  `OnRep_Seed`보다 **먼저** 도착할 수 있다. 그리드 초기화 전 파괴는 큐에 쌓고
  `InitGridFromSeed` 말미에 flush.
- **왜 `ApplyDestruction`이 "요청 목록"이 아니라 "실제로 바뀐 칸"을 알리나** — 같은 셀이 두 번
  들어오는 것이 정상 경로다(중간 접속 따라잡기·연쇄 범위 겹침·서든데스와 폭탄이 같은 칸).
  요청 목록을 그대로 방송하면 아무것도 안 부서졌는데 "부서졌다"가 나가 구독자(파괴음)가 헛돈다.
- **왜 `OnGridChanged` 델리게이트인가** — 파괴음 같은 게임 반응을 Voxel에 넣으면 의존 규칙이
  무너진다. Voxel은 "이 셀들이 비워졌다"만 알리고, Gameplay(`UCA3DFeedbackSubsystem`)가 구독한다.
- **아이템 배치(`ItemPlacements`)는 데이터만** — 지형은 아이템 액터를 모른다.
  `ConsumeItemPlacement`가 데이터를 꺼내 주면 스폰은 `ExplosionSubsystem`(Gameplay)이 한다.

## 네트워크 표면
- 복제: `Seed`·`GridSize`·`DestroyedCells` (전부 OnRep)
- RPC: `MulticastOnBlocksDestroyed(Cells)` 하나. Server RPC 없음 — 서버 전용 함수는 `HasAuthority()` 가드.

## 연결
- 소유 데이터: [02-VoxelGrid.md](02-VoxelGrid.md) · 렌더: [04-VoxelRenderer.md](04-VoxelRenderer.md) ·
  호출자: `ApplyExplosionCells`([16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md))

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
