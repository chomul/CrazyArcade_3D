# Checklist 06 — VoxelWorld

> 대응 Task: `mds/Tasks/06-VoxelWorld.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [ ] `CrazyArcade3DEditor` 빌드 통과
- [ ] `CrazyArcade3DServer` 빌드 통과
- [ ] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [ ] 파괴가 `ApplyDestruction` 단일 경로 — 이 함수 밖에서 파괴 목적의 `Grid.Set` 없음 (불변식 1)
- [ ] `ServerInitFromSeed`/`ServerDestroyBlocks` 최상단 `if (!HasAuthority()) return;`
- [ ] `Seed`가 `ReplicatedUsing=OnRep_Seed`, 파괴는 `NetMulticast(Reliable)`
- [ ] **파괴 이벤트 선도착 큐**(PendingDestroyQueue) + `OnRep_Seed` 직후 flush 존재
- [ ] `Renderer` null 가드 (데디/렌더러 미장착 상태 안전)
- [ ] `CellSize` 임시값 주석 표기 (확정은 Task 10)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [ ] `ServerInitFromSeed(고정 시드)` → 그리드 덤프가 Task 04 결과와 동일
- [ ] `WorldToCell(CellToWorld(C)) == C` — 경계 셀 포함 통과
- [ ] `ServerDestroyBlocks` 후 해당 셀 `Empty` (로그)
- [ ] (Listen+클라 1) 클라 그리드가 시드로 동일 생성됨 — 서버·클라 덤프 해시 일치
- [ ] (가능하면) 파괴 멀티캐스트를 시드보다 먼저 수신하는 상황 시뮬레이션 → 큐 flush 정상 동작
