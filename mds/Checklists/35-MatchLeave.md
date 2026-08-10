# Checklist 35 — 중도 이탈(Logout) 처리

> 2026-08-10 사용자 확정: **"나간 사람은 사망 처리하고 순위 탈주 처리해줘"**
> 회귀 스위트: `CrazyArcade3D.Framework.MatchLeave` · `CA3DGameMode.cpp:164` 의 TODO 해소

## 왜 이게 진짜 구멍이었나
- [x] 이탈 처리가 **없었다** — 3명 중 1명이 알트탭으로 나가면 `AliveCount` 가 3으로 남아
      **남은 둘이 끝까지 싸워도 매치가 영영 안 끝난다**
- [x] 데모를 남에게 보여주면 반드시 밟는 경로다 — 아무도 끝까지 앉아 있지 않는다

## 확정 규칙
- [x] 나간 사람은 **그 자리에서 사망 처리** → 기존 공동 등수 규약대로 순위를 받는다
- [x] 결과 화면에 `3등   아무개 (탈주)` — **등수 자리는 그대로**. 나간 그 자리에서 받은 등수가 결과의 사실이다
- [x] **참가 인원에서 빼지 않는다** (`MatchParticipantCount` 불변) — 그 사람은 참가했고 자리를 차지했다

## 설계의 축 — `OnDeactivated()` 오버라이드
- [x] `APlayerController::CleanupPlayerState`(PlayerController.cpp:1392) → `PlayerState->OnDeactivated()`
      → 기본 구현은 `Destroy()` **한 줄**(PlayerState.cpp:128). 엔진 주석이 *"games can override"* 라고 명시
- [x] 그대로 두면 나간 사람이 `GameState->PlayerArray` 에서 사라져 **결과 화면에서 통째로 증발한다** —
      등수를 부여해 놓고 그 등수를 보여줄 데이터가 없어지는 셈
- [x] `ACA3DPlayerState::OnDeactivated()` 가 **`Super` 를 부르지 않는다**. `APlayerState` 생성자가
      `bAlwaysRelevant = true` 라 소유 컨트롤러가 사라져도 모든 클라에 계속 복제된다 (엔진 소스 확인)
- [x] 대가: 매치가 끝날 때까지 액터가 남는다. 최대 8인 × int32 몇 개라 무시할 수준이고
      레벨 전환에서 회수된다. 되살릴 필요도 없다 — GDD 6.3 에 재접속이 없다
- [x] ⚠️ **엔진 `bIsInactive` 를 재사용하지 않았다** — 그 플래그의 주인은 엔진의 inactive player
      시스템이고 우리가 모르는 시점에 바뀐다. 게다가 우리는 `AGameModeBase` 파생이라
      `AddInactivePlayer` 자체가 없어 그 값을 세울 엔진 경로가 이 프로젝트에 존재하지 않는다

## 밟은 함정 2건 — 지시가 틀렸고 구현이 맞았다

### ① `bAlive = false` 를 미리 세우면 이 Task 가 무효가 된다
- [x] 처음 지시는 폰 없는 경로에서 `NotifyPlayerDeath(PS)` **와** `PS->bAlive = false` 를 둘 다 하라고 했다
- [x] 그런데 `ResolvePendingDeaths` 는 `IsValid && bAlive && FinalRank == 0` 인 항목만 해소한다
      (`CA3DGameMode.cpp:592`). 미리 내리면 다음 틱에 **통째로 걸러져 `AliveCount` 가 영영 안 준다** —
      없애려던 증상이 다른 얼굴로 되살아난다
- [x] `bAlive`·`FinalRank` 를 내리는 곳은 **해소 한 곳뿐**이다. 테스트 ②가 이 경계를 고정

### ② 매치 종료 후 이탈은 탈주가 아니다
- [x] `AController::Destroyed`(Controller.cpp:595)가 `GameMode->Logout(this)` 를 부른다 —
      `APlayerController` 가 아니라 **`AController`** 라 레벨 정리 때 **봇까지 전원**이 통과한다
- [x] 게이트가 없으면 결과 화면이 떠 있는 동안 완주자·**우승자까지 차례로 "탈주"** 로 바뀐다.
      `bLeftMatch` 가 뜻하기로 한 "이 매치를 끝까지 안 뛰었다" 와 정반대
- [x] → `bMatchEnded` 면 표시도 사망 처리도 하지 않는다. (사망 ≠ 매치 종료 —
      "이미 죽은 사람이 나가면 탈주 표시만" 은 그대로 유지)

