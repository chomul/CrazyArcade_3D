# Checklist 29 — 사망 후 관전 (생존자 추적 + 순환)

> 대응: `mds/tasks.md` 후속 항목 · Task 18 잔여 "관전 상태 전환 미구현" · 설계서 294행 `Spectating`
> **PIE 를 실제로 돌리지 않은 항목은 체크하지 않는다.**

## 확정된 규칙 (2026-08-06 사용자)
- [x] **생존자 추적 + 순환** — 죽으면 살아 있는 참가자 시점으로 붙고 좌/우로 다음 생존자로 전환
- [x] **자유 비행 카메라는 만들지 않는다** (범위 밖 — 필요해지면 별도 항목)

## 빌드·검증 (2026-08-06 직접 재실행 — 서브에이전트 보고와 별개로)
- [x] `CrazyArcade3DEditor` `Result: Succeeded`
- [x] `CrazyArcade3DServer` `Result: Succeeded`
- [x] **전체 24스위트 실패 0** (기존 23 + `Spectate`)
- [x] 회귀 위험 스위트 전부 통과 — `DeathHandling` · `StatusComponent` · `ItemPickup` ·
      `BombKick` · `MatchWidget` · `BotController`

## 구조 검증 (정적 — 직접 grep 으로 확인)
- [x] **폰을 건드리지 않는다** — `UnPossess`·`SpectatorPawn`·`Destroy()` 호출 **0건**.
      `SetViewTargetWithBlend` 로 시점만 옮긴다. 관전 폰 경로를 쓰면 부활 여지(CLAUDE.md
      "죽으면 이동 불가 + 부활 여지"), 기존 입력 바인딩, HUD 캐시가 전부 깨진다
- [x] **관전 대상은 복제하지 않는다** — `TObjectPtr<ACA3DPlayerState> SpectateTarget` 이
      `UPROPERTY()` 일 뿐 `Replicated` 아님. 시점은 로컬 표시이고, 복제하면 남의 관전 대상까지
      동기화되는 낭비다. "누가 살아 있는가"만 기존 복제값 `ACA3DPlayerState::bAlive` 에서 읽는다
- [x] 대상 목록은 `GameState->PlayerArray` **고정 순서** 훑기 (`TSet` 순회 없음) — 순환이 매번
      같은 순서로 돈다. 자기 자신 제외
- [x] 데디 가드 **7곳** — 시점 전환은 순수 시각이다
- [x] 대상 사망 시 자동 전환 / 생존자 0명이면 마지막 대상 유지 / **매치 종료 후 시점 고정**
- [x] 살아 있는 동안 좌/우는 이동 그대로 — 사망 중에만 관전으로 재해석 (컨트롤러에서 분기,
      캐릭터의 기존 생존 가드는 무수정)

## `ELifeState::Spectating` 을 추가하지 않은 근거 (되풀이 방지)
- [x] 관전은 **복제 상태가 아니라 로컬 시점**이다 — 서버는 관전 대상을 모르고 알 필요도 없다.
      상태를 추가하면 복제값이 하나 느는데 얻는 것이 없다
- [x] 비용은 확실하다: `== Dead` 를 읽는 곳이 **12군데**(`Move`·`DoJump` 가드, `ServerPlaceBomb`·
      `TryAcquirePredictedVisual` 의 Alive 검증, `ItemPickup::OnOverlap`, `ServerTryKickBomb`,
      킥의 시체 제외, `ExplosionSubsystem` 피격, `BotController` 2곳, `Tick` 낙사 가드)라
      전부 두 값 검사로 늘려야 하고 **하나만 빠뜨리면 "시체가 아이템을 먹는다" 류 회귀**가 난다
- [x] Task 27 문서의 결정("관전 전용 동작이나 부활이 들어와 죽음과 관전을 구분해야 할 때 도입")이
      그대로 유효 — 이번 관전은 폰·상태를 전혀 건드리지 않으므로 아직 구분할 이유가 없다

