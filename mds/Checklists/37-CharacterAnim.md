# Checklist 37 — 선택 캐릭터 외형 적용 + C++ AnimInstance 베이스

> 2026-08-14 사용자 확정: 캐릭터 8종은 `Content/MonsterForSurvivalGame` **Polyart** 버전 ·
> 애니메이션 범위는 **기본 상태 전부**(Idle·이동·점프/낙하·사망·갇힘). Victory 연출은 범위 밖(후속).
> 회귀 스위트: `CrazyArcade3D.Gameplay.CharacterAppearance`

## 설계 축

- [x] **AnimBP 에는 상태 머신만** (BP 로직 금지) — 판정·계산은 전부 `UCA3DAnimInstance`(C++)가 끝내고
      AnimBP 는 완성된 값 5개를 읽기만 한다. 값 가공은 static 순수 함수 4개로 분리(테스트 대상 —
      MatchWidget 관례)
- [x] 노출 값: `Speed`(수평, Z 제외) · `bMoving` · `bInAir`(CMC IsFalling) · `bTrapped` · `bDead`
      — 원본은 `UStatusComponent::LifeState`·CMC. 전부 Transient + BlueprintReadOnly
- [x] `bMoving` 임계 = `KINDA_SMALL_NUMBER` — CMC 가 제동 시 속도를 0 으로 스냅하므로 부동소수
      잔여만 거르면 된다. 게임적 임계값을 지어내면 근거 없는 매직 넘버가 된다
- [x] `bDead` = `Dead || Spectating` — StatusComponent 의 기존 판정 묶음과 일치

## 외형 적용 (`ACA3DCharacter::ApplyCharacterAppearance`)

- [x] **적용 시점은 틱 폴링 + 스냅샷 비교**(`AppliedCharacterIndex`) — OnRep 체인을 잡지 않는 이유:
      폰·PlayerState·GameState(Rules) **3액터의 복제 도착 순서가 비보장**이라 어느 하나의 OnRep 에
      걸면 나머지가 아직 없는 프레임이 반드시 생긴다. 미도착이면 다음 틱 재시도
- [x] 호출은 Tick 의 기존 `HasAuthority()` 가드 **앞** — 외형은 클라에서도 적용돼야 한다.
      함수 최상단은 `IsRunningDedicatedServer()` 만 거른다(리슨·스탠드얼론 포함)
- [x] **진짜 시각 전용이라 데디 가드가 맞다** — 컬리전은 캡슐 담당, 메시는 판정에 안 쓰인다.
      HISM(지형의 유일한 컬리전이라 데디 가드 금지) 함정과 **다른 이유**를 주석으로 명시
- [x] 메시 트랜스폼은 **대체(비가산)**: 기준 = 캡슐 바닥(−HalfHeight) + `MeshOffset`,
      yaw `MeshYawOffset`(-90 표준), 스케일 `MeshScale`. 더하기면 재선택 시 누적돼 멱등이 깨진다.
      적용 순서 메시 → 애님 (SetSkeletalMesh 가 AnimInstance 를 재초기화)
- [x] 스킵한 인덱스(범위 밖·Mesh 미지정)도 스냅샷에 기록 — 룰셋은 매치 중 불변이라 재시도 무의미,
      기록해야 Verbose 로그가 1회로 끝난다

## 검증 (2026-08-14)

- [x] 프로젝트 파일 재생성 + 두 타깃 빌드 `Result: Succeeded`
- [x] `CrazyArcade3D.Gameplay.Character`(기존) · `CrazyArcade3D.Gameplay.CharacterAppearance`(신규) 통과
      — 순수 함수 매핑 · 인스턴스 갱신 경로 · 정상 적용(메시·위치·yaw·스케일) · 같은 인덱스 재적용
      없음 · 범위 밖/미지정 안전 · PlayerState 미도착 무동작
- [x] 전체 회귀는 오케스트레이터 최종 실행 (tasks.md 참조)

## 남은 검증 (미실행 — 체크 금지)

- [ ] **데디 가드 자동화 불가** — 에디터 프로세스는 `IsRunningDedicatedServer()` 가 항상 false
      (CLAUDE.md 함정 표의 기존 한계와 동일). 진짜 서버 exe 에서 메시 미적용 확인 필요
- [ ] PIE/`-game`: 8종 각각 메시·애님 적용, 이동/점프/갇힘/사망 상태 전이 체감
- [ ] Polyart 릭의 원점·정면 축 확인 — 기본값(Offset 0 / Yaw -90 / Scale 1)이 맞는지 에셋별 확인

## 에디터에서 할 일

- [ ] **AnimBP 8종 제작** — 각 몬스터 스켈레톤 대상, **Parent Class = `CA3DAnimInstance`**, 내용은
      상태 머신만. 애님 에셋 매핑: Idle=`IdleNormal` · 이동=`WalkFWD`/`Run`(Speed 블렌드) ·
      공중=적당한 포즈(점프 애님 없음 — Run 유지도 가능) · 갇힘=`Dizzy` · 사망=`Die`
      | 변수 | 권장 전이 |
      |---|---|
      | `bDead` | 최우선 — 아무 상태 → Death (사망 직후 액터가 숨겨지므로 짧아도 된다) |
      | `bTrapped` | 그다음 — 아무 상태 → Trapped(Dizzy). 갇힘 중 미세 이동은 Speed 무시 권장 |
      | `bInAir` | 지상 ↔ 공중 |
      | `bMoving` / `Speed` | Idle ↔ 이동, 블렌드스페이스 입력 |
- [ ] `DA_Rules_Default` 의 `Characters[i]` 에 Polyart 메시(`…/Mesh/Polyart/*_SK`) + 제작한 AnimBP 지정
      (Checklist 36 의 로스터 항목과 같은 작업 — 순서가 곧 선택 UI 버튼 순서)
