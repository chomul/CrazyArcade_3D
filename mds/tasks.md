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

- [x] **18. `ACA3DPlayerState`** — C++ 완료(Framework/CA3DPlayerState.h/.cpp 복제 3필드(ColorIndex·FinalRank·bAlive, 로직 0) + GameMode 승패 판정(PostLogin 색 배정·참가 인원·AliveCount, NotifyPlayerDeath→다음 틱 배칭→ResolvePendingDeaths) + GameState `bMatchEnded` + 룰셋 `MinPlayersForMatchEnd` + StatusComponent 사망 통지 배선) + 테스트 `CrazyArcade3D.Framework.PlayerState` 통과, 두 타깃 빌드 통과.
      **동시 사망 = 공동 등수, 마지막 전원 동시 사망 = 무승부(1등 공석)** 로 사용자 확정 — `FMath::Max(2, AliveBefore-K+1)` 하한이 우승 자리를 비워 두는 장치.
      **실전 검증 완료(2026-08-02, Task 20 봇 매치)** — 쿡된 데디 서버에서 ColorIndex 순차 배정(0·1·…),
      **동시 사망 2명에게 공동 3등**이 실제로 발화, 최후 1인에서 `매치 종료: 우승자 확정`.
      잔여: 관전 상태 전환 미구현 · 중도 이탈 규칙 미확정
- [x] **27. 사망 처리·관전** — C++ 완료(`ACA3DCharacter::ApplyDeathState` 단일 지점: 캡슐 컬리전 off·`MOVE_None`·**액터 숨김**(데디 가드) + `Move`/`DoJump` 생존 가드(캐릭터에 둬서 봇도 같은 경로) + 서버 `ServerKill`·클라 `OnRep_Life` 가 같은 함수 통과) + 테스트 `CrazyArcade3D.Gameplay.DeathHandling` 통과, 전체 회귀 13스위트, 두 타깃 빌드 통과 + **PIE 검증 통과(2026-08-02 — 시체 소멸·유령 방해 없음·관전 시점 그 자리 고정·갇힘 중 점프 차단)**.
      폰은 파괴·풀링하지 않고 유지 — 부활은 이 함수가 바꾼 것을 되돌리기만 하면 된다.
      **컬리전·이동 모드는 서버·클라 각자 적용** — 복제되지 않는 값(`bActorEnableCollision` 미복제 / `ReplicatedMovementMode` 는 COND_SimulatedOnly)이라 서버 가드로 감싸면 죽은 본인 화면이 깨진다 (Task 문서 "정정" 절).
      숨김은 **메시 하나가 아니라 액터 단위** — `GetMesh()` 만 끄면 BP 서브클래스가 추가한 메시가 남아 시체가 보인다(1차 PIE 실패 원인). 확정: **갇힘 중 점프 금지**(`DoJump` 는 `Alive` 만 통과, `Move` 는 `Dead` 만 차단 — 조건이 서로 다르다).
      잔여 미검증: 데디 서버에서 시각 처리 생략(GDD 7.4) — 다음 항목 "Windows 데디 서버 실전 확인"에서 함께 본다. 자유 관전·추적은 후속
      ⚠️ 새 클래스 없음(StatusComponent·CA3DCharacter 확장) — 1 Task = 1 Class 관례의 예외
- [x] **Windows 데디 서버 실전 확인** (2026-08-02) — 쿡·스테이징한 `CrazyArcade3DServer.exe` 를 별도
      프로세스로 띄우고 클라 2개 접속 성공. 체크리스트 `mds/Checklists/dedi-server-windows.md`.
      포트 리스닝·틱 30·양쪽 `Welcomed by server`·**ColorIndex 0/1 순서 배정**(Task 18 실전 동작)·
      **시드 복제로 두 클라가 동일 지형**(셀 1764/솔리드 756/인스턴스 756)·Warning·Error 0건.
      GDD 7.4 통과: 오디오 미초기화·RHI Null·셰이더 컴파일 0·머티리얼/파티클/Slate 0·Niagara 는 플러그인 마운트 로그뿐.
      · **언쿡 실행은 불가 판명** — 서버 exe 에 `.uproject` 를 넘겨 띄우면 엔진 기본 에셋을 읽다가 크래시한다.
        우리 코드가 아니라 엔진 구조: 언쿡 패키지 헤더에 든 `PersistentGuid`(16B)를 `WITH_EDITORONLY_DATA=0` 인
        서버 타깃이 건너뛰어 스트림이 어긋난다 (`mds/build.md` 상세). → **쿡·스테이징 후 실행**으로 절차 확정.
        리눅스 배포도 같은 절차라 그대로 재사용된다
      · 잔여(입력이 필요해 헤드리스 불가): 실전 파괴 해시 일치 · 사망 복제 · 최후 1인 종료 →
        **Task 20(봇) 이 들어오면 한 판 완주로 자동 검증된다**
