# UExplosionSubsystem

> `Gameplay/Bomb/ExplosionSubsystem.h/.cpp` · UWorldSubsystem

## 역할
- **계산**: `Propagate` — static 순수 함수. 실폭발·프리뷰·봇·서든데스 공용
- **스케줄링**: `RequestDetonate` / `ProcessChainStep` — 연쇄를 단계 타이머로 분산
- **적용**: `ApplyExplosionCells` — 파괴→FX→갇힘→아이템 고정 순서
- **레지스트리**: 폭탄·아이템 셀 조회(`FindBombAt` / `FindItemAt`)

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `PendingChain` | 다음 단계에 터질 폭탄 큐 |
| `ChainTimer` / `bProcessingStep` | 단계 타이머 · 재진입 가드 |
| `Propagate(Grid, Origin, Range, bFloorDestructible, BombCells)` (static) | 6방향 전파 계산 → `FExplosionResult`. 부작용 0 |
| `RequestDetonate(Bomb)` | 연쇄 큐 투입 — 큐가 비어 있었으면 즉시 1단계 |
| `ProcessChainStep()` (내부) | 단계 실행: 폭탄들 Propagate 병합 → 적용 → 다음 단계 예약 |
| `ServerApplyExplosionAt(Origin, Range, bDestroyFloor)` | 폭탄 없는 폭발(서든데스) — 같은 적용 본체 |
| `ApplyExplosionCells(...)` (내부) | ②파괴 ③FX ④갇힘 ⑤아이템 고정 순서 |
| `ProcessStepItems(...)` (내부) | 아이템 소멸 먼저 → 노출 나중 |
| `RegisterBomb / UnregisterBomb / FindBombAt(Cell)` | 폭탄 레지스트리 — 매번 `GetCell()` 되물음 |
| `GetActiveBombCellsSorted()` | 정렬된 폭탄 셀 (Propagate 입력·결정론) |
| `RegisterItem / FindItemAt / GetActiveItemCellsSorted` | 아이템 레지스트리 (봇 목표 탐색) |

## 왜
- **왜 Propagate가 순수 함수?(불변식 2)** → 프리뷰가 같은 함수 = 표시·실제 구조적 일치 ·
  봇이 공짜 조회 · 테스트 가능 · 서든데스가 인자 하나로 무수정 구현
- **왜 연쇄를 단계 분산?** → 폭탄 10개 한 프레임 = 히치. "촤르륵" 연출도 원작의 맛.
  단계마다 그리드 새로 읽어 앞 단계 파괴가 반영됨
- **왜 원점을 연쇄 판정에서 제외?** → 자기 자신 재폭발 = 무한 루프
- **왜 ApplyExplosionCells 분리?** → 폭탄·서든데스가 같은 적용 본체.
  따로면 "서든데스로 부순 블록만 다르게 동작"
- **왜 아이템 소멸 먼저, 노출 나중?** → 뒤집으면 방금 나온 아이템이 같은 폭발에 즉시 탐
- **왜 레지스트리가 캐시 없음?** → `FindBombAt`이 매번 `GetCell()` 되물음.
  킥 이동 시 재등록 불필요 — Cell 갱신이 곧 레지스트리 갱신
- **왜 Multicast를 릴레이에 위임?** → 서브시스템은 액터가 아니라 RPC 불가

## 연결
[15-ExplosionTypes.md](15-ExplosionTypes.md) · 파괴: [03-VoxelWorld.md](../../Voxel/03-VoxelWorld.md) · FX: [19-ExplosionFXRelay.md](19-ExplosionFXRelay.md) · 갇힘: [24-StatusComponent.md](../Character/24-StatusComponent.md)

## Q&A
아직 없음
