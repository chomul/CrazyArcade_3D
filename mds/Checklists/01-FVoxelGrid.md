# Checklist 01 — FVoxelGrid

> 대응 Task: `mds/Tasks/01-FVoxelGrid.md`
> **실제로 실행(자동화 테스트/PIE)하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] `.h/.cpp` 추가 후 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] `FVoxelGrid`는 UObject가 아니다 (UCLASS/UPROPERTY 없음)
- [ ] `Voxel/` 폴더가 `Gameplay`·`Framework`·`Core`를 include하지 않는다
- [ ] 좌표 변환(FVector 관련)이 이 struct에 없다
- [ ] std 컨테이너 미사용, `TArray<uint8>` 사용

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [ ] `Init` 후 전 칸이 `Empty`
- [ ] `Set` → `Get` 왕복 일치 (블록 3종 각각)
- [ ] **범위 밖 `Get`이 `Empty` 반환** (음수·초과 좌표 모두)
- [ ] 범위 밖 `Set`이 크래시 없이 무시됨
- [ ] `Index` ↔ 좌표 왕복 무결성 (모서리 셀 포함)
- [ ] `IsSolid` / `BlocksExplosion`이 블록 종류별로 명세와 일치