- [~] **19. `UCA3DGameInstance`** — ⏸️ **보류 (2026-08-02 사용자 결정)**. 구현은 끝났고 **꺼져 있다**
      (`bEnableEOS=false`). 목표가 "게임 자체를 얼마나 빨리 만드느냐"이고 배포가 미정이라
      로비/세션을 **배포 시점으로 이월**했다. 접속은 IP 직접 — 데디 서버에서 이미 검증된 경로다.
      만든 것: Framework/CA3DGameInstance.h/.cpp(Device ID 로그인·방 생성/검색/참가/파기·실패 통지 델리게이트)
      + 자격 증명 로더(모듈 StartupModule 이 `Config/EOSCredentials.ini`→GEngineIni 병합, **gitignore 대상**)
      + `ca3d.*` 진단 콘솔 + 테스트 `CrazyArcade3D.Framework.GameInstance`(자격 증명 유출 회귀 포함),
      전체 회귀 15스위트, 두 타깃 빌드 통과. 끈 상태에서 데디 봇 매치가 이전과 동일 동작함을 확인(에러 0).
      ⛔ **엔진 블로커**: UE 5.8 OSS v1 EOS 는 데스크톱에서 Device ID 로그인이 **구조적으로 불가능**하다 —
      `UserLoginInfo`(SDK 가 Device ID 로그인에 필수라고 명시)를 채우는 코드가 `#if ADD_USER_LOGIN_INFO`
      안에 있고 데스크톱 기본값이 꺼짐이라 `nullptr` 이 전달돼 `EOS_InvalidParameters`.
      자격 증명 문제가 아님을 SDK 직접 호출 성공으로 분리 증명했다. 재개 절차는 `mds/Tasks/19-*.md` "보류 상태" 절
- [x] **20. `ABotController`** — AI/BotController.h/.cpp (순수 C++ FSM: Evade > Attack > Wander, 위험·설치 판단은
      `Propagate` 재사용, 그리드 BFS(높이차 1칸), 재계획 주기로 매 틱 BFS 회피) + GameMode 봇 채우기
      (참가 등록을 `RegisterParticipant` 단일 경로로 통합 — 사람·봇 공용) + 룰셋 Bot 6종 + 콘솔 `ca3d.BotFill`
      + 테스트 `CrazyArcade3D.AI.BotController`, 전체 회귀 14스위트, 두 타깃 빌드 통과.
      **봇도 `ACA3DPlayerState` 를 갖는다**(`bWantsPlayerState`) — Task 18 판정이 사람과 봇을 구분하지 않는다.
      🎯 **데디 서버 한 판 완주 달성(2026-08-02)**: 사람 입력 0으로 공동 4등→공동 3등(2명 동시)→공동 2등→우승자 확정,
      파괴 29건, 서버·클라 해시 23지점 전부 일치. Task 18·27 의 미검증 항목이 이걸로 함께 해소됐다.
      ⛔ **이 과정에서 데디 전용 버그 2건 발견·수정** (`mds/Checklists/dedi-server-windows.md`):
      ① 데디에 지형 컬리전이 없었다(HISM 을 "시각 전용"으로 보고 파괴 — 캐릭터가 지형 통과, 지상 판정 3.6%→58%)
      ② 중간 접속 클라가 접속 전 파괴를 못 받아 지형이 달랐다(→ `DestroyedCells` 복제 이력 + 미적용분 따라잡기)
      한계(구현자 보고): 2칸 이상 낙하 경로 미계획 · 공중 설치 안 함 · 폭탄 차기/아이템 없음 (체크리스트 20)
- [ ] (이월) 리눅스 크로스 컴파일 툴체인 — 클라우드 배포 시점에 착수 (`mds/build.md`)

## 3주차 — 플레이어블 멀티 데모

우선순위 기준: **게임을 빨리 완성하는 것**(2026-08-02 사용자). 체감에 직접 꽂히는 것부터.

