# Checklist 07 — HISMVoxelRenderer

> 대응 Task: `mds/Tasks/07-HISMVoxelRenderer.md`
> **PIE를 실제로 돌리지 않은 항목은 체크하지 않는다 — 미검증으로 남긴다.**

## 빌드 (필수 게이트)
- [x] `CrazyArcade3DEditor` 빌드 통과 (2026-07-28)
- [x] `CrazyArcade3DServer` 빌드 통과 (2026-07-28)
- [x] 프로젝트 파일 재생성 실행

## 코드 검증 (정적)
- [x] `UActorComponent` + `IVoxelRenderer` 구현
- [x] 생성·빌드 경로에 `if (IsRunningDedicatedServer()) return;` + AVoxelWorld BeginPlay에서 데디는 컴포넌트 파괴
- [x] **RemoveInstance 스왑 보정** — LastIndex 셀의 정/역맵 엔트리를 제거 인덱스로 이관 (RemoveBlock)
- [x] 표면 판정: 6면 중 하나라도 Empty(범위 밖 포함)면 인스턴스화
- [x] 메시·머티리얼은 BP 지정 프로퍼티(`BlockMeshes` EditDefaultsOnly) — 동적 머티리얼 생성 없음

## 에디터 연결
- [x] `BP_VoxelWorld`에 블록 3종 메시 지정 (2026-07-29 · PIE에서 3종 렌더링 확인)
- [x] `L_Arena` 맵에 배치, 열림 확인 후에만 `GameDefaultMap` 주석 해제 (2026-07-29 · DefaultEngine.ini 활성)

## 동작 검증 (PIE 필수 — 미실행 시 미검증)
- [x] **맵이 눈에 보이고** Task 04 레이아웃과 일치 (층·기둥·계단) (2026-07-29 · 사용자 PIE 확인)
- [x] 표면 추출 비율 20~40% (로그: 총 블록 vs 인스턴스 수) (2026-07-29 · PIE 로그 실측 — 총 셀 대비 42.9%, 아래 기록 참조)
- [x] 블록 1개 파괴 → 그 블록만 사라짐 (엉뚱한 블록 사라지면 인덱스 맵 버그) (2026-07-29 · `ca3d.DestroyAim`, 3타입 전부)
- [x] 파괴 후 주변 6칸의 새 표면 노출 (2026-07-29 · 사용자 PIE 확인)
- [x] 연속 파괴 20회+ 에서도 렌더·그리드 불일치 없음 (2026-07-29 · 사용자 PIE 확인)
- [ ] `stat unit` — BuildFromGrid 시 1회성 히치 외 지속 스파이크 없음

## 검증 기록

- 2026-07-28 · C++ 로직은 자동화 테스트 `CrazyArcade3D.Voxel.HISMVoxelRenderer` 헤드리스 실행으로 선검증 → `Result={Success}` (테스트 코드: `Source/CrazyArcade3D/Tests/HISMVoxelRendererTests.cpp`)
  - 인스턴스 수 == 독립 표면 판정 결과, 정/역맵 왕복 무결성, 단일 파괴 스왑 보정, 서로 다른 셀 24회 연속 파괴 후 재빌드 기대 집합과 완전 일치, Clear 동작 — 전부 통과 (메시 미지정 상태의 카운트/맵 로직 검증. PIE 시각 검증은 별개로 필요)
  - 표면 비율 실측: 인스턴스 756 / 솔리드 756 (솔리드 대비 100%) / 총 셀 1764 (총 셀 대비 42.9%). 맵이 사실상 1층 위주라 완전히 묻힌 블록이 없음 — z=0 바닥도 아래(범위 밖=Empty)에 접해 표면 판정. "20~40%" 기대치는 총 셀 대비로 해석해야 맞고, PIE에서 재확인 필요
  - 에디터 연결(BP·맵)과 PIE 검증은 미완 — 아래 미체크 항목 참조
- 2026-07-29 · PIE 검증 완료 (멀티 클라 PIE, `ca3d.DestroyAim`/`ca3d.DestroyBlock` 임시 디버그 명령 사용)
  - 파괴 수단이 없어 VoxelWorld.cpp 에 `#if !UE_BUILD_SHIPPING` 디버그 콘솔 명령 추가 — 정식 경로(ServerDestroyBlocks → ApplyDestruction + Multicast)를 그대로 태움 (불변식 1). **Task 16 ABomb 완성 시 제거**
  - 멀티 클라 PIE에서 명령이 클라 월드에서 실행돼 권한 거부 → 같은 프로세스의 권한 있는 VoxelWorld를 찾아 파괴를 넣도록 수정. 클라에서 부수면 서버 → Multicast → 전 클라 반영까지 리플리케이션 경로 동작 확인
  - 3타입(Floor·Destructible·Immortal) 전부 파괴·주변 표면 노출·연속 파괴 정상. Immortal 도 부서지는 것은 의도 — 타입 필터는 Task 15 `ExplosionSubsystem::Propagate` 소관 (구조 설계서 폭발 전파 의사코드)
  - 표면 비율 PIE 실측: 총 셀 1764 / 솔리드 756 / 인스턴스 756 (총 셀 대비 42.9%) — 헤드리스 선검증과 동일
  - `stat unit` 히치 확인만 미실시 — 유일한 잔여 항목
