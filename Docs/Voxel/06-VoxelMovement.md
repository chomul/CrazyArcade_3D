# VoxelMove (namespace)

> `Voxel/VoxelMovement.h/.cpp` · 순수 함수 — `MaxClimbCells=1`

## 역할
- 격자 이동 규칙 정의: `IsStandable` · `GatherReachableNeighbors`(1칸 오르기·무제한 낙하) ·
  `CountEscapeDirections`
- 맵 검증기·봇 BFS가 공유하는 "도달 가능"의 유일한 정의

## 주요 변수·함수
| 이름 | 설명 |
|---|---|
| `MaxClimbCells = 1` | 점프로 오를 수 있는 높이 (constexpr — 게임 규칙) |
| `FMoveCaps` | 호출자별 옵션 — `bRequireHeadroom`(봇만 true) |
| `PlanarDirs[4]` | +X,-X,+Y,-Y 고정 순서 — BFS 확장 순서의 단일 출처 |
| `IsStandable(Grid, Cell)` | 그 칸에 설 수 있는가 (발밑 솔리드 + 몸 공간 Empty) |
| `FindLandingInColumn(Grid, From, Dir)` | 그 방향 한 칸의 착지 셀 탐색 (오르기 1칸·낙하 무제한) |
| `GatherReachableNeighbors(Grid, From)` | 한 걸음에 갈 수 있는 이웃들 — BFS·flood fill의 심장 |
| `CountEscapeDirections(Grid, From)` | 4방향 중 이동 가능한 수 (스폰 검증 ③) |

## 왜
- **왜 존재?** → 이동 규칙이 검증기·봇에 각각 있었다가 "검증은 통과인데 봇은 못 가는
  지형"이 실제로 발생(봇이 2칸 낙하를 몰랐음). 한 곳으로 통일
- **왜 낙하 무제한?** → 내려갈 땐 판정 없음, 중력이 처리. 물리와 그래프가 자연 일치
- **`FMoveCaps`?** → 호출자별 차이(봇만 머리 공간 요구)를 함수 복제 대신 인자로
- **왜 `PlanarDirs` 고정 순서?** → BFS 확장 순서 = 봇 경로 결정론

## 연결
[11-MapValidator.md](../MapGen/11-MapValidator.md) · [37-BotController.md](../AI/37-BotController.md)

## Q&A
아직 없음