- [x] **23. `AItemPickup`** — Gameplay/Item/ItemPickup.h/.cpp (서버 권한 아이템 액터, 복제, 획득→`ServerApplyItem`,
      물줄기 소멸) + 생성기 아이템 배치(`FRandomStream` + 정수 퍼센트 — 불변식 4) + `ExplosionSubsystem` 배선
      (소멸 → 노출 순서) + `AVoxelWorld::ConsumeItemPlacement`(데이터만 — 지형은 아이템 액터를 모른다)
      + 룰셋 Item 카테고리 + 니들 수동 사용 입력 경로 + 테스트 `CrazyArcade3D.Gameplay.ItemPickup`,
      전체 회귀 16스위트, 두 타깃 빌드 통과.
      **확정 규칙(사용자)**: 갇힘 중 획득 불가 · 니들은 별도 키 수동 사용 · 킥은 획득 플래그까지만.
      데디 실전(봇 6대·220초): 노출 7 · **획득 4(니들·킥·풍선·물약)** · 소멸 1 · 에러 0.
      컬리전(Sphere)과 표시(Mesh)를 분리해 **데디에서도 획득이 동작** — HISM 사건의 재발 방지책.
      잔여: 에디터 작업(BP_ItemPickup 메시·IA_UseNeedle·IMC 바인딩) 후 PIE 체감 확인
- [x] **25·26. HUD·결과 화면** — C++ 완료(UI/CA3DHUD.h/.cpp — 위젯 수명 관리 + **캔버스 텍스트 폴백**(`ca3d.DebugHUD`
      -1 자동/0 끄기/1 강제, `#if !UE_BUILD_SHIPPING`) / UI/MatchWidget.h/.cpp — GDD 5장 HUD 3요소 + 결과 화면,
      표시 가공·순위 정렬·무승부 판정을 **static 순수 함수**로 분리) + `ACA3DGameMode` 생성자 `HUDClass` 배선
      + Build.cs UMG·Slate·SlateCore 를 Public 으로 승격 + 테스트 `CrazyArcade3D.UI.MatchWidget`,
      **전체 회귀 17스위트 실패 0**, 두 타깃 빌드 통과.
      ⭐ **에디터 작업 0 으로도 값이 보인다** — WBP 미제작 상태에서 클라 스크린샷으로 확인
      (`생존 1  0:00` / `폭탄 0/1  범위 1  속도 x1.00  니들 X  킥 X`, 한글 글리프 정상).
      · 명세 변경 2건: `BindWidget`(필수) → **`BindWidgetOptional`** (필수면 이름이 다 맞을 때까지 WBP 가
        컴파일조차 안 돼 첫 제작이 막힌다 — 대신 `NativeConstruct` 가 미바인딩 이름을 한 줄로 경고) ·
        `ShowResult(Ranking)` → **`ShowResult()`** (순위 수집이 HUD·위젯 두 곳에 생기는 것을 막으려고 출처를 한 곳에서 읽음)
      · **서든데스 경고는 상태를 지어내지 않았다** — 항상 Collapsed, `UpdateSuddenDeathWarning(bool)` 호출 한 줄이
        Task 24 연결 지점(주석으로 명시). 없는 값을 추측해 채우면 진짜 구현과 충돌한다
      · 갱신은 **폴링 + 스냅샷 비교** — `OnRep` 을 UI 가 잡으면 Framework→UI 역방향 의존이 생긴다.
        `FMatchStatSnapshot` 이 같으면 문자열을 다시 만들지 않고, StatusComponent 포인터는 **폰이 바뀔 때만** 재해석
      · **데디 검증 통과(GDD 7.4)** — 쿡·스테이징한 `CrazyArcade3DServer.exe` 봇 4인 75초 완주에서
        HUD·위젯·Slate·UMG 생성 흔적 **0건**, Error·Warning 0건 (`AGameModeBase` 는 데디에서 HUD 를 스폰하지 않고,
        `ACA3DHUD::BeginPlay` 최상단 가드가 이중 방어)
      · **UI→Gameplay(StatusComponent) 읽기 예외** — GDD 5장 ① 의 출처가 여기뿐이라 불가피. 읽기만 하며 주석에 명시
      · **에디터 작업 완료(2026-08-04)** — `Content/UI/WBP_Match` + `BP_CA3DHUD`, `BP_CA3DGameMode` 의
        `HUDClass` 오버라이드. 계층 구조·배치값·함정은 `mds/Tasks/26-MatchWidget.md` 에 남겼다.
        실행 로그로 확인: `매치 위젯 표시 — WBP_Match_C` · `결과 화면 표시 — 3명, 우승자 있음` ·
        **`미바인딩 위젯` 경고 0건**(= 11개 바인딩 전부 연결. `BindWidgetOptional` 이라 경고 부재가 유일한 증거).
        사용자 PIE 세션에서는 **무승부 경로**(`bMatchEnded` + Rank==1 없음)도 처음 발동해 정상 표시됐다
      · 잔여 미검증은 **선행 Task 대기 2건뿐** — 서든데스 경고(Task 24) · 로비 복귀 동선(Task 19 보류).
        볼 수 있는 것은 전부 확인됐다 (갇힘/사망 시 패널 접힘·`-` 표시 포함)
