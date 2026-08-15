# Checklist 41 — 로비 페이즈(방장·준비·시작) + 매치 HUD 페이즈 숨김

> 2026-08-16 사용자 요청(패키징 멀티 테스트 직후) 2건:
> ① "사람이 모이고 게임 시작 버튼이 눌린 다음에 캐릭터 선택창이 진행되어야 할 것 같아 —
>    처음 들어온 사람이 방장이고 나머지는 준비 버튼, 모두 준비가 끝나야 방장이 시작할 수 있도록"
> ② "캐릭터 선택할 때 왼쪽 아래 아이템 현황 UI 가 보여 — 이건 게임 안에서만 보이면 됨"
> 스위트 신규 2: `Framework.Lobby` · `UI.Lobby` (+ `UI.MatchWidget` 확장) — 전체 **35스위트**

## 설계 — Task 36 을 한 칸 앞에 복제했다

로비는 **캐릭터 선택 페이즈의 구조를 그대로 앞에 놓은 것**이다. 새 개념을 만들지 않았다:
페이즈 플래그 복제 · GameMode 단독 갱신 · 스폰 게이트 연장 · 요청 단일 경로 · 콘솔 폴백.

- [x] `ACA3DGameState::bLobbyActive` 복제 1개. **짝이 되는 "종료 시각" 필드가 없다** —
      로비는 시간 제한이 없다(방장이 누를 때까지). 선택 페이즈와 다른 유일한 점
- [x] `ACA3DPlayerState::bReady`·`bIsHost` 복제 2개, 로직 0. 조건 없이 전원 복제 —
      남의 준비 현황을 그려야 하고 UI 버튼 활성이 같은 입력으로 계산된다
- [x] 룰셋 `bUseLobby` **C++ 기본 false**(= 기존 흐름). 실제 true 는 `DA_Rules_Default`.
      Task 36 의 `CharacterSelectDuration=0` 과 완전히 같은 무회귀 전략
- [x] 요청 단일 경로 `TrySetReady` · `TryStartMatchFromLobby` (`TryAssignCharacter` 관례).
      PlayerController 의 `ServerSetReady`/`ServerStartMatch` 는 **얇은 껍데기** — 판정은 서버 단독
- [x] **시작 가능 판정은 static 순수 함수 `CanStartFromLobby` 하나** — 서버 판정과 UI 버튼 활성이
      같은 함수를 통과한다. 두 벌이면 "버튼은 활성인데 눌러도 안 되는" 어긋남이 생긴다
      (`UMatchWidget` 표시 가공을 폴백 HUD 와 공유하는 것과 같은 근거)
- [x] 로비에서는 **타이머를 하나도 걸지 않는다** — 봇·서든데스·선택 페이즈 전부. 하나라도 걸면
      "로비를 오래 끌면 시작하자마자 서든데스"가 된다. 매치의 모든 시계는 `EndLobby` 부터
- [x] `StartCharacterSelect()` 추출 — `BeginPlay`(로비 미사용)와 `EndLobby`(로비 사용)가 **같은 함수**를
      탄다. 분기 사본을 두면 "로비를 켰을 때만 봇이 안 들어오는" 식으로 조용히 갈라진다

## 방장 규칙

- [x] 첫 입장 **사람**이 방장. **봇은 절대 방장이 아니다**(봇이 먼저 등록돼도 마찬가지)
- [x] 방장 이탈 → 남은 사람 중 `ColorIndex` 최소에게 승계 + **승계자의 `bReady` 해제**
      ("준비 완료인 방장"이라는 표현 불가능한 상태를 남기지 않는다)
- [x] ⚠️ 승계 순간 시작 조건이 자동 충족돼도 **자동 시작하지 않는다** — 시작은 언제나
      방장의 명시적 요청뿐. 테스트 ⑦ 이 이걸 못 박는다
- [x] `bIsHost` 는 `bUseLobby` 와 무관하게 배정한다("첫 입장 사람"은 로비 유무와 무관한 사실).
      로비가 없으면 이 값을 읽는 경로가 아예 안 돈다

## 스폰 게이트 연장 (32-SpawnGate 계열 — 가장 위험한 자리)

- [x] `PendingSpawnControllers` **하나**를 그대로 쓴다. 보류 조건에 `bLobbyActive` 추가,
      `FlushPendingSpawns` 조기 반환에도 추가. **목록도 해소 지점도 늘리지 않았다**
- [x] `EndLobby` 가 해소를 한 번 두드린다 — 선택 페이즈를 **쓰는** 설정에서는 플래그를 보고
      그대로 되돌아가고(해소는 `EndCharacterSelect` 한 곳), **안 쓰는** 설정에서는 여기서 해소된다.
      이게 없으면 `StartPlay` 해소는 로비 중이라 지나갔고 `EndCharacterSelect` 는 영영 안 불려
      **"시작했는데 아무도 안 나타난다"** 가 된다 (테스트 ⑩ 이 이 절을 지킨다)
- [x] `UpdatePlayerStartSpot`·`ShouldSpawnAtStartSpot` 은 **한 줄도 안 건드렸다**

