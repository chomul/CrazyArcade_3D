# Checklist 12 — StatusComponent

> 대응 Task: `mds/Tasks/12-StatusComponent.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-29)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-29)
- [x] 프로젝트 파일 재생성 실행 (2026-07-29)

## 코드 검증 (정적)
- [x] 모든 `Server*` 함수 최상단 권한 가드
- [x] 스탯 5종 복제 지정이 설계서 2.5와 일치, `ActiveBombCount`는 비복제
- [x] `OnRep_Life` 시각 처리에 데디 가드
- [x] 갇힘 시간·Cap·이동속도 전부 룰셋 참조 (매직 넘버 없음 — RollerSpeedStep·MoveSpeedMulCap 신설, 전부 임시값)
- [x] `ServerApplyItem`이 Cap으로 클램프

## 자동화 테스트 (헤드리스 — PIE 복제 검증과 별개)
- [x] `CrazyArcade3D.Gameplay.StatusComponent` 통과 (2026-07-29) — 초기값 / 비권한 호출 불변(권한 가드) / Cap 클램프 3종 / Roller→MaxWalkSpeed 단일 경로 / Trap→TrappedMoveSpeed·타이머 / Escape(니들 소모·복원) / 니들 없이 Escape 무시 / 타이머 만료→Dead(Water) / Dead 이후 전이·스탯 무시 / KillZ Tick→Dead(Fall)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] (Listen+클라 1) 서버 `ServerApplyItem(Balloon)` → 클라 `MaxBombCount` 복제
- [ ] `ServerTrap` → `TrappedDuration` 후 자동 `Dead`
- [ ] Trapped 중 이동 속도가 `TrappedMoveSpeed`로 제한
- [ ] Trapped + 니들 → `ServerEscape` → `Alive` 복귀, 니들 소모
- [ ] 니들 없이 `ServerEscape` → 무시됨
- [ ] 낙사 → `ServerKill(Fall)` → `Dead` 복제
- [ ] 클라에서 Server* 직접 호출 → 상태 불변 (권한 가드 실증)
- [ ] Dead 이후 상태 전이 없음 (Trap/Escape 무시)