- [x] **24. `USuddenDeathSubsystem`** — C++ 완료(Gameplay/SuddenDeath/SuddenDeathSubsystem.h/.cpp —
      서버 낙하 스케줄러 + `ASuddenDeathRelay`(Multicast 소유 액터) + `ASuddenDeathDropMarker`(풀링 마커)).
      두 타깃 빌드 통과, **전체 회귀 18스위트 실패 0** (신규 `CrazyArcade3D.Gameplay.SuddenDeath`).
      · **사용자 확정(2026-08-04)**: 서든데스 낙하만 바닥을 부순다 — 신규 `bSuddenDeathDestroysFloor=true`,
        기존 `bFloorDestructible=false` 유지. 초반 150초는 발판이 보장되고 서든데스부터 구멍이 뚫린다.
        `DropInterval=1.0` · `DropsPerWave=1` · `DropExplosionRange=2`
      · ⭐ **`Propagate` 를 한 줄도 안 고쳤다** — 불변식 2 덕에 `bFloorDestructible` 이 이미 인자다.
        서든데스는 거기에 자기 룰셋 값을 넘기기만 한다. 순수 함수로 둔 대가를 여기서 회수했다
      · **폭발 적용부를 폭탄과 공유** — `ApplyExplosionCells`(②~⑤ 파괴·FX·갇힘·아이템)를 추출해
        `ProcessChainStep` 과 `ServerApplyExplosionAt` 이 같은 본체를 탄다. 따로 구현했으면
        "폭탄으로 부순 블록과 서든데스로 부순 블록이 다르게 동작"하는 어긋남이 조용히 생겼을 자리다.
        연쇄 스케줄링(단계 분산·`ChainStepDelay`·재진입 가드)은 이동하지 않아 폭탄 회귀 0
      · **낙사 원인은 시각으로 판정** — `bSuddenDeathActive` 면 `SuddenDeath`, 아니면 `Fall`.
        "누가 이 구멍을 냈나"를 추적하려면 `FVoxelGrid` 가 파괴 원인을 알아야 하고 그 순간
        Voxel 독립성이 무너진다. 통계용 분류에 그만한 비용을 치르지 않는다
      · 명세 변경 3건: `MulticastWarnDrop` 을 서브시스템 → `ASuddenDeathRelay` 로 (UHT 는 RPC 를
        액터에만 허용 — `AExplosionFXRelay` 와 같은 사정) · 시그니처를 셀 1개 → **웨이브 배열**
        (`DropInterval`(1.0) < `DropWarningTime`(1.5) 이라 예고 중인 웨이브가 동시에 여러 개다.
        핸들을 하나만 두면 뒤 웨이브가 앞 웨이브를 덮어써 앞 웨이브가 영영 안 떨어진다) ·
        `DropMarkerClass` 타입을 `AActor` → `ASuddenDeathDropMarker` (`UPoolSubsystem::Acquire` 가
        `IPooledActor` 미구현 시 ensure 로 실패 — 순수 AActor 면 낙하마다 터진다)
      · **HUD 서든데스 경고 연결 완료** — `UpdateSuddenDeathWarning(GameState->bSuddenDeathActive)`.
        Task 25·26 의 마지막 미검증 항목이 이걸로 닫힌다 (표시 확인은 PIE)
      · 잔여: PIE 검증(마커 보고 회피·낙사 원인 집계·페이싱) · 데디 exe 마커 스킵 확인 ·
        에디터 작업(`BP_DropMarker` 생성 후 `DA_Rules_Default` 의 `DropMarkerClass` 지정)
      · ⚠️ 튜닝 대기: **낙하의 약 21%가 부술 것 없는 Immortal 외벽 위에 떨어진다** (체크리스트 24 참조)
