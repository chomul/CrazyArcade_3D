# Checklist 38 — 폭탄 설치 Attack 몽타주 + 사망 지연 숨김

> 2026-08-14 사용자 요청: ① 폭탄 놓을 때 Attack 애니메이션 재생(몽타주 방식 확정) ·
> ② 사망 시 즉시 사라지는 문제 — **죽으면 바로 충돌만 끄고, 사망 애니메이션이 끝난 뒤 숨긴다.**
> 회귀: 기존 `CrazyArcade3D.Gameplay.Bomb`·`DeathHandling`·`Framework.MatchLeave` 스위트 확장 (신규 스위트 없음)

## 사망 지연 숨김

- [x] `ApplyDeathState()` 는 그대로 **즉시** 캡슐 NoCollision + MOVE_None (판정상 즉사 —
      시체가 길을 막지 않는 GDD 규칙 불변). 지연되는 것은 `SetActorHiddenInGame(true)` **하나뿐**
- [x] 숨김의 단일 지점 `HideAfterDeath()` 분리 — 즉시(딜레이 0 이하)든 타이머든 같은 함수.
      지연 구간 동안 AnimBP `bDead` 상태가 Die 애님을 재생한다 (Task 37 의 상태 머신이 이제 보인다)
- [x] **`DeathHideDelay` 는 룰셋** (기본 2.0초, Life 카테고리) — Die 애님 길이가 캐릭터마다 달라
      코드 상수로 못 박지 않는다. 0 이하 = 즉시 숨김(기존 동작으로 되돌리는 손잡이).
      룰셋 미도착이어도 기다리지 않는다 — 시각 전용이라 CDO 폴백으로 충분 (`ResolveVisualRules` 관례)
- [x] 서버(ServerKill)·클라(OnRep_Life)가 같은 `ApplyDeathState` 를 타는 구조는 그대로 —
      각 머신이 자기 타이머로 자기 화면만 숨긴다. 데디는 기존 가드에서 걸러짐
- [x] `EndPlay` 에서 타이머 명시 정리 (StatusComponent 의 TrappedTimer 관례)

## 폭탄 설치 Attack 몽타주

- [x] **몽타주가 맞는 이유**: 설치는 순간 이벤트지 상태가 아니다 — 상태 머신에 넣으면
      "언제 나가는가" 를 직접 설계해야 하고, 몽타주는 재생 후 알아서 상태 머신 포즈로 복귀한다
- [x] 에셋은 `FCA3DCharacterDef::AttackMontage` (룰셋 캐릭터 정의) — 캐릭터마다 스켈레톤이 달라
      Mesh·AnimClass 와 같은 자리. **미지정이면 재생만 생략** (설치 동작은 정상 — 외형 스킵 관례)
- [x] 확정 신호는 **복제 카운터** `BombPlaceCounter`(uint8) — `ServerPlaceBomb` 의
      **스폰 성공 지점에서만 ++** (거부 4경로 불변 — 거부인데 올리면 관전자 화면에서 헛스윙).
      클라는 틱 폴링 + 스냅샷 비교로 감지 (외형 적용과 같은 "복제 도착 순서 비보장" 관례).
      랩어라운드 무해 — 값이 아니라 변화가 신호
- [x] 판정은 static 순수 함수 `ShouldPlayFromCounter` — 전 분기 테스트 대상:
      변화 없음 / **첫 관측(INDEX_NONE 센티널) = 동기화만**(늦은 접속자 화면에서 과거 설치분에
      일제히 헛스윙하는 것을 막는다) / **로컬 조종 + 비권한 = 동기화만**(예측이 이미 재생) /
      그 외(리슨 호스트 본인·봇·원격 시뮬레이티드 프록시) = 재생
- [x] 원격 클라 본인은 `TryPlaceBombPredicted` 의 예측 성공 직후 **즉시 재생** — 설치음과 같은
      근거(왕복 지연 은폐). 서버가 거부해도 "헛스윙"일 뿐 되돌릴 상태가 없다 (불변식 3)
- [x] 재생의 단일 경로 `PlayAttackMontage()` — 예측과 복제 관측이 같은 함수를 탄다

## 검증 (2026-08-14)

- [x] 두 타깃 빌드 `Result: Succeeded` (Editor·Server) — 서브에이전트 + 오케스트레이터 재확인
- [x] **전체 33스위트 실패 0, exit 0** (오케스트레이터 직접 재실행)
- [x] `Gameplay.Bomb` 확장: 카운터 성공 시 1 증가·거부 3연속 + 사망 거부 불변 ·
      `ShouldPlayFromCounter` 전 분기 7건 · `BombPlaceCounter` CPF_Net 등록 고정
- [x] `Gameplay.DeathHandling` 갱신: 사망 직후 **안 숨음** + NoCollision + 타이머 활성 →
      `HideAfterDeath` 후 숨음 · 딜레이 0 이하 = 즉시 숨김 + 타이머 없음 (CDO 잠시 변경·복원)
- [x] `Framework.MatchLeave` ①: "즉시 숨김" 단언을 "즉시 NoCollision" 으로 교체 (경로 통과 증거)

## 남은 검증 (미실행 — 체크 금지)

- [ ] PIE/`-game`: 사망 시 Die 애님이 2초 보인 뒤 사라지는지 · 설치 시 몽타주가 이동 복귀와
      자연스럽게 블렌드되는지 (블렌드 아웃 0.2초 권장)
- [ ] 리슨/데디 + 클라: 남이 설치할 때 관전자 화면에서 몽타주가 보이는지 · 중간 접속자가
      입장 직후 헛스윙을 보지 않는지
- ~~`DeathHideDelay = 2.0` 이 8종 Die 애님 길이와 맞는지~~ → **Task 39 에서 구조로 해결**:
      캐릭터별 DeathMontage 실측 길이가 1순위, 이 값은 폴백으로 강등 (Checklist 39)

## 에디터에서 할 일

- [ ] **AttackMontage 8종 제작** — 각 Attack 애님(`Animation/Polyart/…`) 우클릭 → Create →
      AnimMontage: Beholder `Attack01` · Cactus `Attack01` · ChestMonster `Attack01` ·
      Mushroom `Attack01Smile` · Slime `Attack01` · Swarm08/09 `Attack` · TurtleShell `Attack01`
- [ ] **각 AnimBP 의 AnimGraph 에 Slot 노드(DefaultSlot)** — 스테이트 머신과 Output Pose 사이.
      이게 없으면 몽타주를 재생해도 화면에 안 나온다
- [ ] `DA_Rules_Default`: `Characters[i] → Attack Montage` 8종 지정 · `Life → Death Hide Delay`
      확인(기본 2초, 취향 조정)
