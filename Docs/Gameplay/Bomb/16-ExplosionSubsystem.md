# UExplosionSubsystem

> `Gameplay/Bomb/ExplosionSubsystem.h/.cpp` · UWorldSubsystem

## 역할
- **계산**: `Propagate` — static 순수 함수. 실폭발·프리뷰·봇·서든데스 공용
- **스케줄링**: `RequestDetonate` / `ProcessChainStep` — 연쇄를 단계 타이머로 분산
- **적용**: `ApplyExplosionCells` — 파괴→FX→갇힘→아이템 고정 순서
- **레지스트리**: 폭탄·아이템 셀 조회(`FindBombAt` / `FindItemAt`)

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `PendingChain` / `ChainTimer` | 연쇄 큐·단계 타이머 | 서버 전용 — 폭발 확정은 권한. 클라는 결과만 받음 |
| `Propagate(...)` (static) | 6방향 전파 계산 → `FExplosionResult`. 부작용 0 | **전송 없음** — 클라도 같은 함수를 로컬 실행(위험 프리뷰). 같은 함수라 서버와 어긋날 수 없음 |
| `RequestDetonate(Bomb)` | 연쇄 큐 투입 — 비어 있었으면 즉시 1단계 | 서버 전용 |
| `ProcessChainStep()` (내부) | 폭탄들 Propagate 병합 → 적용 → 다음 예약 | 서버 전용 — 클라 통지는 파괴 Multicast(VoxelWorld)와 물줄기 Multicast(FXRelay)가 담당 |
| `ServerApplyExplosionAt(...)` | 폭탄 없는 폭발(서든데스) — 같은 적용 본체 | `HasAuthority()` 가드 — 지형 파괴는 권한 |
| `ApplyExplosionCells(...)` (내부) | ②파괴 ③FX ④갇힘 ⑤아이템 고정 순서 | 파괴 복제는 `ServerDestroyBlocks`에 위임 — 이 클래스는 복제를 직접 안 함 |
| `ProcessStepItems(...)` (내부) | 아이템 소멸 먼저 → 노출 나중 | 서버 — 아이템은 복제 액터라 스폰·파괴 자체가 전달 수단 |
| `RegisterBomb / FindBombAt(Cell)` | 폭탄 레지스트리 — 매번 `GetCell()` 되물음 | 서버 로컬 — 클라에 폭탄 목록을 따로 보낼 필요 없음(액터 복제로 이미 감) |
| `GetActiveBombCellsSorted()` | 정렬된 폭탄 셀 | 정렬 = 결정론(Propagate 입력 순서 고정) — 전송용 아님 |
| `RegisterItem / FindItemAt / ...Sorted` | 아이템 레지스트리 (봇 목표) | 서버 로컬 |

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

## 멀티 처리
서브시스템은 서버·클라 양쪽 월드에 존재하지만 **폭발 확정·적용은 서버에서만** 돈다.
클라 쪽 인스턴스는 `Propagate`(프리뷰)와 레지스트리 조회에만 쓰인다.
클라 통지는 직접 하지 않고 파괴는 VoxelWorld Multicast, 물줄기는 FXRelay가 대신 방송한다.

## 연결
[15-ExplosionTypes.md](15-ExplosionTypes.md) · 파괴: [03-VoxelWorld.md](../../Voxel/03-VoxelWorld.md) · FX: [19-ExplosionFXRelay.md](19-ExplosionFXRelay.md) · 갇힘: [24-StatusComponent.md](../Character/24-StatusComponent.md)

## Q&A
아직 없음