- [x] **(추가) 폭탄 컬리전 + 물건 회전** (2026-08-06 사용자 요청) — 두 타깃 빌드 통과 · 전체 18스위트 실패 0 ·
      `-game` 실전 세션에서 폭탄 9개 전부 막힘 승격(에러·ensure 0)
      · **폭탄이 플레이어를 막는다** — `ABomb::BlockingBox`(UBoxComponent, 룰셋 `BombBlockExtentCells` × CellSize).
        설치자가 자기 폭탄에 갇히지 않도록 **처음엔 Overlap, 겹친 폰이 전부 빠져나가면 Block 으로 승격**(원작 규칙).
        실측 승격 지연 77~268ms(장전→승격) — 즉시도 아니고 영영 안 막히지도 않는다.
        ⚠️ **데디 서버에서도 파괴하지 않는다** — 막힘은 물리이고 CMC 는 서버에서도 돈다 (HISM 함정과 동형)
      · **폭탄·예측 폭탄·아이템 메시가 제자리에서 돈다** — `Gameplay/SpinVisual.h` 한 곳을 셋이 공유
        (룰셋 `PickupSpinDegreesPerSecond`, 기본 45°/s = 8초 1바퀴). 속도가 갈리면 서버 확정 순간
        예측→진짜 교체가 각도 점프로 보이므로 출처를 하나로 묶었다. 판정 컴포넌트는 돌지 않는다
      · 테스트 `PredictedBombVisual` ⑦ 의 불변식 3 가드를 교체 — "bCanEverTick == false"(회전 틱이 생겨 무효)에서
        **틱을 100회 직접 돌려 회전 외 상태 변화 0** 을 확인하는 방식으로. 틱 본문을 실제로 실행하므로 더 강하다
      · 잔여: 봇이 폭탄 벽에 막혀 갇히는 상황이 나오는지 장기 관찰 (봇 FSM 은 폭탄 막힘을 모른다)
- [x] **(추가) 가림 디더 페이드** (2026-08-06 사용자 요청 — Task 11 에서 미룬 항목) —
      두 타깃 빌드 통과 · 전체 **19스위트 실패 0** · 체크리스트 `mds/Checklists/27-OcclusionFade.md`
      · `Voxel/VoxelRayCast`(순수 함수 3D DDA) + `IVoxelRenderer::SetCellFade`(숫자만 받는다) +
        `Gameplay/Character/OcclusionFadeComponent`(카메라·폰을 아는 쪽). 지형은 "카메라가 나를
        못 본다"는 개념을 모른 채로 남는다
      · **동적 머티리얼 금지**(GDD 7.4) 준수 — HISM 인스턴스별 커스텀 데이터 float 1개로 처리
      · 재계산 0.1초 간격(GDD 7.4), 페이드 보간은 매 프레임
      · **화면 마스크 (2026-08-06 사용자 확정)**: 블록을 통째로 지우지 않고 **화면에서 캐릭터를
        덮는 픽셀만** 뚫는다. C++ 이 머리·발·옆구리를 투영해 화면 타원(중심uv·반지름uv)을
        파라미터 컬렉션에 매 프레임 기록하고, 머티리얼이 `PerInstanceCustomData × 화면마스크` 로
        곱한다. **둘 다 필요하다** — 마스크만 쓰면 캐릭터 뒤쪽 벽·발밑 바닥에도 구멍이 뚫리고,
        블록 판정만 쓰면 벽 한 칸이 통째로 사라진다. 컬렉션도 동적 머티리얼이 아니라 7.4 위반 아님
      · **에디터 작업 완료 + 화면 동작 확인 (2026-08-06)** — 머티리얼 디더 + MPC + 마스크 곱.
        진짜 원인은 **머티리얼 인스턴스의 `Material Property Overrides`** 가 부모의 Masked 를
        이기고 있던 것 (MI 3개). CLAUDE.md 함정 표 + 체크리스트 27 "실제로 밟은 함정"에 기록
      · 진단 도구: `ca3d.DebugOcclusionFade 1`(판정 박스) / `2`(인스턴스에서 되읽은 실제 페이드 값 —
        C++ 과 머티리얼 중 어느 쪽 문제인지 가르는 용도)
      · ⚠️ **지금 카메라(-55도)·폴백 맵(블록 1칸)에서는 가림이 드물다** — 시선이 내부 블록 위를
        지나간다. 자주 보려면 피치를 얕게 하거나 Task 22 에서 2~3칸 구조물을 만들어야 한다
