# Checklist 13 — PooledActor (인터페이스)

> 대응 Task: `mds/Tasks/13-PooledActor.md`
> 순수 인터페이스 — PIE 검증 대상 없음. 컴파일·정적 검증만으로 완료 가능.

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `UPooledActor`(UINTERFACE) + `IPooledActor` 쌍 구조
- [ ] `OnAcquiredFromPool` / `OnReleasedToPool` 2개 순수 가상 함수
- [ ] Release 시 타이머·FX 정지 의무가 주석으로 명시
- [ ] `Core/` 폴더가 아무것도 참조하지 않음
