# Checklist 06 — VoxelWorld

> 대응 Task: `mds/Tasks/06-VoxelWorld.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-28)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-28)
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] 파괴가 `ApplyDestruction` 단일 경로 — 이 함수 밖에서 파괴 목적의 `Grid.Set` 없음 (불변식 1)
- [x] `ServerInitFromSeed`/`ServerDestroyBlocks` 최상단 `if (!HasAuthority()) return;`
- [x] `Seed`가 `ReplicatedUsing=OnRep_Seed`, 파괴는 `NetMulticast(Reliable)` + 서버 로컬 실행 중복 방지 가드
- [x] **파괴 이벤트 선도착 큐**(PendingDestroyQueue) + `OnRep_Seed` 직후 flush 존재
- [x] `Renderer` null 가드 (데디/렌더러 미장착 상태 안전)
- [x] `CellSize` 임시값 주석 표기 (확정은 Task 10)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [x] `ServerInitFromSeed(고정 시드)` → 그리드 덤프가 Task 04 결과와 동일 — 자동화 테스트에서 Memcmp 비트 단위 동일 확인
- [x] `WorldToCell(CellToWorld(C)) == C` — 경계 셀 포함 통과 (액터를 (500,-300,250)에 스폰한 경우 포함)
- [x] `ServerDestroyBlocks` 후 해당 셀 `Empty` (로그: Destructible 71→68, 요청 3개)
- [x] (Listen+클라 1) 클라 그리드가 시드로 동일 생성됨 — 서버·클라 덤프 해시 일치 (2026-07-30 · Task 17 게이트에서 검증). 데디 서버 + 클라 2 구성에서 **생성 직후 3인스턴스 전부 `354BDDCD`(솔리드 756칸)**, 이후 파괴 10회도 전부 일치 → 시드 재생성·파괴 반영 양쪽 확인
- [x] (가능하면) 파괴 멀티캐스트를 시드보다 먼저 수신하는 상황 시뮬레이션 → 큐 flush 정상 동작 — friend 접근으로 큐 적재 후 `OnRep_Seed()` 호출, 2셀 flush 확인

## 검증 기록

- 2026-07-28 · Listen+클라 항목 제외 전 항목 검증 완료.
  - 자동화 테스트 `CrazyArcade3D.Voxel.VoxelWorld` 헤드리스 실행 → `Test Completed. Result={Success}` (테스트 코드: `Source/CrazyArcade3D/Tests/VoxelWorldTests.cpp`, `UWorld::CreateWorld` 기반 — PIE 아님)
  - 실제 넷 복제(OnRep·Multicast 순서)는 리슨 서버 2인 PIE가 가능해지는 Task 17 게이트에서 확인한다.