- [x] **21. `FMapValidator`** — `MapGen/MapValidator.h/.cpp` (GDD 4.2 검증 5종을 static 순수 함수로) +
      테스트 `CrazyArcade3D.MapGen.MapValidator`, 전체 **20스위트 실패 0**, 빌드 통과.
      Task 22 의 리롤 판정이 이걸 호출하므로 **22 보다 먼저** 했다 (tasks.md 3주차 목록에 빠져 있었음)
      · 이동 그래프가 곧 게임 규칙 — **1칸은 오르고 2칸은 못 오른다**. 테스트에 "2칸 실패" 케이스 명시
      · ⚠️ **④(고립 구역)는 최외곽 링을 제외한다** — 경계 벽 꼭대기는 도달 불가가 **설계**다
        (1칸 점프로 벽 위에 못 올라가게 2층으로 올린 것). 제외 안 하면 정상 맵이 전부 떨어져
        리롤이 무한히 돈다. 첫 구현에서 실제로 폴백 맵이 불통과했다
      · 폴백 맵 실측: 스폰 8개 / 스폰 간 최소 맨해튼 **9** / 아이템 18개 → 임계값 8의 근거
- [x] **22. `UProcMapGenerator`** — `MapGen/ProcMapGenerator.h/.cpp` + `MapGenUtil.h/.cpp`(아이템 배치
      공용 헬퍼 — 폴백과 같은 본체) + 테스트 `CrazyArcade3D.MapGen.ProcMapGenerator`,
      전체 **21스위트 실패 0** · 두 타깃 빌드 통과 · `-game` 실전 세션 정상 (체크리스트 22 참조)
      · **확정(2026-08-06 사용자)**: 산·협곡 Z 6층 (웨딩케이크 계단식 — 전 층 1칸 점프 도달) ·
        맵 크기 인원별 티어 (≤4명 17×17×6 / 초과 21×21×6, 값은 룰셋)
      · 결정론 실증: 같은 시드 5회 해시 동일 (`FC080461`) · 다른 시드 10개 고유 해시 10/10 ·
        리롤도 파생 시드 고정식(`Seed*7919+attempt`)
      · **파괴:고정 8:2 확정 (2026-08-06 사용자)** — 구조물(산)을 전부 Destructible 로.
        실측 79~84% 파괴, 테스트가 최악 시드 75% 이상 강제. Immortal 은 외곽 벽 + 기둥 격자뿐.
        파생 수정: 사분면 편차 기준을 아이템 수 비례(max(4, N/4))로 — 아이템 11→71개가 되며
        절대값 4는 기준 시드조차 리롤 상한을 소진시켰다. ⚠️ **아이템 밀도는 밸런스 미확정** —
        파괴 블록 4배 ⇒ 매치당 아이템도 ~4배(71개). `ItemDropPercent`(30) 조정은 사용자 판단
      · **`GridSize` 복제 추가** — 클라가 로컬 인원수로 크기를 유추하면 중간 접속자가 다른 맵을
        만든다. 크기는 서버 결정·복제만. 생성기 실패도 결정론이라 서버·클라가 같은 폴백을 탄다
      · ⚠️ **-ExecCmds 타이밍 함정** — cvar 는 BeginPlay 이후 적용이라 티어 판정이 커맨드라인
        폴백을 탄다. `FParse::Value` 는 따옴표 안을 건너뛰므로(-ExecCmds="..." 못 찾음)
        ExecCmds 값을 먼저 꺼낸 뒤 그 안에서 찾는다 (체크리스트 22)
      · 잔여: PIE 체감(6층 지형에서 점프·카메라·가림 페이드) · Listen+클라 해시 일치 · 리눅스 시드 재현
- [ ] (후속) 킥 실제 차기 동작 · 봇 층간 이동 개선(2칸 낙하) · 자유 관전

## 진행 중 메모

- ⭐ 1번 시점부터 `stat unit` 을 켜고 개발한다 (GDD 7.4). "다 만들고 최적화"는 3주 프로젝트에 없다.
- 사망 처리 방향 (2026-07-30 사용자): 죽으면 **이동 불가 + 캐릭터 파괴 또는 풀 반납** — 부활 기능을 넣을 여지를 남긴다.
  사망·갇힘 연출(물방울 비주얼·애니메이션)은 에셋이 생긴 뒤 후속 Task 에서. 지금은 로그만 (StatusComponent TODO).
