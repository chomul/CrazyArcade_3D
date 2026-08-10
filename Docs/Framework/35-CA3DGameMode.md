# ACA3DGameMode

> `Source/CrazyArcade3D/Framework/CA3DGameMode.h/.cpp` · AGameModeBase (서버 전용)

매치 진행자: 시드 결정 → VoxelWorld 초기화 → 스폰 게이트 → 참가 등록 → 봇 채우기 →
사망 배칭 → 승패 판정 → 서든데스 스위치.

## 왜 이렇게 했는가

- **GameMode는 서버에만 존재한다** — 엔진 구조상 클라에 복제되지 않는다. 그래서 매치
  진행 로직을 여기 두면 권한 분리가 공짜다. 클라가 알아야 할 결과만 GameState/PlayerState로.
- **스폰 게이트가 필요한 이유** — 엔진이 로그인(`PostLogin`)을 `World::BeginPlay()`
  **앞에서** 부를 수 있는데, 맵은 BeginPlay에서 시드로 생성된다. 첫 입장자는 지형도
  스폰 셀도 없어 엔진 폴백(원점)에 스폰 → 낙사. 해결이 두 겹:
  ① 지형 준비 전 입장은 `PendingSpawnControllers`에 대기, `StartPlay`의 Super **직후** 해소
  (BeginPlay 안에서 하면 `NotifyBeginPlay` 순회 중이라 폰의 BeginPlay가 누락될 수 있다)
  ② `InitNewPlayer`가 오염된 원점을 `StartSpot`에 굳혀 두므로 `UpdatePlayerStartSpot`·
  `ShouldSpawnAtStartSpot` **둘 다** 오버라이드. ⚠️ PIE는 `APlayerStartPIE` 덕에 면역이라
  이 결함이 PIE로 안 잡혔다 — 영향 범위는 스탠드얼론·리슨·패키징 클라(=데모 빌드).
- **`RegisterParticipant` 단일 경로 (사람·봇 공용)** — 색 배정·참가 수·AliveCount를
  한 함수가 처리한다. 봇도 `bWantsPlayerState`로 PlayerState를 가지므로 승패 판정이
  사람과 봇을 구분하지 않는다.
- **사망을 다음 틱으로 배칭(`ResolvePendingDeaths`)하는 이유 — 동시 사망 = 공동 등수.**
  같은 폭발로 2명이 죽으면 사망 통지가 같은 프레임에 2번 온다. 즉시 처리하면 먼저 온
  쪽이 낮은 등수가 된다(순서는 우연). 한 틱 모아 한꺼번에 판정:
  `SharedRank = Max(2, AliveBefore - K + 1)` — **하한 2가 우승 자리를 비워 두는 장치**
  (마지막 전원 동시 사망 = 무승부, 1등 공석).
- **승패를 GameState에 쓰는 이유** — `bMatchEnded`와 `MatchWinner`가 같은 액터에 있어야
  클라가 종료와 승자를 **원자적으로** 받는다(다른 액터면 복제 도착 순서 미보장 —
  실제로 무승부 오표시 버그가 있었다).
- **중도 이탈 = 사망 처리 재사용** — 폰이 있으면 `ServerKill(Left)`, 없으면
  `NotifyPlayerDeath` 직접(없으면 AliveCount가 영영 안 줄어 매치가 안 끝난다).
  매치 종료 후 이탈은 탈주 표시조차 안 함 — 완주자가 줄줄이 "탈주"가 되는 것 방지.
- **시드 결정에서만 `FMath::Rand()` 허용** — 시드 자체는 무작위여도 된다.
  결정론이 필요한 건 "시드 → 맵" 변환이지 시드 생성이 아니다.

## 연결
- 초기화 대상: [03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) · 결과 기록: [33-CA3DGameState.md](33-CA3DGameState.md) ·
  [34-CA3DPlayerState.md](34-CA3DPlayerState.md) · 사망 통지자: [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
아직 없음 — 질문이 생기면 여기에 쌓는다.
