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
- [x] **08. `ACA3DGameState`** — Framework/CA3DGameState.h/.cpp (Rules 에셋 포인터·AliveCount·MatchStartServerTime 복제, 로직 없음) / 두 타깃 빌드 + PIE 복제 검증 완료 (AliveCount 실검증만 Task 18로 이월)
- [x] **09. `ACA3DGameMode`** — Framework/CA3DGameMode.h/.cpp (시드 결정+고정 시드 모드, GameState에 Rules 세팅 → VoxelWorld 초기화, 스폰 셀 순서 배정 ChoosePlayerStart) + VoxelWorld 임시 자동 초기화 제거·룰셋을 GameState 복제본으로 교체·스폰 셀 게터 / 자동화 테스트 `CrazyArcade3D.Framework.GameMode` + 두 타깃 빌드 + 에디터 연결(BP_CA3DGameMode·World Settings) + PIE 검증 완료 (맵 생성·스폰·2인 상이 스폰·Rules 복제·고정 시드)
- [x] **10. `ACA3DCharacter`** — C++ 완료(Gameplay/Character/CA3DCharacter.h/.cpp — CMC 기본, 룰셋 계수×CellSize 파생 이동속도 4칸/초·점프 정점 1.4칸·스텝 0.3칸 전부 임시, GetFootCell 1차 정의, 서버 KillZ 로그) + 테스트 `CrazyArcade3D.Gameplay.Character` 통과, 두 타깃 빌드 통과 — 에디터 연결(BP_CA3DCharacter) 완료 + **튜닝 확정(2026-07-30)**: 셀 100·4칸/초 현행 유지, 가감속 0.05초(공중 포함 AirControl 1.0), 점프 이동 거리 0.7배(`JumpAirSpeedFactor` — 수평 속도만 감소, 체공 시간 불변), 점프 높이 1칸·발밑 셀(잠정) + PIE 체감 확인. 잔여 미검증: 계단 2층 도달·2블록 벽 못 오름(정점 경계는 자동화로 검증)·KillZ 낙사 로그
- [x] **11. `ACA3DPlayerController`** — C++ 완료(Gameplay/Character/CA3DPlayerController.h/.cpp — Enhanced Input 바인딩, 45도 스냅 SpringArm 카메라, WASD 월드 축 기본 + `ca3d.CameraRelativeInput` 토글) — 에디터 연결(IMC/IA·BP 컨트롤러) 완료 + **입력 기준 = 카메라 기준 확정(2026-07-30)** + 카메라 붐 컬리전 off(벽에서 확대되던 문제 — 가림은 3주차 디더 페이드). 잔여 미검증: 45도 스냅 8방향 순회·회전 중 이동 연속성
- [x] **12. `UStatusComponent`** — Gameplay/Character/StatusComponent.h/.cpp (스탯 5종+LifeState 복제, Server* 4종 권한 가드, Trap 만료 타이머→익사, KillZ 낙사를 ServerKill(Fall)로 연결, 속도 재계산은 캐릭터 RefreshMoveSpeed 단일 경로) + 룰셋에 RollerSpeedStep·MoveSpeedMulCap(임시) / 테스트 `CrazyArcade3D.Gameplay.StatusComponent` 통과, 두 타깃 빌드 통과. PIE 복제 실검증은 체크리스트 12 잔여
- [x] **13. `IPooledActor`** — Core/PooledActor.h (UINTERFACE 쌍, Acquire/Release 콜백 계약, BP 구현 차단) / 두 타깃 빌드 통과 (순수 인터페이스 — 컴파일 검증만)
- [x] **14. `UPoolSubsystem`** — Core/PoolSubsystem.h/.cpp (클래스별 프리 리스트+GC 보호 래퍼, Prewarm/Acquire/Release, 계약 위반 ensure) / 테스트 `CrazyArcade3D.Core.PoolSubsystem` 통과 (200개×5회 누수 없음), 두 타깃 빌드 통과. stat unit 실측은 Task 16 실사용 후
- [x] **15. `UExplosionSubsystem`** — Gameplay/Bomb/ExplosionTypes.h + ExplosionSubsystem.h/.cpp (Propagate static 순수 함수 — 6방향·층간·Immortal 차단·Destructible 멈춤·Floor 룰 분기·연쇄 검출, 불변식 2) / 테스트 `CrazyArcade3D.Gameplay.ExplosionSubsystem` 통과 (순수성 포함 8항목), 두 타깃 빌드 통과. RequestDetonate/ProcessChainStep 연쇄 스케줄링은 ABomb 부재로 Task 16에서 구현 (TODO 주석으로 명세 보존)
- [x] **16. `ABomb`** — C++ 완료(Gameplay/Bomb/Bomb.h/.cpp 서버 권한 폭탄 + ServerPlaceBomb/ClientRejectBomb RPC + 공중 -Z 스캔 설치 셀(잠정 규칙) + ExplosionSubsystem 연쇄 스케줄링(RequestDetonate/ProcessChainStep 6단계·ChainStepDelay 분산) + WaterSegment/DangerDecal 풀링 FX + ExplosionFXRelay Multicast) + 테스트 `CrazyArcade3D.Gameplay.Bomb` 통과(설치 셀 4종·권위 검증 4종·폭발 단일 경로·연쇄·중복 방지), 두 타깃 빌드 통과 + 에디터 연결(BP_Bomb·BP_DangerDecal·BP_WaterSegment·머티리얼) 완료 + 2인 PIE 검증 대부분 통과(2026-07-30 — 퓨즈 3.007s·연쇄 4단 70~78ms·개수 거부·슬롯 반환 후 재설치·프리뷰 일치 로그/사용자 확인) — 2인 PIE 검증 완료(2026-07-30 — 퓨즈 3.007s·연쇄 4단 70~78ms·개수/중복 거부·슬롯 반환 후 재설치·프리뷰 일치·물줄기 피격 Trap→익사·파괴 후 낙하·stat unit 이상 없음). 잔여 미검증 2건은 후속 재확인 대상: 물줄기 풀 반납 실측 · 점프 판정 세부(제자리 피격/다른 발판 회피 — 발밑 셀 정의 확정 시). 공중 설치 규칙은 잠정
- [x] **17. `APredictedBombVisual`** — C++ 완료(Gameplay/Bomb/PredictedBombVisual.h/.cpp 클라 예측 비주얼: bReplicates=false·타이머/틱/판정 0줄(불변식 3)·풀링 + 캐릭터 TryPlaceBombPredicted 로컬 검증 4종(Alive·셀 Empty·같은 셀 예측 중복·개수 예측치) 후 비주얼+RPC, 리슨 호스트는 예측 생략 + ABomb 클라 BeginPlay/ClientRejectBomb 양쪽 Cell 매칭 반납 + 룰셋 PredictedBombVisualClass) + 테스트 `CrazyArcade3D.Gameplay.PredictedBombVisual` 통과(11항목 — 거부 3종·획득·연타·방치 무폭발·반납 매칭·풀 오염 없음·호스트 생략), 전체 회귀 11스위트 통과, 두 타깃 빌드 통과 + 에디터 연결(BP_PredictedBombVisual·룰셋 PredictedBombVisualClass) 완료 + PIE 동작 검증 5항목 통과(2026-07-30 · 지연 PktLag 100~130ms — 즉시 표시·교체 무겹침·거부 시 비주얼만 제거·방치 무폭발·풀 오염 없음) — **🏁 1주차 마감 게이트 통과 (2026-07-30)** — 데디 서버 + 클라 2 구성에서 파괴 10회 × 3인스턴스 = 30건 해시 전부 일치(불일치 0), 최종 486E4312 / 740칸. 측정은 `ApplyDestruction` 자동 해시 로그로 (앞선 2회는 콘솔 명령의 월드 해석·측정 시점 문제로 무효였음 → 절차 의존 제거)

