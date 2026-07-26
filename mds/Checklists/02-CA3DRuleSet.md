# Checklist 02 — CA3DRuleSet

> 대응 Task: `mds/Tasks/02-CA3DRuleSet.md`
> **에디터에서 실제로 확인하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 부모가 `UPrimaryDataAsset`, `BlueprintType` 지정
- [ ] 설계서 2.6의 프로퍼티가 전부 있고 카테고리(Bomb/Life/Map/SuddenDeath)가 일치
- [ ] 로직(함수) 없음 — 순수 데이터
- [ ] 미확정 값(캡·무적·바닥 파괴)에 미확정 주석 표기

## 에디터 검증 (실행 필수 — 미실행 시 미검증)
- [ ] `Content/Data/DA_Rules_Default` 에셋 생성됨
- [ ] 에셋에서 값 수정·저장이 코드 수정 없이 가능
- [ ] 에디터 재시작 후 값 유지
