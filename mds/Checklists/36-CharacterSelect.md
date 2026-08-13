# Checklist 36 — 게임 시작 전 캐릭터 선택 (선택 페이즈 코어)

> 2026-08-14 사용자 확정: 매치 시작 전 **약 10초** 캐릭터 선택 페이즈 · **중복 선택 불가(선착순)** ·
> 미선택자는 종료 시점에 **남은 것 중 랜덤 배정** · **봇도 남은 풀에서 배정** · 아트는 Polyart.
> 캐릭터 로스터: `Content/MonsterForSurvivalGame` 8종 (Beholder·Cactus·ChestMonster·Mushroom·Slime·Swarm08·Swarm09·TurtleShell) — 최대 8인과 정확히 맞는다.
> 회귀 스위트: `CrazyArcade3D.Framework.CharacterSelect`

## 설계 축

- [x] **선택 결과의 단일 출처는 `ACA3DPlayerState::CharacterIndex`** (복제, INDEX_NONE=미선택).
      점유 현황을 별도 상태(비트마스크 등)로 복제하지 않는다 — UI 는 `PlayerArray` 의 이 값을
      순회해 유도한다. 상태가 두 벌이면 "회색인데 선택되는 / 흰색인데 거부되는" 어긋남이 가능해진다
- [x] **배정의 단일 경로는 `ACA3DGameMode::TryAssignCharacter`** — 사람 RPC(`ServerSelectCharacter`)·
      종료 시 자동 배정·봇 배정·늦은 접속 배정이 전부 여기를 통과한다 (`RegisterParticipant` 와
      같은 근거: 경로가 갈라지면 "봇이 낀 매치에서만 중복이 나는" 진단 최악의 버그가 된다)
- [x] 페이즈 상태는 `ACA3DGameState` 복제 2필드(`bCharacterSelectActive`·`CharacterSelectEndServerTime`),
      갱신 주체는 GameMode 단독 (bSuddenDeathActive 관례)
- [x] **`CharacterSelectDuration` C++ 기본값은 0(=페이즈 스킵)** — 기존 30개 자동화 스위트와
      기존 실행 흐름의 무회귀가 근거다. 실제 10초는 `DA_Rules_Default` 에서 지정한다(에디터 작업).
      `Characters` 배열이 비어 있어도 스킵 + 경고 1회 — 로스터 없이 페이즈만 도는 상태를 막는다

## 매치 흐름 (Duration > 0 일 때만 — 분기점은 BeginPlay 한 곳)

- [x] BeginPlay(맵 판가름 성공 후): 페이즈 플래그·종료 시각 세팅 → `EndCharacterSelect` 타이머.
      **서든데스·봇 채우기 타이머는 여기서 예약하지 않는다** (매치 시작 기준이 페이즈 종료로 밀린다)
- [x] `MatchStartServerTime` 은 **예상 시작 시각(now+Duration)으로 선기록** — HUD 경과 타이머가
      페이즈 동안 음수를 그리지 않는다. `EndCharacterSelect` ③ 이 실제 시각으로 재기록
- [x] `EndCharacterSelect`: ① 미선택 사람 자동 배정 ② `SpawnFillBots` 즉시 + 봇도 같은 자동 배정
      경로 ③ MatchStartServerTime 재기록 ④ 플래그 해제 ⑤ `FlushPendingSpawns` ⑥ 서든데스 예약
- [x] **스폰 게이트를 연장** — 페이즈 중 입장자는 기존 `PendingSpawnControllers` 에 보류(별도 목록
      없음), StartPlay 의 flush 는 페이즈 중이면 건너뛴다. 32-SpawnGate 의 구조를 재사용
- [x] 자동 배정 랜덤은 `FMath::Rand` 가 아니라 **맵 시드 파생 `FRandomStream`(Seed×7919+1)** —
      고정 시드 모드에서 배정까지 재현된다 (테스트 ⑧이 이 재현성을 고정)

## 규칙의 경계

- [x] 재선택 허용(페이즈 중): 자기 값 덮어쓰기이므로 이전 선택은 자연 해제 — 해제 코드가 따로 없다
- [x] 페이즈 종료 후: **변경 요청(CharacterIndex != INDEX_NONE)은 거부**, 미배정자(늦은 접속)는
      자동 배정 — 같은 함수 안에서 `INDEX_NONE` 여부 하나로 가른다 (사람/자동 플래그 인자 없음)
- [x] 페이즈 없는 매치(`bCharacterSelectPhaseUsed == false`)에서는 `TryAssignCharacter` 전면 거부 —
      악성 클라 RPC 로도 복제 값이 안 바뀐다 (기존 흐름 무회귀)