## 사망 처리는 기존 경로를 탄다
- [x] 폰이 있으면 `UStatusComponent::ServerKill(EDeathCause::Left)` — 폰 숨김·컬리전 off·
      `LifeState`·사인 기록·`NotifyPlayerDeath` 가 전부 그 한 함수에 이미 있다.
      여기서 다시 쓰면 두 벌이 되고, 한쪽만 고쳐지는 순간 "이탈로 죽으면 시체가 안 사라진다" 가 된다
- [x] 폰이 없으면(스폰 게이트 대기 중 이탈) `NotifyPlayerDeath` 를 직접 — **이 경로가 핵심**이다
- [x] `EDeathCause::Left` 는 **맨 끝에 append** (`EBotState` 와 같은 규약 — 중간에 끼우면
      기존 진단 로그의 숫자 의미가 조용히 어긋난다)
- [x] `PendingSpawnControllers` 에서 제거 — 안 하면 `FlushPendingSpawns` 가 파괴 중인 컨트롤러에
      폰을 붙이려 든다
- [x] `BuildResultRows` 의 정렬·공동 등수 규칙은 **한 줄도 안 건드렸다.** 탈주는 표시 속성일 뿐 순위 규칙이 아니다

## 검증 (2026-08-10)
- [x] 프로젝트 파일 재생성 + 두 타깃 빌드 `Result: Succeeded`
- [x] **전체 30스위트 실패 0** (`MatchLeave` 신규 — 29 → 30)
- [x] `LogCA3D` **Error 0건**. Warning 83건은 전부 기존 항목(테스트 월드에 BP 에셋 없음 안내 +
      의도적 실패 경로 테스트)이며 이번 변경과 무관
- [x] 실행 로그에 경로가 실제로 찍혔다:
      `참가자 이탈 — (ColorIndex 0) → 사망 처리(폰 경로), 생존 3명(해소는 다음 틱), 참가 3명 유지` ·
      `→ 사망 처리(폰 없음 — 통지만)` · `이탈 — 매치 종료 후라 탈주 표시·사망 처리 없음` ·
      `PlayerState 는 결과 화면을 위해 남긴다 (등수 3, 탈주)`

### 테스트가 고정한 경계 (8+1)
① 산 사람 이탈 → 등수·`bAlive=false`·`bLeftMatch`·`AliveCount` 감소 ·
② **폰 없는 이탈도 `AliveCount` 가 준다**(핵심 결함 경로) · ③ 이미 죽은 사람 이탈은 이중 감산 없음 ·
④ 3명 중 2명 이탈 → 남은 1명 우승 · ⑤ 전원 이탈 → 무승부(기존 규약 유지) ·
⑥ 보류 스폰 목록에서 제거 · ⑦ `OnDeactivated()` 후에도 `PlayerArray` 에 남는다 ·
⑧ UI 순수 함수 — 탈주 표시가 붙어도 **등수·정렬·공동등수 결과 불변**

## 남은 검증 (미실행 — 체크 금지)
- [ ] **리슨/데디 + 클라 2대에서 실제 접속 끊기** → 남은 클라 결과 화면에 `(탈주)` 표시.
      헤드리스는 넷드라이버가 없어 **복제 자체를 검증하지 못한다**
- [ ] 나간 사람의 폰이 다른 클라 화면에서도 사라지는가 (`ApplyDeathState` 복제)
- [ ] 스폰 게이트 대기 중 실제 접속 끊기 (헤드리스는 손으로 태운 근사치)
- [ ] 마지막 한 명만 남고 나머지가 전부 나갔을 때 결과 화면 전환·종료음

## 알아 둘 것 — 봇은 이 보호를 못 받는다
`AController::CleanupPlayerState`(Controller.cpp:640)는 `PlayerState->Destroy()` 를 **직접** 부른다 —
`OnDeactivated` 훅은 `APlayerController` 경로에만 있다. 즉 `ABotController` 가 매치 중 파괴되면
등수는 부여되지만 결과 행이 사라진다. **지금은 봇을 매치 중 제거하는 경로가 없어 도달 불가**지만,
생기면 `ABotController::CleanupPlayerState` 오버라이드가 필요하다.

## 에디터에서 할 일
**없음.** `WBP_Match` 의 `ResultText` 가 `FormatResultRow` 결과를 그대로 받으므로 위젯 작업도 없다.
