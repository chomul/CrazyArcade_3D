# VoxelMove (namespace)

> `Source/CrazyArcade3D/Voxel/VoxelMovement.h/.cpp` · 순수 함수 모음

그리드 위 이동 규칙의 **단일 출처**. `MaxClimbCells=1`(1칸은 오르고 2칸은 못 오름),
`IsStandable`, `FindLandingInColumn`, `GatherReachableNeighbors`, `CountEscapeDirections`.

## 역할

- 격자 위 **이동 규칙을 정의**한다: 설 수 있는 칸(`IsStandable`), 한 칸 이동으로 도달
  가능한 이웃(`GatherReachableNeighbors` — 1칸 오르기·무제한 낙하), 탈출 방향 수
  (`CountEscapeDirections`).
- 맵 검증기와 봇 BFS가 공유하는 "도달 가능"의 유일한 정의.

## 왜 이렇게 했는가

- **탄생 배경이 곧 존재 이유** — 원래 이동 규칙이 맵 검증기(`FMapValidator`)와 봇 BFS에
  **각각** 들어 있었다. 그러자 "검증은 통과인데 봇은 못 가는 지형"이 실제로 생겼다
  (봇 BFS가 2칸 낙하를 몰랐다). 규칙이 두 벌이면 어긋남이 조용히 생산된다 —
  그래서 한 파일로 추출해 검증기와 봇이 **같은 함수**를 쓰게 했다.
- **점프 높이 상수(`MaxClimbCells=1`)가 이제 한 곳에만 있다** — "1칸은 오르고 2칸은 못
  오른다"는 이동 그래프의 정의이자 곧 게임 규칙이다. 이 값이 갈라지면 맵 검증과
  실제 이동이 다른 세계를 산다.
- **낙하는 무제한인 이유** — 내려가는 방향은 아무 판정이 없다. 봇 `FollowPath`가 내려갈 땐
  아무것도 안 하고 중력이 처리한다 — 물리와 그래프가 자연스럽게 일치한다.
- **`FMoveCaps.bRequireHeadroom`** — 봇만 true(머리 위 공간 요구). 검증기는 기본값.
  같은 함수를 쓰되 호출자별 차이는 인자로 표현 — 함수를 복제하지 않는다.
- **`PlanarDirs[4]` 고정 순서(+X,-X,+Y,-Y)** — BFS 확장 순서의 단일 출처.
  순서가 실행마다 다르면 봇 경로가 비결정적이 된다(디버깅 불가).

## 연결
- 소비자: [11-MapValidator.md](../MapGen/11-MapValidator.md)(flood fill·탈출로) ·
  [37-BotController.md](../AI/37-BotController.md)(BFS 이웃 확장)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
