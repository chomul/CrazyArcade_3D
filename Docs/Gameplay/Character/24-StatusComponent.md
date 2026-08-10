# UStatusComponent

> `Source/CrazyArcade3D/Gameplay/Character/StatusComponent.h/.cpp` · UActorComponent

스탯(폭탄 수·범위·속도·니들·킥)과 생존 상태(`ELifeState`)의 **단일 출처**.
`ServerApplyItem / ServerTrap / ServerEscape / ServerKill` 네 진입점.

## 왜 이렇게 했는가

- **왜 캐릭터가 아니라 컴포넌트인가** — 설계 결정 8번: "플레이어 상태를 컴포넌트로" —
  봇과 사람의 코드 경로가 같아진다. 캐릭터는 행동, 컴포넌트는 상태로 관심사 분리.
- **RPC가 하나도 없는 이유** — 네 진입점 전부 서버 로컬 함수 + 최상단 `HasAuthority()`
  가드다. 클라 → 서버 요청은 캐릭터의 RPC(`ServerUseNeedle` 등)가 받고, 상태 변경은
  서버 안에서만 일어난다. 상태 변경 표면이 좁을수록 검증할 곳이 준다(불변식 5).
- **`ActiveBombCount`가 비복제인 이유** — 서버 판정에만 쓰인다. 클라 예측은
  "예측 비주얼 개수"로 대신한다 — 복제 지연에 걸린 값으로 예측하면 오히려 틀린다.
- **`ServerTrap`은 Alive만 통과** — 중복 갇힘·시체 갇힘 방지. 덤으로 "갇힌 상대에게
  폭탄은 무효"라는 원작 규칙이 공짜로 성립하고, 봇의 PopTrapped 우선순위(폭탄보다 접촉)의
  근거가 된다.
- **갇힘 → 익사 타이머** — `ServerTrap`이 `TrappedDuration`(4초) 뒤 `ServerKill(Water)`를
  예약. 니들(`ServerEscape`)이 타이머를 해제. 상태 전이가 이 컴포넌트 안에서 완결된다.
- **`EDeathCause`(Water·Fall·SuddenDeath·Popped·Left)를 구분하는 이유** — 사인 집계·디버깅.
  섞이면 "익사 0건이 됐다" 같은 밸런스 신호를 못 읽는다. 새 값은 반드시 끝에 append
  (복제·저장 호환).
- **속도 재계산은 캐릭터의 `RefreshMoveSpeed` 단일 경로** — 롤러·갇힘·사망이 제각각
  속도를 만지면 복원이 꼬인다. 상태가 바뀌면 "다시 계산해라" 한 곳만 부른다.
- **`HasRoomForItem`이 여기 있는 이유** — 봇의 아이템 목표 선별용 판정을 봇이 아니라
  `ServerApplyItem` **바로 옆**에 뒀다. 두 파일로 갈라지면 "봇은 상한인 줄 아는데
  실제로는 아직 오르는" 어긋남이 조용히 생긴다. 가치 판단(범위 vs 속도)은 범위 밖 —
  이 함수는 "받으면 뭔가 오르는가"만 답한다.

## 네트워크 표면
복제 6종: `MaxBombCount`·`BombRange`·`MoveSpeedMul`(OnRep_Stats) · `bHasNeedle`·`bHasKick` ·
`LifeState`(OnRep_Life → `ApplyDeathState` 공통 경로).

## 연결
- 소유자: [23-CA3DCharacter.md](23-CA3DCharacter.md) · 갇힘 호출자: [16-ExplosionSubsystem.md](../Bomb/16-ExplosionSubsystem.md) ·
  사망 통지: [35-CA3DGameMode.md](../../Framework/35-CA3DGameMode.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
