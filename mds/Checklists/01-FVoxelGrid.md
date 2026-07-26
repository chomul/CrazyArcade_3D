# Checklist 01 — FVoxelGrid

> 대응 Task: `mds/Tasks/01-FVoxelGrid.md`
> **실제로 실행(자동화 테스트/PIE)하지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과
- [x] `CrazyArcade3DServer` 빌드 통과
- [x] `.h/.cpp` 추가 후 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] `FVoxelGrid`는 UObject가 아니다 (UCLASS/UPROPERTY 없음)
- [x] `Voxel/` 폴더가 `Gameplay`·`Framework`·`Core`를 include하지 않는다 (CoreMinimal + VoxelTypes 뿐)
- [x] 좌표 변환(FVector 관련)이 이 struct에 없다
- [x] std 컨테이너 미사용, `TArray<uint8>` 사용

## 동작 검증 (실행 필수 — 미실행 시 미검증)
- [x] `Init` 후 전 칸이 `Empty`
- [x] `Set` → `Get` 왕복 일치 (블록 3종 각각)
- [x] **범위 밖 `Get`이 `Empty` 반환** (음수·초과 좌표 모두)
- [x] 범위 밖 `Set`이 크래시 없이 무시됨
- [x] `Index` ↔ 좌표 왕복 무결성 (모서리 셀 포함)
- [x] `IsSolid` / `BlocksExplosion`이 블록 종류별로 명세와 일치

## 검증 기록

- 2026-07-27 · 전 항목 검증 완료.
  - 빌드: Editor 58초 / Server 32분(첫 엔진 서버 모듈 포함) — 둘 다 `Result: Succeeded`
  - 동작 6항목: 자동화 테스트 `CrazyArcade3D.Voxel.VoxelGrid` 헤드리스 실행 → `Test Completed. Result={Success}` (테스트 코드: `Source/CrazyArcade3D/Tests/VoxelGridTests.cpp`)
  - 정적 4항목: 소스 grep으로 include·std·FVector 부재 확인
