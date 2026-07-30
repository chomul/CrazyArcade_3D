# Task 인덱스 — 1 Task = 1 Class

> 근거 문서: `mds/crazy-arcade-3d-architecture.md`(우선) · `mds/crazy-arcade-3d-gdd-v2.md`
> 번호는 코드 나열 순서가 아니라 **에디터에서 실제로 만들고 연결하며 하나씩 검증해 나가는 순서**다.
> 앞 Task의 산출물이 다음 Task의 입력이 되도록 배열했다. 진행 체크는 `mds/tasks.md`에만 기록한다.

## 공통 검증 원칙 (모든 Task에 적용)

1. **빌드 두 타깃 통과가 완료의 최소 조건** — `CrazyArcade3DEditor`, `CrazyArcade3DServer` (명령은 `CLAUDE.md`).
2. `.h/.cpp` 추가·삭제 시 **프로젝트 파일 재생성**을 먼저 실행한다.
3. **폴더 의존 규칙 준수**: `Voxel→(없음)`, `Core→(없음)`, `MapGen→Voxel`, `Gameplay→Voxel,Core`, `Framework→전부`, `UI→Framework(읽기 전용)`.
4. **권한 분리**: 상태 변경 함수 최상단 `if (!HasAuthority()) return;`, 시각 전용 함수 최상단 `if (IsRunningDedicatedServer()) return;`.
5. 튜닝 값은 코드에 매직 넘버로 넣지 않고 `UCA3DRuleSet`에 노출한다.
6. 대응 체크리스트(`mds/Checklists/NN-*.md`)의 항목으로 검증한다. **PIE를 실제로 돌리지 않은 항목은 체크하지 않고 미검증으로 남긴다.**

## 공통 응답 원칙 (모든 Task에 적용)

1. 완료 보고에는 **두 타깃의 빌드 결과**를 명시한다. 실패했으면 "완료"라 하지 않고 실패 로그 원문을 보고한다.
2. PIE로 확인하지 않은 동작은 반드시 **"미검증"**이라고 명시한다.
3. **미결정 값**(셀 크기·이동속도·점프 높이·발밑 셀 정의·카메라 입력 기준·바닥 파괴·스폰 무적·스택 상한)은 임의로 정하지 말고 질문한다. 임시값을 쓸 땐 임시임을 명시한다.
4. 빌드 통과 후 `mds/tasks.md` 체크박스를 갱신하고 만든 것을 한 줄 남긴다.
5. 설계서 시그니처에서 벗어난 부분이 있으면 이유와 함께 보고한다.

## 1주차 — 로컬 코어

| # | Task | 클래스 | 산출물 검증 |
|---|---|---|---|
| 01 | [FVoxelGrid](01-FVoxelGrid.md) | `FVoxelGrid` (+`VoxelTypes.h`) | 그리드 로그 덤프 |
| 02 | [CA3DRuleSet](02-CA3DRuleSet.md) | `UCA3DRuleSet` | `DA_Rules_Default` 에셋 |
| 03 | [MapGenerator](03-MapGenerator.md) | `IMapGenerator` (+`ItemTypes.h`) | 인터페이스 컴파일 |
| 04 | [FallbackMapGenerator](04-FallbackMapGenerator.md) | `UFallbackMapGenerator` | 그리드 덤프·스폰 8개 |
| 05 | [VoxelRenderer](05-VoxelRenderer.md) | `IVoxelRenderer` | 인터페이스 컴파일 |
| 06 | [VoxelWorld](06-VoxelWorld.md) | `AVoxelWorld` | 시드→그리드 로그 |
| 07 | [HISMVoxelRenderer](07-HISMVoxelRenderer.md) | `UHISMVoxelRenderer` | **맵이 눈에 보임**, 표면 비율 20~40% |
| 08 | [CA3DGameState](08-CA3DGameState.md) | `ACA3DGameState` | 클라에서 룰셋 포인터 유효 |
| 09 | [CA3DGameMode](09-CA3DGameMode.md) | `ACA3DGameMode` | 시드 생성→월드 초기화·스폰 |
| 10 | [CA3DCharacter](10-CA3DCharacter.md) | `ACA3DCharacter` | 이동·점프·낙사 (**튜닝 질문**) |
| 11 | [CA3DPlayerController](11-CA3DPlayerController.md) | `ACA3DPlayerController` | 45도 스냅 카메라 (**입력 기준 질문**) |
| 12 | [StatusComponent](12-StatusComponent.md) | `UStatusComponent` | 스탯 복제·갇힘 상태 전이 |
| 13 | [PooledActor](13-PooledActor.md) | `IPooledActor` | 인터페이스 컴파일 |
| 14 | [PoolSubsystem](14-PoolSubsystem.md) | `UPoolSubsystem` | 200개 획득/반납 스트레스 |
| 15 | [ExplosionSubsystem](15-ExplosionSubsystem.md) | `UExplosionSubsystem` (+`ExplosionTypes.h`) | `Propagate` 6방향·층간 전파 |
| 16 | [Bomb](16-Bomb.md) | `ABomb` | 설치→3초→폭발→파괴·연쇄·프리뷰 |
| 17 | [PredictedBombVisual](17-PredictedBombVisual.md) | `APredictedBombVisual` | 즉시 표시→서버 확정 교체 |

**1주차 마감 게이트**: Listen Server PIE 2인 테스트 — 두 클라의 지형이 동일한가 (구조 설계서 5장 11번). Task 17 체크리스트에 포함.

## 2주차 — 멀티 / 데디 서버

| # | Task | 클래스 | 산출물 검증 |
|---|---|---|---|
| 18 | [CA3DPlayerState](18-CA3DPlayerState.md) | `ACA3DPlayerState` | 생존/승패 상태 복제 |
| 19 | [CA3DGameInstance](19-CA3DGameInstance.md) | `UCA3DGameInstance` | EOS 세션 생성·참가 |
| 20 | [BotController](20-BotController.md) | `ABotController` | 봇이 플레이어와 같은 경로로 동작 |

**배포 방침 (2026-07-30)**: 데디 서버 검증은 **Windows 데디(`CrazyArcade3DServer.exe`)로 진행**하고,
리눅스 크로스 컴파일은 클라우드에 실제로 올릴 시점으로 이월한다 (사유·착수 절차는 `mds/build.md`).
Task 18 과 19 사이에 "Windows 데디 서버 실전 확인"(별도 프로세스 + 클라 2, GDD 7.4 점검)을 끼운다.

## 3주차 — 콘텐츠

| # | Task | 클래스 | 산출물 검증 |
|---|---|---|---|
| 21 | [MapValidator](21-MapValidator.md) | `FMapValidator` | 검증 5종 각각 독립 테스트 |
| 22 | [ProcMapGenerator](22-ProcMapGenerator.md) | `UProcMapGenerator` | 같은 시드=같은 맵, 리롤 동작 |
| 23 | [ItemPickup](23-ItemPickup.md) | `AItemPickup` | 획득→스탯 반영, 폭발 소멸 |
| 24 | [SuddenDeathSubsystem](24-SuddenDeathSubsystem.md) | `USuddenDeathSubsystem` | 예고→낙하→파괴, 외곽 가중 |
| 25 | [CA3DHUD](25-CA3DHUD.md) | `ACA3DHUD` | 데디 서버에서 생성 안 됨 |
| 26 | [MatchWidget](26-MatchWidget.md) | `UMatchWidget` | HUD 항목 갱신 |
