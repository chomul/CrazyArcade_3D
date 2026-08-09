# Checklist 32 — 호스트 스폰 낙사 (지형보다 먼저 들어온 사람)

> 2026-08-09 발견 · 수정. **데모 빌드 경로에서 사람이 매번 즉사하던 결함이다.**
> 회귀 스위트: `CrazyArcade3D.Framework.SpawnGate` (26번째)

## 증상 — `-game` 세션마다 100% 재현
```
23.114  참가자 입장 — 총 1명, ColorIndex 0        ← PostLogin
23.146  매치 시드 82952512                         ← GameMode::BeginPlay (32ms 뒤)
23.182  HISMVoxelRenderer: 총 셀 2646 ...          ← 지형 완성
25.938  KillZ(-500) 아래로 낙하 — 낙사 처리 (추락)  ← 2.8초 뒤 사망
```
- [x] 로컬 플레이어가 **매치 시작 3초 만에** 죽는다. 봇은 +4초에 들어와 멀쩡하다
- [x] 그래서 이 결함이 그동안 **"관전 기능이 잘 도는 증거"로 오독**됐다 — 모든 헤드리스 세션에서
      로컬 플레이어가 초반에 죽고 관전으로 넘어갔는데, 아무도 그 죽음의 원인을 묻지 않았다

## ⚠️ 왜 PIE 로는 안 잡히나 (이 프로젝트 세 번째 사례)
- [x] **순서는 PIE 와 `-game` 이 완전히 같다.** 두 경로 모두 `SpawnPlayActor`(→`Login`→`PostLogin`)를
      `World->BeginPlay()` **앞에서** 부른다
      (`UEngine::LoadMap` / `UGameInstance::StartPlayInEditorGameInstance`)
- [x] 다른 것은 **PIE 에만 그물이 하나 더 있다**는 점이다:
      `UGameInstance::InitializeForPlayInEditor` 가 `SetGameMode` 전에
      **`APlayerStartPIE` 를 뷰포트 카메라 위치에 스폰**하고(`SpawnPlayFromHereStart`),
      엔진 폴백이 그것을 **최우선으로** 고른다
      (`AGameModeBase::ChoosePlayerStart_Implementation` 의 `IsA<APlayerStartPIE>()` 조기 탈출).
      카메라는 보통 아레나 위라 30ms 뒤 완성되는 지형 위로 떨어져 산다
- [x] 즉 **PIE 의 면역은 에디터 사용자 설정에 달려 있다** — 플레이 드롭다운을 "Default Player Start"
      로 바꾸면 PIE 에서도 즉사한다. 우연히 가려져 있던 것이지 안전했던 적이 없다
- [x] 앞선 두 사례와 같은 계열: **HISM 컬리전 데디 가드**(PIE 데디는 에디터 프로세스라
      `IsRunningDedicatedServer()` 가 false) · **`bReplicateMovement`**(서버 한 곳에서 도는
      테스트로는 안 잡힘). 공통점은 **"검증 환경이 진짜 실행 환경보다 관대하다"** 는 것

## 영향 범위
| 경로 | 영향 |
|---|---|
| 스탠드얼론 · **패키징 클라이언트** · 리슨 호스트 | **사람이 매번 즉사** — 데모 빌드가 이 경로다 |
| PIE | 우연히 가려짐 (위) |
| 데디 서버 | 없음 — 로컬 플레이어가 0명이라 `SpawnPlayActor` 루프가 비고, 사람은 `BeginPlay` 이후 접속 |

## 원인은 **두 겹**이었다 (1차 수정으로 안 고쳐졌다)

### 겹 ① 지형보다 먼저 스폰된다
- [x] `ChoosePlayerStart` 의 `!VoxelWorld || SpawnCells.Num() == 0` 가드가 **엔진 폴백**으로 넘긴다
- [x] 이 레벨에는 `APlayerStart` 가 없다(맵을 시드로 생성하므로) → `FindPlayerStart_Implementation`
      이 `World->GetWorldSettings()`(원점)를 돌려준다. 엔진 주석도 그렇게 적혀 있다
- [x] **수정**: 게이트(`bMatchStartResolved`) — 지형 준비 전 입장은 `PendingSpawnControllers` 에
      쌓아 두고 폰을 만들지 않는다. `BeginPlay` 끝에서 게이트를 열고 `StartPlay()` 의
      `Super` **직후** 해소한다
- [x] 해소를 `BeginPlay` 안에서 하지 않은 이유: `NotifyBeginPlay` 가 액터 목록을 **순회 중**이고
      `SetBegunPlay(true)` 는 순회 뒤다. 그 안에서 폰을 스폰하면 **폰의 BeginPlay 가 통째로
      누락될 수 있다** (순회가 이미 그 자리를 지나갔을 수 있다)
- [x] `RegisterParticipant`(색 인덱스·`AliveCount`·`MatchParticipantCount`)는 손대지 않았다 —
      **참가 등록과 폰 스폰만 분리**했다. `GetNumPlayers()` 는 컨트롤러를 세므로 맵 크기 티어와
      봇 채우기도 대기 중인 사람을 그대로 포함한다

### 겹 ② 엔진이 원점을 **기억**하고 있었다 (1차 수정 후에도 계속 죽었다)
- [x] `AGameModeBase::InitNewPlayer`(Login 단계, 게이트보다 앞)가 `UpdatePlayerStartSpot` 을 부르고,
      그 결과(= 폴백 원점)를 **`Player->StartSpot` 에 저장**한다
