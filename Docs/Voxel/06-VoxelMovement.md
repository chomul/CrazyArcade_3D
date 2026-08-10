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
- **Q. 캐릭터 관련 처리는 캐릭터에 넣는 게 맞지 않나? 왜 따로 있나?** →
  그 원칙은 맞고 실제 이동 **처리**(물리·점프·입력)는 캐릭터+CMC에 있다.
  VoxelMove는 처리가 아니라 **이동 규칙서** — "격자 관점에서 갈 수 있는가"의 순수 조회이고
  캐릭터를 한 줄도 안 만짐. 캐릭터에 못 넣는 결정적 이유는 **읽는 쪽이 캐릭터가 아니라서**:
  ① 맵 검증기는 캐릭터 스폰 전(월드 없는 테스트 포함)에 돌고 ② 봇 BFS는 미래 칸 수백 개의
  순수 조회. 게다가 캐릭터(Gameplay)에 두면 MapGen→Gameplay 의존이 생겨 폴더 규칙 붕괴 —
  Voxel이 MapGen·AI 둘 다 닿는 유일한 아래층. CMC와의 일치는 확정 규칙("1칸만 오름")을
  양쪽이 각자 언어로 표현(룰셋 1.4칸 정점 / MaxClimbCells=1)해서 성립.
  비유: 나이트의 L자 규칙은 기물이 아니라 규칙서에 있다 — AI도 검증기도 같은 규칙서를 읽는다
- **Q. 그럼 말 그대로 규칙이고, 봇이랑 플레이어가 이 함수들을 이용하는 건가?** →
  규칙 맞음. 단 **플레이어는 이 함수를 안 쓴다** — 사용자는 봇·검증기 둘뿐.
  이동의 세계가 둘이라서: 플레이어는 **연속 물리**(CMC — "갈 수 있나" 안 묻고 그냥
  움직이면 물리가 결정), 봇·검증기는 **이산 격자**(물리를 미리 돌릴 수 없으니 칸 단위
  즉답 조회 필요). 두 세계의 일치는 같은 확정 규칙("1칸만 오름")을 물리 튜닝(정점
  1.4칸)과 규칙서(MaxClimbCells=1)가 각자 표현해서 성립.
  봇도 **계획은 격자, 실행은 물리** — 경로는 VoxelMove로 세우고 움직임은 플레이어와
  같은 Move/DoJump로 CMC를 탄다
