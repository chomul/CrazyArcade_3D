# AWaterSegment

> `Gameplay/Bomb/WaterSegment.h/.cpp` · AActor · 비복제 · IPooledActor

## 역할
- 물줄기 1칸 표시. `StartLinger` 후 스스로 풀 반납. 판정 없음

## 주요 함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `StartLinger(Seconds)` | 표시 타이머 — 만료 시 스스로 `Pool->Release` | 비복제 — 각 클라가 Multicast 셀 목록을 받아 로컬 스폰. 서버(데디)에는 아예 안 만듦 |
| `OnReleasedToPool` | 타이머 정지 (풀 계약) | |

## 멀티 처리
**네트워크 없음.** 물줄기 위치는 `MulticastWaterCells`의 셀 목록으로 오고,
이 액터는 각 클라가 그 목록을 보고 만드는 순수 로컬 그림이다

## 왜
- **왜 판정 없음?** → 갇힘은 서버가 셀로 이미 판정. 여기 오버랩을 넣으면 판정 두 벌
- **왜 비복제?** → 셀 목록 Multicast + 각 클라 로컬 스폰이면 충분. 셀당 액터 복제는 낭비
- **왜 풀링?** → 가장 빈번한 시각 액터 — 풀링 이득 최대 지점
- **왜 자체 반납?** → 수명이 "시간 경과" 하나뿐. 소유자 관리 불필요

## 연결
스폰: [19-ExplosionFXRelay.md](19-ExplosionFXRelay.md) · 풀: [14-PoolSubsystem.md](../../Core/14-PoolSubsystem.md)

## Q&A
아직 없음
