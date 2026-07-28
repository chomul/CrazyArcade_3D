# Task · 진행 상황

구조 설계서(`mds/crazy-arcade-3d-architecture.md`) 5장의 순서를 따른다.
클래스 단위 상세 명세는 `mds/Tasks/00-INDEX.md`(1 task = 1 class, 26개),
검증 항목은 `mds/Checklists/` 의 같은 번호 파일을 따른다.
**작업을 시작·완료할 때 이 파일을 갱신한다. `CLAUDE.md` 는 건드리지 않는다.**
완료 형식: `- [x] **N. 항목** — 만든 것 / 검증 방법` (빌드 통과 후에만 체크)

## 1주차 — 로컬 플레이어블

번호는 `mds/Tasks/` 의 1 task = 1 class 체계 (구 5장 번호는 00-INDEX 참조).

- [x] **00. 프로젝트 생성 + 폴더 스캐폴딩** — 에디터·서버 타깃 모두 빌드 통과 (소스 엔진)
- [x] **01. `FVoxelGrid`** — VoxelTypes.h + VoxelGrid.h/.cpp + 자동화 테스트 6항목 통과 (`CrazyArcade3D.Voxel.VoxelGrid`), 두 타깃 빌드 통과
- [x] **02. `UCA3DRuleSet`** — Framework/CA3DRuleSet.h/.cpp (UPrimaryDataAsset, 튜닝 값 14종) / 두 타깃 빌드 통과. 에디터 작업(DA_Rules_Default 생성) 미완
- [x] **03. `IMapGenerator` + ItemTypes.h** — MapGen/MapGenerator.h (UINTERFACE 쌍, Generate 시그니처·결정론 계약 주석) + Gameplay/Item/ItemTypes.h (EItemType 5종, FItemPlacement) / 두 타깃 빌드 통과 (순수 인터페이스 — 컴파일 검증만)
- [x] **04. `UFallbackMapGenerator`** — MapGen/FallbackMapGenerator.h/.cpp (하드코딩 21×21×4 레이아웃: 외곽 2층 Immortal 벽·기둥 격자·Destructible 71개·계단 접근로 (8,9,1)→(9,9,1)→(10,9,1~2)·스폰 8개) + 자동화 테스트 `CrazyArcade3D.MapGen.FallbackMapGenerator` 통과 / 두 타깃 빌드 통과
- [x] **05. `IVoxelRenderer`** — Voxel/VoxelRenderer.h (UINTERFACE 쌍, BuildFromGrid/RemoveBlock/Clear) / 두 타깃 빌드 통과 (순수 인터페이스 — 컴파일 검증만)
- [x] **06. `AVoxelWorld`** — Voxel/VoxelWorld.h/.cpp (그리드 소유·Seed 복제·ApplyDestruction 단일 경로·선도착 파괴 큐 flush·좌표 변환, CellSize=100 임시) + 자동화 테스트 `CrazyArcade3D.Voxel.VoxelWorld` 통과 / 두 타깃 빌드 통과. 실제 넷 복제 검증은 Task 17 게이트로 이월
- [x] **07. `UHISMVoxelRenderer`** — Voxel/HISMVoxelRenderer.h/.cpp (타입별 HISM + 표면 추출 + RemoveInstance 스왑 보정) / 로직 테스트 통과 + 에디터 연결(BP_VoxelWorld·L_Arena) + PIE 검증 완료 — 임시 디버그 명령 `ca3d.DestroyAim`/`DestroyBlock` (Task 16에서 제거)으로 3타입 파괴·표면 노출·연속 파괴·멀티 클라 리플리케이션 확인. 잔여: `stat unit` 히치 확인만 (체크리스트 07)
- [ ] **08. `ACA3DGameState`**
- [ ] **09. `ACA3DGameMode`**
- [ ] **10. `ACA3DCharacter`** — 셀 크기·이동속도·점프 높이를 여기서 몸으로 결정
- [ ] **11. `ACA3DPlayerController`** — 카메라 입력 기준도 여기서 결정
- [ ] **12. `UStatusComponent`**
- [ ] **13. `IPooledActor`**
- [ ] **14. `UPoolSubsystem`**
- [ ] **15. `UExplosionSubsystem`** — Propagate 6방향·층간
- [ ] **16. `ABomb`** — 파괴→낙하·연쇄 분산·프리뷰 데칼 포함
- [ ] **17. `APredictedBombVisual`** — 🏁 Listen Server PIE 2인 게이트 포함

## 2주차 — 멀티 / 데디 서버

- [ ] 리눅스 크로스 컴파일 툴체인 확인 (`mds/build.md` 참조)

## 3주차 — 플레이어블 멀티 데모

_(미정)_

## 진행 중 메모

- ⭐ 1번 시점부터 `stat unit` 을 켜고 개발한다 (GDD 7.4). "다 만들고 최적화"는 3주 프로젝트에 없다.
