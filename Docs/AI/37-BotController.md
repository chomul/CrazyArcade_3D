# ABotController

> `Source/CrazyArcade3D/AI/BotController.h/.cpp` · AAIController (서버 전용)

순수 C++ FSM 봇. 상태 5개: `Wander · Attack · Evade · PopTrapped · SeekItem`.
우선순위(= `Replan`의 코드 순서): **Evade > PopTrapped > 설치 > SeekItem > Attack > Wander**.

## 역할

- 매 틱: **상황 인식**(위험 셀 수집·목표 탐색) → **상태 결정**(`Replan` — 우선순위 분기)
  → **경로 계획**(그리드 BFS) → **캐릭터 조작**(`FollowPath` — 이동·점프·설치).
- 관전자를 위한 카메라 각(`CamYawIndex`)을 이동 방향에서 산출해 기록.
- 판정 로직을 소유하지 않는다 — 위험·이동 가능·아이템 가치 전부 기존 함수를 조회만.

## 왜 이렇게 했는가

- **왜 Behavior Tree가 아니라 C++ FSM인가** — 설계 결정 12번: BT 학습 비용 회피(3주 일정).
  상태 5개 규모에서 BT의 이점(디자이너 편집·시각화)보다 C++의 이점(디버깅·테스트·결정론)이 크다.
- **판단은 전부 기존 함수 재사용 — 봇 전용 판정이 없다**
  - 위험: `Propagate`(활성 폭탄 전부, **폭탄이 든 Range**로 — 설치 후 포션을 먹어도
    폭탄의 실폭발 범위가 기준)
  - 설치 이득: `ShouldPlaceBombAt` = `Propagate` + **탈출로 BFS 통과 필수**(없으면 자폭 반복)
  - 이동 가능: `VoxelMove::GatherReachableNeighbors` = **맵 검증기와 같은 함수**
  - 아이템 가치: `StatusComponent::HasRoomForItem` = `ServerApplyItem` 바로 옆
  - 행동: `Move`/`DoJump`/`TryPlaceBombPredicted`/`TryUseNeedle` = 사람과 같은 진입점
- **우선순위의 근거**
  - PopTrapped가 설치보다 위: 갇힌 상대에게 폭탄은 무효(`ServerTrap`이 Alive만 받음),
    접촉은 확정 킬
  - SeekItem이 설치보다 아래: 설치 분기는 이미 "지금 자리가 놓을 자리"이고,
    **폭탄을 놓는 것이 곧 아이템을 만드는 행위**다(파괴 블록에서 나온다)
  - SeekItem이 Attack보다 위: Attack은 쿨다운이 없어 아래 두면 영영 발동 안 함
  - 실측 검증: SeekItem 추가 후 파괴 묶음이 61→78로 오히려 증가(배치가 맞았다는 증거)
- **`GatherDangerCells` 집합 하나를 위험 판정과 BFS 통행이 공유** — 셀마다 Propagate를
  재실행하면 비용이 곱해지고 두 경로가 갈라진다.
- **`bPlanFailed` 플래그** — 빈 경로 → 재계획 → 또 실패가 매 틱 돌면 전원이 갇힌 상황에서
  서버가 가장 비싸진다. 실패를 기억하고 주기까지 기다린다.
- **BFS 결정론 규율** — 방문 집합 TMap은 조회 전용(순회 금지), 확장 순서는
  `VoxelMove::PlanarDirs` 고정, 난수는 `ColorIndex+1` 시드 스트림(봇마다 독립·재현 가능).
- **`FollowPath`의 가드** — 오를 수 없는 높이면 경로 폐기(발판이 폭발로 사라지면 옛 코드는
  매 틱 벽에 붙어 점프했다). 내려갈 땐 아무것도 안 함 — 중력이 처리(낙하 무제한 공짜).
- **`BotMaxPathNodes` 1024인 이유** — 512면 Z6층+낙하 무제한에서 BFS가 상한에 잘리는데
  **잘림과 실패가 구분되지 않아** 먼 상대를 도달 불가로 오판했다.
- **봇의 `CamYawIndex`** — 카메라가 없으므로 이동 방향을 히스테리시스(12도) 스냅해 기록.
  관전자가 봇을 볼 때의 각. 복제 값이므로 데디 가드를 하지 않는다(시각이 아니라 상태).

## 연결
- 소비: [16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md) · [06-VoxelMovement.md](../Voxel/06-VoxelMovement.md) ·
  [23-CA3DCharacter.md](../Gameplay/Character/23-CA3DCharacter.md) · [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