## ② 매치 HUD 페이즈 숨김

- [x] `UMatchWidget::ShouldShowMatchHUD(bLobby, bSelect)` — 둘 다 false 일 때만 보인다
- [x] **개별 자식(ItemPanel)이 아니라 위젯 루트를 접는다.** WBP 계층에서 아이템 텍스트가
      `ItemPanel` 바깥에 있으면 패널만 꺼서는 일부가 남는다 — 사용자가 본 증상이 그것이다.
      루트 `Collapsed` 는 계층과 무관하게 확실히 사라진다
- [x] ⚠️ **접힌 위젯은 Slate 가 Tick 을 부르지 않는다**(Tick 이 Paint 경로에 있고 Collapsed 는
      arrange 되지 않는다 — 5.8 엔진 소스 확인). 스스로 접으면 영영 못 편다 →
      되돌리는 구동자는 `ACA3DHUD::Tick`. 판정 공식은 순수 함수 하나뿐이라 두 벌이 아니다
- [x] 캔버스 폴백도 페이즈 중 매치 스탯 줄을 그리지 않는다(같은 함수 통과)
- [x] 결과 화면은 그대로 — 매치 종료 시점엔 두 플래그가 모두 false 라 별도 분기가 없다

## 병합에서 잡은 것 (서브에이전트 2대 병렬)

- [x] **콘솔 명령 중복 등록** — 양쪽이 각자 `ca3d.Ready`/`ca3d.StartMatch` 를 등록했다.
      선례(`ca3d.SelectCharacter` 가 `UI/CharacterSelectWidget.cpp`)를 따라 **위젯 쪽을 남기고**
      PlayerController 쪽을 제거 + 재발 방지 주석. 두 곳에 두면 런타임 중복 등록 경고
- [x] 커서/입력 모드는 Framework 쪽이 이미 "로비 ∪ 선택"을 **하나의 상태**로 확장해 뒀다 —
      로비→선택 전환에서 커서가 깜빡이지 않는다(전이가 아니라 연속)

## 검증 (2026-08-16)

- [x] 프로젝트 파일 재생성 + 두 타깃 빌드 `Result: Succeeded`
- [x] **전체 35스위트 실패 0, exit 0** (오케스트레이터 직접 재실행) — 신규 `Framework.Lobby`(10절) ·
      `UI.Lobby` · `UI.MatchWidget` ⑦ · `PlayerState` 복제 등록 6→8 갱신
- [x] `-game` 실전(봇 4, 70초): `bUseLobby=false` **기존 경로 무회귀** — 선택 페이즈 → 매치 시작,
      `LogCA3D` Error 0, **콘솔 중복 등록 경고 0**
- [ ] ⚠️ **로비 실동작은 아직 검증 못 했다** — `DA_Rules_Default` 의 `bUseLobby` 가 꺼져 있어
      런타임 경로가 돌지 않는다(에셋은 사용자 소관). 켜고 재패키징 후 확인할 것

## 남은 검증 (미실행 — 체크 금지)

- [ ] 패키징 서버 + 클라 2: 첫 접속자가 방장, 둘째는 준비 버튼 · 전원 준비 전 시작 버튼 비활성 ·
      시작 → 캐릭터 선택 → 매치 (`mds/packaged-test.md`)
- [ ] 로비 중 마우스 커서로 버튼이 눌리는가 (WBP_Lobby 제작 후)
- [ ] 방장이 나갔을 때 승계가 화면에 보이는가 · 자동 시작되지 않는가
- [ ] 로비·선택 중 매치 HUD 가 완전히 사라지는가 (②의 실전 확인)

## 에디터에서 할 일

- [ ] **`DA_Rules_Default` → `Lobby` → `Use Lobby` 체크** — 이걸 켜야 로비가 돈다 (기본 꺼짐)
- [ ] **`WBP_Lobby` 제작** (선택 — 없어도 캔버스 폴백 + 콘솔로 동작). 부모 `LobbyWidget`,
      **로직 금지**(배치·스타일만). 📄 **계층·슬롯 배치값·함정은 `mds/Tasks/41-WBP_Lobby.md`**:

| 바인딩 이름 | 타입 | 역할 |
|---|---|---|
| `RosterText` | TextBlock | 참가자 목록 (C++ 가 개행 조립) |
| `StatusText` | TextBlock | 안내 문구 ("N/M 명 준비 완료 — …") |
| `ReadyButton` | Button | 비방장 전용 준비/해제 |
| `ReadyButtonText` | TextBlock | 준비 버튼 라벨 (보통 `ReadyButton` 의 자식) |
| `StartButton` | Button | 방장 전용 시작 (전원 준비 전에는 비활성) |

- [ ] `BP_CA3DHUD` 의 `Lobby Widget Class` 에 `WBP_Lobby` 지정
- [ ] 켠 뒤 **재패키징** (`mds/build.md`)
- 개발용 콘솔(WBP 없이 검증): `ca3d.Ready`(인자 없으면 토글) · `ca3d.StartMatch`
