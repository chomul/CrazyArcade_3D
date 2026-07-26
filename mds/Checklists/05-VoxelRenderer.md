# Checklist 05 — VoxelRenderer (인터페이스)

> 대응 Task: `mds/Tasks/05-VoxelRenderer.md`
> 순수 인터페이스 — PIE 검증 대상 없음. 컴파일·정적 검증만으로 완료 가능.

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `UVoxelRenderer`(UINTERFACE) + `IVoxelRenderer` 쌍 구조
- [ ] `BuildFromGrid` / `RemoveBlock` / `Clear` 3개 순수 가상 함수 — 설계서 2.3과 일치
- [ ] 파라미터가 `FVoxelGrid`·좌표뿐 — Gameplay/Framework 타입 없음
- [ ] `Voxel/` 폴더 의존 규칙 위반 없음