- [x] 나중에 해소 → `RestartPlayer` → `FindPlayerStart_Implementation` 이 맨 앞에서
      `ShouldSpawnAtStartSpot(Player)`(= `StartSpot != nullptr`)를 보고 **우리 스폰 셀을
      쳐다보지도 않고** 그 원점을 재사용한다. 게이트가 지형을 기다린 의미가 통째로 사라진다
- [x] **수정 (a)** `UpdatePlayerStartSpot` 오버라이드 — 로그인 단계에서는 자리를 정하지 않는다.
      `true` 를 돌려줘 `"Could not find a starting spot"` 경고도 막는다: **자리를 못 찾은 게
      아니라 아직 고를 때가 아니다**
- [x] **수정 (b)** `ShouldSpawnAtStartSpot` → `false`. "스폰 위치는 **항상** 생성된 스폰 셀에서
      고른다" 는 계약의 문장이다. UE 5.8 에서는 (a) 만으로도 동작하지만
      (`InitStartSpot_Implementation` 이 비어 있고 `StartSpot` 을 채우는 곳이
      `UpdatePlayerStartSpot` 하나뿐임을 grep 으로 확인), (b) 없이는 **부활 기능이 들어오거나
      엔진이 그 훅을 채우는 순간 아무 경고 없이 되살아난다**

### 덤으로 잡힌 결함 — 스폰 인덱스 이중 소비
- [x] 로그인 단계의 `ChoosePlayerStart` 가 `NextSpawnIndex++` 를 **한 번 더** 돌리고 있었다.
      1인당 2칸 → 셀 8개에 5명 이상이면 인덱스가 되감기고, `SpawnStartActors` 캐시가 같은 액터를
      돌려주므로 **두 명이 완전히 같은 좌표에 겹쳐 스폰**된다. (a) 가 호출 자체를 없애 해소.
      ⚠️ 이건 **코드 추론이며 8인 실측으로 재현하지는 않았다** (테스트 ⑥ 이 인덱스 소비를 직접 검사)

## ⚠️ 1차 수정 때 26스위트가 **전부 통과했는데 실제 경로는 깨져 있었다**
- [x] 원인: 테스트가 `Login` 단계를 통째로 빼먹고 `PostLogin` 만 재현했다.
      **"폰이 안 생긴다"만 봤지, "어디에 생기는가"를 정하는 값(`StartSpot`)이 이미 오염돼 있다는
      것을 재현하지 않았다.** 순서 재현을 줄인 만큼 결함이 그 틈으로 통과했다
- [x] 교훈: **엔진 흐름을 흉내 내는 테스트는 "무엇을 건너뛰었는가" 가 곧 사각지대다.**
      건너뛴 단계를 주석에 적어 두면 다음 사람이 그 틈을 먼저 본다
- [x] 보강: ①-a 로그인이 `StartSpot` 을 굳히지 않고 인덱스도 안 돌리는지 · ①-b 굳어 버린 경우를
      **엔진 원본이 저장했을 바로 그 액터**로 강제 재현 · ④ 위치 검사에 **"원점이 아님" 을 따로
      배제**(기존 검사도 실패는 했겠지만 메시지가 "위치가 다르다" 라 원인이 안 보인다) ·
      ⑥ 늦은 로그인(데디 경로)이 인덱스를 태우지 않는지 · ⑧ 정상 스폰 후 `StartSpot` 이 비어 있다는
      **전제 자체**를 검사

## 검증
- [x] 두 타깃 빌드 `Result: Succeeded`
- [x] **전체 26스위트 실패 0** (`SpawnGate` 신규)
- [x] `-game` 실전 (대형 맵 · 봇 6 · 190초):
      `지형 준비 전 입장 — 폰 스폰 보류` → `보류 폰 스폰 해소` → **로컬 플레이어가 9초간 생존하다
      봇 폭탄 물방울에 갇혀 익사**(`EDeathCause::Water`). 허공이 아니라 게임 안에서 죽었다
- [x] `ChoosePlayerStart` 폴백 경고 **0건** (수정 전에는 시작마다 1회)
- [x] `LogCA3D` Warning·Error **0건**

## 남은 검증 (미실행 — 체크 금지)
- [ ] **사람이 직접 `-game`(또는 패키징 빌드)으로 시작해 살아 있는지** — 헤드리스는 입력이 없다
- [ ] 리슨 호스트 + 원격 클라 2인: 둘 다 서로 다른 스폰 셀에 뜨는지
- [ ] 5인 이상에서 스폰 겹침이 실제로 사라졌는지 (이중 소비 수정의 실측)
- [ ] PIE 를 "Default Player Start" 모드로 바꿔 실행 — 수정 전이라면 즉사했을 설정이다.
      이제 살아야 정상이고, 그게 PIE 에서 이 수정을 확인하는 유일한 방법이다

## 에디터에서 할 일
**없음.** 순수 C++ 수정이다. 오히려 이 수정으로 **레벨에 `PlayerStart` 를 두는 우회책이
불필요해졌다** — 두면 오히려 "스폰 셀 대신 그 액터" 라는 새 경로가 생기니 두지 말 것.
