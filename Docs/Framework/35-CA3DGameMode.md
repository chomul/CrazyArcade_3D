# ACA3DGameMode

> `Framework/CA3DGameMode.h/.cpp` · AGameModeBase — 서버 전용

## 역할
- 매치 시작: 시드 결정 → VoxelWorld 초기화(크기 티어) → 스폰 게이트 → 스폰 셀 배정
- 참가 관리: `RegisterParticipant`(사람·봇 공용) · 봇 채우기 · 중도 이탈
- 승패: `NotifyPlayerDeath` 수집 → 다음 틱 `ResolvePendingDeaths` → GameState 기록
- 서든데스 시작/정지 스위치

## 주요 변수·함수
| 이름 | 설명 | 멀티 이유 |
|---|---|---|
| `bMatchStartResolved` / `PendingSpawnControllers` | 스폰 게이트 상태 | 서버 전용 — GameMode는 클라에 존재하지 않음 (이하 전부 동일) |
| `MatchParticipantCount` / `NextSpawnIndex` | 참가 수 · 스폰 순번 | |
| `PendingDeaths` / `bDeathResolveScheduled` | 사망 배칭 큐 · 중복 예약 방지 | |
| `SpawnCells` / `SpawnStartActors` | 스폰 셀 · PlayerStart 캐시 | |
| `bUseFixedSeed` / `FixedSeed` | 고정 시드 모드 (디버그) | |
| `BeginPlay()` | Rules→GameState → 시드 → VoxelWorld 초기화 → 봇·서든데스 예약 | Rules 세팅이 VoxelWorld 초기화보다 **먼저** — 클라 InitGrid가 GameState.Rules를 기다리는 구조와 짝 |
| `StartPlay()` | Super 직후 `FlushPendingSpawns()` | 게이트 해소 시점 — BeginPlay 안이면 NotifyBeginPlay 순회 중이라 폰 BeginPlay 누락 위험 |
| `PostLogin()` | `RegisterParticipant` (Super보다 먼저) | 엔진이 이걸 World::BeginPlay **앞에서** 부를 수 있음 — 스폰 게이트가 필요한 근본 이유 |
| `HandleStartingNewPlayer` | 게이트 미해소면 대기열로 | 지형 없는 시점의 스폰 = 원점 낙사 (데모 빌드에서 실제로 터진 결함) |
| `ChoosePlayerStart` | 스폰 셀 순서 배정 | 서버 판정 — 클라는 결과 위치만 받음 |
| `UpdatePlayerStartSpot` / `ShouldSpawnAtStartSpot` (override) | StartSpot 재사용 차단 | 엔진이 오염된 원점을 `StartSpot`에 굳혀 재사용 — 둘 다 막아야 함 |
| `RegisterParticipant(Controller)` | 사람·봇 공용 등록 | 색·AliveCount를 복제 값에 기록 — 이후 판정이 사람·봇 무구분 |
| `SpawnFillBots()` | 봇 채우기 | 서버에서만 스폰 — 봇 폰은 일반 복제로 클라에 보임 |
| `NotifyPlayerDeath(PS)` | 사망 수집 → 다음 틱 예약 | **배칭** — 같은 폭발의 동시 사망이 도착 순서(우연)로 등수가 갈리면 안 됨 |
| `ResolvePendingDeaths()` | 공동 등수 → 우승/무승부 확정 | 결과를 GameState·PlayerState **복제로 발행** — 이것이 클라와의 유일한 통신 |
| `HandleParticipantLeft()` (Logout) | 이탈 = 사망 재사용 | 폰 없이 나가도 통지 직접 — 안 하면 AliveCount가 안 줄어 매치가 영영 안 끝남 |
| `StartSuddenDeath / StopSuddenDeath` | 서브시스템 스위치 | 클라 알림은 GameState 플래그 복제로 |

## 왜
- **왜 매치 로직이 여기?** → GameMode는 클라에 없음 — 권한 분리가 공짜
- **왜 스폰 게이트?** → 엔진이 PostLogin을 World::BeginPlay **앞에서** 부름.
  맵은 BeginPlay에서 생성 → 첫 입장자는 지형 없음 → 원점 낙사.
  해소는 StartPlay의 Super **직후** (BeginPlay 안이면 폰 BeginPlay 누락 가능)
- **왜 StartSpot 오버라이드 2개?** → 오염된 원점이 `StartSpot`에 굳음 —
  `UpdatePlayerStartSpot`·`ShouldSpawnAtStartSpot` 둘 다 막아야 함.
  ⚠️ PIE는 APlayerStartPIE 덕에 면역 — 데모 빌드에서만 터졌던 결함
- **왜 RegisterParticipant 단일 경로?** → 색·참가 수·AliveCount가 한 함수.
  봇도 PlayerState를 가져 판정이 사람·봇 무구분
- **왜 사망을 다음 틱 배칭?** → 동시 사망 = 공동 등수. 즉시 처리면 도착 순서(우연)가
  등수를 가름. 공식 `Max(2, AliveBefore-K+1)` — **하한 2 = 우승 자리 비워두기(무승부 장치)**
- **왜 승패를 GameState에?** → bMatchEnded+MatchWinner 원자 복제 (다른 액터면 순서 미보장)
- **왜 이탈 = 사망 재사용?** → 폰 있으면 `ServerKill(Left)`, 없으면 통지 직접
  (안 하면 AliveCount가 안 줄어 매치가 안 끝남). 종료 후 이탈은 탈주 표시 안 함
- **왜 여기만 FMath::Rand 허용?** → 결정론이 필요한 건 "시드→맵"이지 시드 생성이 아님

## 멀티 처리
**클라에 존재하지 않는 서버 전용 액터.** 그래서 이 안의 로직은 권한 가드조차 대부분
불필요하고, 클라와의 통신은 오직 "결정 결과를 GameState·PlayerState 복제 값에 쓰는 것"뿐.
받는 쪽 경로도 없다 — 사망 통지는 서버 안의 StatusComponent가 직접 부른다.

## 연결
[03-VoxelWorld.md](../Voxel/03-VoxelWorld.md) · [33-CA3DGameState.md](33-CA3DGameState.md) · [34-CA3DPlayerState.md](34-CA3DPlayerState.md) · 통지자: [24-StatusComponent.md](../Gameplay/Character/24-StatusComponent.md)

## Q&A
아직 없음
