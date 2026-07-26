# Checklist 12 — StatusComponent

> 대응 Task: `mds/Tasks/12-StatusComponent.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 모든 `Server*` 함수 최상단 권한 가드
- [ ] 스탯 5종 복제 지정이 설계서 2.5와 일치, `ActiveBombCount`는 비복제
- [ ] `OnRep_Life` 시각 처리에 데디 가드
- [ ] 갇힘 시간·Cap·이동속도 전부 룰셋 참조 (매직 넘버 없음)
- [ ] `ServerApplyItem`이 Cap으로 클램프

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] (Listen+클라 1) 서버 `ServerApplyItem(Balloon)` → 클라 `MaxBombCount` 복제
- [ ] `ServerTrap` → `TrappedDuration` 후 자동 `Dead`
- [ ] Trapped 중 이동 속도가 `TrappedMoveSpeed`로 제한
- [ ] Trapped + 니들 → `ServerEscape` → `Alive` 복귀, 니들 소모
- [ ] 니들 없이 `ServerEscape` → 무시됨
- [ ] 낙사 → `ServerKill(Fall)` → `Dead` 복제
- [ ] 클라에서 Server* 직접 호출 → 상태 불변 (권한 가드 실증)
- [ ] Dead 이후 상태 전이 없음 (Trap/Escape 무시)