## 2주차 — 멀티 / 데디 서버

> **배포 방침 변경 (2026-07-30)**: 리눅스 데디 서버는 **클라우드에 실제로 올릴 시점으로 이월**한다.
> 데디 서버와 리눅스는 별개다 — Windows 데디(`CrazyArcade3DServer.exe`)는 이미 빌드·동작하고
> 1주차 마감 게이트도 데디 모드로 통과했다. 리눅스가 필요한 이유는 **클라우드 비용 하나**라
> (GDD 8장: 시간당 과금 VPS·도쿄 리전), 개발 단계에서 툴체인 설치(수 GB)와 리눅스 전용 빌드
> 이슈를 미리 겪을 이유가 없다. 코드 변경 없이 크로스 컴파일되므로 배포 직전에 붙이면 된다.

> GDD 8장 2주차 항목 중 **"서버 권한 구조 이전 + 폭탄 로컬 예측", "시드 전송 + 파괴 리플리케이션"은
> 1주차에 달성 완료** (불변식 5로 처음부터 권한 분리 / Task 16·17 + 게이트로 동기화 실증). 남은 것:

- [ ] **18. `ACA3DPlayerState`** — C++ 완료(Framework/CA3DPlayerState.h/.cpp 복제 3필드(ColorIndex·FinalRank·bAlive, 로직 0) + GameMode 승패 판정(PostLogin 색 배정·참가 인원·AliveCount, NotifyPlayerDeath→다음 틱 배칭→ResolvePendingDeaths) + GameState `bMatchEnded` + 룰셋 `MinPlayersForMatchEnd` + StatusComponent 사망 통지 배선) + 테스트 `CrazyArcade3D.Framework.PlayerState` 통과, 전체 회귀 12스위트, 두 타깃 빌드 통과.
      **동시 사망 = 공동 등수, 마지막 전원 동시 사망 = 무승부(1등 공석)** 로 사용자 확정 — `FMath::Max(2, AliveBefore-K+1)` 하한이 우승 자리를 비워 두는 장치.
      — **PIE 검증(색 인덱스 전 클라 동일·복제·최후 1인 종료) 대기라 미체크.** 잔여: 관전 상태 전환 미구현 · 중도 이탈 규칙 미확정
