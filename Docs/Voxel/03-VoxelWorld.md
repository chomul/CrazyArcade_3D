# AVoxelWorld

> `Voxel/VoxelWorld.h/.cpp` · AActor (bReplicates · bAlwaysRelevant) — 레벨에 1개

## 역할
- `FVoxelGrid` 소유 + 엔진 결합: 복제(Seed·GridSize·DestroyedCells) · 파괴 단일 경로
- 서버: `ServerInitFromSeed` · `ServerDestroyBlocks` / 클라: OnRep으로 재생성·따라잡기
- 좌표 변환(`WorldToCell`/`CellToWorld`) · 스폰 셀·아이템 배치 보관 · `OnGridChanged` 알림

## 왜
- **왜 지형을 시드로 보내나?** → 데이터 복제 대신 Seed 하나 + 클라가 같은 순수 함수로
  재생성. 생성기가 결정론이면 "서버와 다른 지형"이 원천 불가
- **왜 GridSize 별도 복제?** → 크기는 인원 티어로 서버가 결정. 클라가 유추하면
  중간 접속자가 다른 맵을 만듦
- **왜 ApplyDestruction 단일 경로?(불변식 1)** → 서버 직접 호출 + Multicast 수신이
  같은 함수. 경로 두 벌 = 반드시 어긋남. 해시 30건 일치로 실증
- **왜 DestroyedCells 이력?** → Multicast는 그 순간 접속자에게만 감.
  늦은 접속자는 OnRep에서 미적용분만 따라잡음
- **왜 선도착 큐?** → 파괴 Multicast가 OnRep_Seed보다 먼저 올 수 있음.
  그리드 초기화 전 파괴는 큐 → 초기화 직후 flush
- **왜 "실제로 바뀐 칸"만 알림?** → 같은 셀 중복 요청이 정상 경로(따라잡기·연쇄 겹침).
  요청 목록 그대로 방송하면 구독자(파괴음)가 헛돎
- **왜 OnGridChanged 델리게이트?** → 파괴음을 Voxel에 넣지 않기 위해.
  Voxel은 "비워진 셀"만 알리고 Gameplay가 구독
- **왜 아이템은 데이터만?** → 지형은 아이템 액터를 모름. 스폰은 Gameplay 소관

## 네트워크
복제 3(전부 OnRep) · RPC 1(`MulticastOnBlocksDestroyed`). Server RPC 없음 — `HasAuthority()` 가드

## 연결
[02-VoxelGrid.md](02-VoxelGrid.md) · [04-VoxelRenderer.md](04-VoxelRenderer.md) · 호출자: [16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md)

## Q&A
아직 없음
