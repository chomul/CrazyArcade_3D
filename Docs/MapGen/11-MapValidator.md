# FMapValidator

> `MapGen/MapValidator.h/.cpp` · 전부 static 순수 함수

## 역할
- 맵의 플레이 가능성 판정 — 리롤 루프의 관문. 실패 사유를 문자열로 보고
- 검증 5종: ①스폰 연결 ②최소 거리 ③탈출로 ④고립 구역 없음 ⑤아이템 사분면 균등

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `FThresholds` | 임계값 묶음 — 스폰 최소 거리(8)·탈출로 수(2)·아이템 편차 |
| `Validate(...)` | 5종 일괄 실행(비용순) + 실패 사유 문자열 |
| `AreAllSpawnsConnected` | ① 스폰 전원이 서로 도달 가능한가 (flood fill) |
| `HaveSpawnsMinDistance` | ② 스폰 간 맨해튼 거리 최소값 |
| `HaveSpawnsEscapeRoutes` | ③ 스폰마다 이동 가능 방향 ≥ 2 |
| `HasNoIsolatedRegion` | ④ 도달 불가 구역 없음 (**최외곽 링 제외**) |
| `AreItemsBalanced` | ⑤ 사분면별 아이템 수 편차 |
| `FloodFillStandable` (내부) | TArray 큐 flood fill — 결정론 유지 |

## 왜
- **왜 순수 함수?** → 리롤 루프에서 수십 번 호출. 상태가 있으면 결정론 붕괴
- **이동 정의는 `VoxelMove`** → 자체 규칙을 들면 봇과 갈라짐 (실제로 그랬음)
- **⚠️ ④는 최외곽 링 제외** → 벽 꼭대기 도달 불가는 **설계**. 제외 안 하면 정상 맵
  전부 불통과 → 무한 리롤 (첫 구현에서 폴백 맵이 실제로 불통과)
- **왜 비용순 실행(②③⑤→①④)?** → 싼 검사로 먼저 걸러 BFS 비용 절약
- **왜 flood fill이 TArray 큐?** → TQueue·TSet 순회는 순서 불보장 (불변식 4)
- **임계값 근거 = 실측** → 폴백 맵 스폰 거리 9 → 임계 8

## 연결
[06-VoxelMovement.md](../Voxel/06-VoxelMovement.md) · 호출: [09-ProcMapGenerator.md](09-ProcMapGenerator.md)

## Q&A
아직 없음