## ⚠️ 아래 "봇 pitch 덮어쓰기" 는 2026-08-09 폐기됐다
PIE 실측에서 **각도만 고쳐서는 부족**하다는 것이 드러났다 — yaw 가 봇의 이동 방향이라 45도 스냅이
깨진 채 계속 미끄러졌고, 원격 플레이어에서는 아예 다른 기전으로 카메라가 눕는다. 카메라 각의
소유를 폰(`ACA3DCharacter::GetViewRotation`)으로 옮기고 이 덮어쓰기는 제거했다.
**현행 규칙은 체크리스트 30 (`30-BombFall-SpectateCamera.md`).** 아래는 그때의 판단 기록으로 남긴다.

## ~~⚠️ 봇을 관전하면 카메라가 눕는다~~ (2026-08-06 — 폐기된 1차 수정)
- [x] 관전 카메라의 각도는 **대상 폰의 스프링암**이 정하고, 그 회전은 `bUsePawnControlRotation`
      → `APawn::GetViewRotation()` → **대상 컨트롤러의 `ControlRotation`** 이다.
      `AAIController::UpdateControlRotation` 은 "상대 폰을 보고 있지 않으면 pitch = 0" 으로
      눌러버리므로, 그대로 두면 **봇을 관전할 때 카메라가 지면에 눕는다** — 45도 내려보기
      아케이드에서 칸 감각이 통째로 무너진다
- [x] 수정: `ABotController::Tick` 의 `Super::Tick` **직후**에 yaw(봇이 향한 방향)는 살리고
      pitch 만 룰셋 `CameraPitchDeg` 로 덮어쓴다. 캐릭터가 `bUseControllerRotationYaw=false` 라
      `FaceRotation` 이 아무 일도 하지 않으므로 **봇의 이동·판단·외형에 영향 0**
- [x] 데디 가드를 걸지 **않는다** — 데디에서는 이 값이 복제되어 클라의 관전 카메라 각이 된다

## 룰셋 추가
| 이름 | 기본값 | 근거 |
|---|---|---|
| `SpectateBlendTime` | 0.4초 | 0 이면 8명을 넘길 때 화면이 순간이동해 어디를 보는지 잃고, 길면 정작 보려던 장면을 놓친다 |
| `SpectateCycleAxisThreshold` | 0.5 | 사망 중 기존 이동 축을 재해석하므로 눌림/뗌 판정이 필요. 절반보다 크게 잡아 대각 입력의 약한 축이 한 칸 더 넘기지 않게 |

## `-game` 실전 (2026-08-06 · 대형 맵, 봇 6, 220초 · 직접 실행)
- [x] 로컬 플레이어 사망 직후 `ACA3DPlayerController: 관전 대상 → Bot 1` 전환
- [x] **전환 로그가 1회뿐** — 매 프레임 스팸 없음 (필요한 순간에만 찍힌다)
- [x] 매치 종료(`결과 화면 표시 — 6명, 우승자 있음`) 이후 전환 로그 없음 = 시점 고정 확인
- [x] 같은 판에서 봇 2칸 낙하 4건 · 폭탄 83발 — 킥·봇 기능 회귀 없음
- [x] `LogCA3D` Error · ensure **0**

## 남은 검증 (미실행 — 체크 금지)
- [ ] **좌/우 순환을 사람이 직접** 눌러 확인 (봇만 있는 세션에서는 입력이 안 들어간다).
      자동화 테스트가 순환 순서·래치를 덮지만 손맛은 PIE 로만 본다
- [ ] (Listen+클라) 원격 클라에서 관전 카메라 각이 정상인지 — 봇 `ControlRotation` 복제 경로가
      실제로 도는지는 진짜 멀티에서만 보인다
- [ ] 진짜 데디 exe 에서 관전 경로가 서버에서 no-op 인지

## 에디터에서 할 일
**없음.** 새 `IA_*`·IMC·에셋을 만들지 않았다 — 기존 `IA_Move` 의 좌/우 축을 사망 중에만
재해석하고 같은 IA 에 `Completed` 바인딩 한 줄만 추가했다. 룰셋 두 값도 C++ 기본값이 있어
`DA_Rules_Default` 를 열 필요가 없다 (블렌드 시간만 취향대로 조절 가능).
