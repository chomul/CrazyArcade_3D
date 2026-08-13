# ABotController

> `AI/BotController.h/.cpp` · AAIController — 서버 전용 FSM 5상태

## 역할
- 매 틱: 상황 인식(위험·목표) → 상태 결정(`Replan`) → 경로(BFS) → 캐릭터 조작(`FollowPath`)
- 우선순위: **Evade > PopTrapped > 설치 > SeekItem > Attack > Wander**
- 관전용 카메라 각(`CamYawIndex`)을 이동 방향에서 산출
- 판정 로직 소유 안 함 — 전부 기존 함수 조회

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `State` (EBotState) | Wander·Attack·Evade·PopTrapped·SeekItem | 서버 전용 — 봇 컨트롤러는 클라에 존재하지 않음. 클라는 봇의 "폰"만 일반 복제로 봄 |
| `PathCells` / `PathIndex` | BFS 경로·진행 위치 | 서버 전용 |
| `DangerCells` (TSet) | 위험 셀 집합 — 판정·통행 공용 | 서버에서 `Propagate` 재실행 — 폭탄 복제 값이 아니라 서버 원본을 읽음 |
| `bPlanFailed` / `TimeSinceReplan` | 폭주 차단 · 주기 | |
| `RandomStream` | ColorIndex 시드 | 봇별 독립·재현 — 서버 로컬이라 결정론 제약은 디버깅 편의 목적 |
| `Tick()` | 인식 → 재계획 → FollowPath | `HasAuthority()` 가드 — 봇 로직 전체가 서버 틱 |
| `Replan()` | 우선순위 분기 전부 | |
| `GatherDangerCells()` | 폭탄 전부 Propagate 병합 | |
| `ShouldPlaceBombAt(Cell)` | 이득 판정 + 탈출로 BFS | |
| `Plan*` 4종 | 상태별 목표·경로 | |
| `RunBFS(Start, Goals)` | VoxelMove 확장 · 상한 1024 | |
| `FollowPath(FootCell)` | 웨이포인트 소비 → `Move`/`DoJump` | **사람과 같은 진입점** 호출 — 봇의 이동도 CMC를 타서 클라에 사람과 똑같이 복제됨 |
| `BotMoveCaps()` | 봇 전용 이동 옵션 | |
| (Tick 안) `CamYawIndex` 기록 | 이동 방향 스냅 | 복제 값 쓰기 — 관전자가 봇을 볼 때의 각. **데디 가드 없음**(시각이 아니라 상태) |

## 왜
- **왜 BT가 아니라 FSM?** → 3주 일정, 상태 5개 규모. C++의 디버깅·테스트·결정론이 이김
- **왜 판정 재사용?** → 위험=`Propagate`(폭탄이 든 Range로) · 이동=`VoxelMove`(검증기와
  동일) · 아이템 가치=`HasRoomForItem` · 행동=사람과 같은 진입점.
  봇 전용 판정 = 검증 두 벌
- **우선순위 근거** → PopTrapped>설치: 갇힌 상대에게 폭탄 무효, 접촉은 확정 킬 /
  SeekItem<설치: 폭탄 놓기가 곧 아이템 생성 / SeekItem>Attack: Attack은 쿨다운
  없어 아래 두면 영영 미발동. 실측: SeekItem 추가 후 파괴 61→78 (배치 검증)
- **왜 설치에 탈출로 BFS 필수?** → 없으면 자폭 반복
- **왜 위험 집합 하나 공유?** → 셀마다 Propagate 재실행 = 비용 곱 + 경로 갈라짐
- **왜 bPlanFailed?** → 실패→매 틱 재계획 폭주 차단 (전원 갇힌 상황이 최악)
- **BFS 결정론** → 방문 TMap 조회 전용 · `PlanarDirs` 고정 순서 · 난수는 ColorIndex 시드
- **왜 FollowPath에 경로 폐기 가드?** → 발판이 폭발로 사라지면 옛 코드는 벽에 붙어 점프
- **왜 MaxPathNodes 1024?** → 512는 잘림≠실패 구분 불가 — 먼 상대를 도달 불가로 오판
- **왜 CamYawIndex에 데디 가드 없음?** → 복제 값 = 상태이지 시각이 아님

## 멀티 처리
**봇은 서버에만 존재하고, 조작 결과만 사람과 같은 복제 경로로 나간다.**
이동은 CMC 복제, 폭탄은 서버 직행 설치(예측 생략), 상태는 PlayerState·StatusComponent 복제.
클라 입장에서 봇 폰과 사람 폰은 구분되지 않는다 — 봇 전용 네트워크 코드가 0인 이유.

## 연결
[16-ExplosionSubsystem.md](../Gameplay/Bomb/16-ExplosionSubsystem.md) · [06-VoxelMovement.md](../Voxel/06-VoxelMovement.md) · [23-CA3DCharacter.md](../Gameplay/Character/23-CA3DCharacter.md) · [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
- **Q. BFS면 따로 AI 안 쓰고 BFS로 이동하는 형식?** → 아니. AI(FSM)가 따로 있고
  BFS는 그 AI가 쓰는 **길찾기 도구**일 뿐. 봇의 한 틱은 3단:
  ① **결정 = FSM** — `Replan()`이 우선순위로 "뭘 할까"·목표 셀을 정함(지능의 본체)
  ② **경로 = BFS** — 목표가 정해진 다음에야 `RunBFS`가 길만 냄(판단 없음)
  ③ **실행 = 물리** — `FollowPath`가 사람과 같은 `Move`/`DoJump` → CMC.
  한 줄: "무엇을 = FSM / 어디로 = BFS / 실제 이동 = CMC".
  엔진 AI를 안 쓴 것도 의도: BT 대신 FSM(상태 5개 규모 + 디버깅·결정론),
  NavMesh 대신 그리드 BFS(지형이 실시간 변형 + 답의 단위가 셀 +
  `VoxelMove` 규칙 재사용) — VoxelRay의 "LineTrace 대신 DDA"와 같은 결:
  격자 질문엔 격자 도구