- [ ] **27. 사망 처리·관전** (`mds/Tasks/27-DeathHandling.md`) — 죽으면 이동 불가 + 메시 숨김 + 컬리전 off
      (유령 방해 없음). 폰은 파괴·풀링하지 않고 유지 — 부활을 넣을 때 상태만 되돌리면 되게.
      ⚠️ 새 클래스 없음(StatusComponent·CA3DCharacter 확장) — 1 Task = 1 Class 관례의 예외
- [ ] **Windows 데디 서버 실전 확인** — PIE 가 아니라 `CrazyArcade3DServer.exe` 를 별도 프로세스로
      띄우고 클라 2개 접속. GDD 7.4 "데디에서 나이아가라·사운드·머티리얼·BP 가 돌지 않는가" 확인 포함
- [ ] **19. `UCA3DGameInstance`** — EOS 세션·로비 (방 생성 → 공개 목록 → 참가 → 준비·시작)
- [ ] **20. `ABotController`** — 봇으로 인원 채우기 (🎯 "데디 서버로 8인 한 판 완주"의 현실적 수단)
- [ ] (이월) 리눅스 크로스 컴파일 툴체인 — 클라우드 배포 시점에 착수 (`mds/build.md`)

## 3주차 — 플레이어블 멀티 데모

_(미정)_

## 진행 중 메모

- ⭐ 1번 시점부터 `stat unit` 을 켜고 개발한다 (GDD 7.4). "다 만들고 최적화"는 3주 프로젝트에 없다.
- 사망 처리 방향 (2026-07-30 사용자): 죽으면 **이동 불가 + 캐릭터 파괴 또는 풀 반납** — 부활 기능을 넣을 여지를 남긴다.
  사망·갇힘 연출(물방울 비주얼·애니메이션)은 에셋이 생긴 뒤 후속 Task 에서. 지금은 로그만 (StatusComponent TODO).
