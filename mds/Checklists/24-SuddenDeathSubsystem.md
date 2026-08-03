# Checklist 24 — SuddenDeathSubsystem

> 대응 Task: `mds/Tasks/24-SuddenDeathSubsystem.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 확정된 수치 (2026-08-04 사용자)
- [x] **서든데스 낙하만 바닥(z=0)을 부순다** — 신규 `bSuddenDeathDestroysFloor=true`.
      기존 `bFloorDestructible=false` 는 유지되어 **일반 폭탄은 여전히 바닥을 못 부순다.**
      초반 150초는 발판이 보장되고, 서든데스가 시작돼야 구멍이 뚫려 낙사가 시작된다
- [x] `DropInterval=1.0` · `DropsPerWave=1` · `DropExplosionRange=2` (기존 `DropWarningTime=1.5` 유지)

## 빌드 (필수 게이트) — 2026-08-04 직접 실행
- [x] `CrazyArcade3DEditor` 빌드 통과 (`Result: Succeeded`, 번역 단위 병합 강제)
- [x] `CrazyArcade3DServer` 빌드 통과 (`Result: Succeeded`, 동일)
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] 낙하 지점·시각 결정이 서버 전용, 클라는 `MulticastWarnDrop` 수신만
      (`ServerStart`/`ServerStop`/`ProcessDrop`/`ExecuteWave` 4곳 전부 `NM_Client` 가드)
- [x] 파괴가 `ServerDestroyBlocks` 단일 경로 경유 (불변식 1) — 서든데스는 `FVoxelGrid` 를 직접 만지지 않는다
- [x] **`Propagate` 를 수정하지 않았다** (불변식 2). `bFloorDestructible` 이 이미 인자라
      서든데스는 거기에 `bSuddenDeathDestroysFloor` 를 넘기기만 한다
- [x] **폭발 적용부를 폭탄과 공유** — `UExplosionSubsystem::ApplyExplosionCells` 로 ②~⑤ 를 추출하고
      `ProcessChainStep`(폭탄 연쇄)과 `ServerApplyExplosionAt`(서든데스)이 같은 본체를 탄다.
      연쇄 스케줄링(단계 분산·`ChainStepDelay`·`bProcessingStep` 재진입 가드)은 이동하지 않았다
- [x] 마커 표시에 데디 가드 (2곳) + 풀(`UPoolSubsystem`) 사용
- [x] 시작 시각·예고 시간·외곽 가중 룰셋 참조, 신규 수치 5개도 룰셋에 추가됨 (매직 넘버 0)
- [x] 매치 종료 시 타이머 정리 (`ServerStop`) + `Deinitialize` 안전망.
      **예고만 하고 안 떨어진 웨이브까지 취소** — 결과 화면이 뜬 뒤 지형이 변하면 안 된다
- [x] `Voxel` 이 `Gameplay` 를 참조하지 않는다 — `VoxelWorld.h` 변경은 테스트 `friend` 선언 1줄뿐

## 동작 검증 — 자동화 (2026-08-04 · `CrazyArcade3D.Gameplay.SuddenDeath` 통과, **전체 18스위트 실패 0** 직접 실행)
- [x] **외곽 가중: 낙하 300회 로그** — 외곽 249회/360칸, 중앙 51회/81칸, 칸당 비율 **1.099 : 1**.
      300회는 표본이 작아 흔들리므로 5000회를 함께 측정: **1.333 : 1** (이론 1.38 과 일치)
- [x] **룰셋 값이 실제로 소비된다** — 가중 1.0배 / 2.0배 / 4.0배에서 1.010 / 1.333 / 1.721 로 단조 증가
      (하드코딩이면 셋이 같아야 한다)
- [x] 같은 시드 ⇒ 같은 낙하 순서 (선정 함수가 순수함 — 정수 연산만, float 없음)
- [x] 이미 뚫린(전부 Empty) 기둥은 선택되지 않는다 (재추첨)
- [x] `bSuddenDeathDestroysFloor=true` ⇒ Floor 가 Broken 에 들어간다 / `false` ⇒ 안 들어간다
      (기존 폭탄 동작 회귀 방지)
- [x] 기존 폭탄 경로 회귀 없음 — `Bomb`·`ExplosionSubsystem`·`ItemPickup`·`PredictedBombVisual`·`DeathHandling` 전부 통과

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] `SuddenDeathStart`(테스트용 단축값) 도달 → 낙하 시작
- [ ] 예고 마커 → 정확히 그 셀 파괴 (마커≠낙하 셀이면 실패)
      · 코드상으로는 예고 시점에 셀을 확정 저장하고 만료 시 재추첨하지 않음을 자동화로 검증했다.
        **눈으로 볼 항목은 마커가 실제로 그 자리에 뜨는가**
- [ ] 예고→낙하 지연이 `DropWarningTime`과 일치, **보고 피할 수 있음** (체크리스트 필수 요건)
- [ ] 구멍 낙사 → `EDeathCause::SuddenDeath` (분기는 넣었으나 KillZ 진입은 PIE 에서만 발생)
- [ ] 장시간 진행 시 맵 자연 축소 → 매치 종결 (페이싱)
- [ ] (Listen+클라) 마커·파괴가 전 클라 동일
- [ ] **데디 서버 exe 에서 마커 경로가 실제로 스킵되는가** — PIE 로는 안 잡힌다
      (`IsRunningDedicatedServer()` 가 PIE 데디 모드에서 false. `mds/Checklists/dedi-server-windows.md`)

## 튜닝 항목 (사용자 확정 대기 — 임의로 바꾸지 않았다)
- **낙하의 약 21%가 부술 것 없는 Immortal 외벽 위에 떨어진다.** 외곽 가중이 최대인 자리가
  하필 폴백 맵의 Immortal 외벽 링(80칸)이다. 명세의 재추첨 조건은 "기둥이 전부 Empty" 뿐이라
  Immortal 기둥은 통과한다. 해소하려면 "top solid 가 Immortal 인 기둥도 재추첨" 조건 추가.
  격자 기둥(x,y 둘 다 짝수)도 같은 이유로 낭비에 가깝다
