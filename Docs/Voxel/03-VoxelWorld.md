# AVoxelWorld

> `Voxel/VoxelWorld.h/.cpp` · AActor (bReplicates · bAlwaysRelevant) — 레벨에 1개

## 역할
- `FVoxelGrid` 소유 + 엔진 결합: 복제(Seed·GridSize·DestroyedCells) · 파괴 단일 경로
- 서버: `ServerInitFromSeed` · `ServerDestroyBlocks` / 클라: OnRep으로 재생성·따라잡기
- 좌표 변환(`WorldToCell`/`CellToWorld`) · 스폰 셀·아이템 배치 보관 · `OnGridChanged` 알림

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `Grid` (FVoxelGrid) | 소유한 지형 데이터 (비복제 — 시드로 재생성) |
| `Seed` / `GridSize` (복제·OnRep) | 클라 재생성의 입력 두 개 |
| `DestroyedCells` (복제 배열) | 파괴 이력 — 중간 접속 따라잡기용 |
| `SpawnCells` / `ItemPlacements` | 생성기가 낸 스폰 셀·아이템 배치 (서버 로컬) |
| `PendingDestroyQueue` | 그리드 초기화 전 도착한 파괴 대기열 |
| `CellSize` (=100) | 셀 한 변 cm — 좌표 변환의 기준 |
| `OnGridChanged` (델리게이트) | "이 셀들이 비워졌다" 알림 (파괴음 등이 구독) |
| `ServerInitFromSeed(Seed, Size)` | 서버: 시드 확정 + 그리드 생성 시작 |
| `ServerDestroyBlocks(Cells)` | 서버: 파괴 확정 → 적용 + 이력 + Multicast |
| `ApplyDestruction(Cells)` | **단일 경로** — 그리드 갱신→렌더 제거→델리게이트 |
| `MulticastOnBlocksDestroyed` | 클라 수신 → 같은 ApplyDestruction |
| `OnRep_Seed / OnRep_GridSize` | → `InitGridFromSeed()` (재진입 가드) |
| `OnRep_DestroyedCells` | 이력 따라잡기 (미적용분만) |
| `FilterUnapplied(Cells)` | 이미 Empty인 셀 제외 — 중복 적용 방지 |
| `WorldToCell / CellToWorld / CellToWorldFloor` | 월드 좌표 ↔ 셀 변환 |
| `ConsumeItemPlacement(Cell, OutType)` | 배치 예약 1회 소비 (재파괴 중복 방지) |
| `SetCellFade / GetCellFade` | 렌더러로 위임 (없으면 false/-1) |

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