- [x] 선택 중 마우스 커서: `ACA3DPlayerController` 가 GameState 플래그를 폴링해 **전이 시에만**
      GameAndUI ↔ GameOnly 전환 (HUD 폴링 관례)

## 선택 UI (`UI/CharacterSelectWidget` + `CA3DHUD` 확장 — 같은 날 함께)

- [x] MatchWidget 관례 그대로: `BindWidgetOptional`(`CharButton_0~7`·`CharName_0~7`·`CountdownText`) ·
      NativeTick 폴링 + 스냅샷 비교 · 표시 가공은 static 순수 함수(점유 비트마스크 유도·남은 초).
      회귀 스위트 `CrazyArcade3D.UI.CharacterSelect`
- [x] 점유 표시는 **PlayerArray 의 CharacterIndex 에서 유도** (위 단일 출처 원칙의 소비측) —
      복제 도착 순서로 한 프레임 겹쳐 보일 수 있어 중복 비트는 관대하게 겹친다
- [x] 카운트다운은 **올림(Ceil)** — 내림이면 마지막 1초 내내 "0초"가 떠 있다
- [x] 클릭 → `ACA3DPlayerController::ServerSelectCharacter` 공개 진입점 호출만 (HUD 는 RPC 를
      부르지 않는다는 원칙과의 관계를 주석으로 명시 — 입력 전달은 컨트롤러 소관)
- [x] **WBP 없이도 동작** — 캔버스 폴백(`ca3d.DebugHUD` 관례)에 페이즈·점유 현황 표시 +
      non-shipping 콘솔 명령 **`ca3d.SelectCharacter <1~8>`** 로 선택까지 가능

## 검증 (2026-08-14)

- [x] 두 타깃 빌드 `Result: Succeeded` (Editor·Server)
- [x] **전체 33스위트 실패 0** (신규 3: `Framework.CharacterSelect` — 중복 거부·재선택·자동 배정
      고유성·봇 포함 8인 고유·Duration 0 스킵·종료 후 거부·빈 로스터 안전·고정 시드 재현 /
      `Gameplay.CharacterAppearance`(체크리스트 37) / `UI.CharacterSelect`)
- [x] `CA3DPlayerStateTests` 복제 등록 개수 5 → 6 갱신 (`CharacterIndex` CPF_Net) — 이 갱신 없이는
      기존 스위트가 회귀로 잡힌다

## 남은 검증 (미실행 — 체크 금지)

- [ ] `-game` 실전: 페이즈 10초 동안 커서·카운트다운 → 종료 시 자동 배정 → 스폰 → 완주
- [ ] 리슨/데디 + 클라: 두 클라가 같은 캐릭터를 두고 경합할 때 선착순 한쪽만 성공
- [ ] 페이즈 중 중간 접속·페이즈 종료 직후 접속의 배정
- [ ] 10초가 체감상 적당한가 (룰셋 값이라 코드 수정 없이 조정)

## 에디터에서 할 일

- [ ] `DA_Rules_Default`: **`CharacterSelectDuration = 10`** 지정 (0 이면 페이즈가 아예 안 뜬다)
- [ ] `DA_Rules_Default`: `Characters` 배열 8종 채우기 — 각 항목에 DisplayName ·
      Polyart 스켈레탈 메시(`Content/MonsterForSurvivalGame/Mesh/Polyart/…`) · AnimBP(Task 37 이후) ·
      오프셋/스케일. 순서가 곧 선택 UI 버튼 순서다
- [ ] **`WBP_CharacterSelect` 제작** — 부모 클래스 `CharacterSelectWidget`, 로직 금지(배치·비주얼만).
      바인딩 이름(일치하면 자동 연결, 빠지면 경고 1줄): 버튼 `CharButton_0`~`CharButton_7` ·
      이름 라벨 `CharName_0`~`CharName_7`(계층 위치 자유 — 보통 각 버튼의 자식) · `CountdownText`.
      권장 계층: Canvas/Overlay → 상단 CountdownText, 중앙 UniformGrid(버튼 8개, 각 버튼 안에 라벨).
      클릭·활성/비활성·이름·색(내 선택=노란색)·Collapsed 는 전부 C++ 가 처리 — WBP 이벤트 금지
- [ ] `BP_CA3DHUD` 의 `UI > Character Select Widget Class` 에 `WBP_CharacterSelect` 지정
      (미지정이어도 캔버스 폴백 + `ca3d.SelectCharacter` 로 검증 가능)
