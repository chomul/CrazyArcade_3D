# Checklist — Windows 데디 서버 실전 확인

> PIE 의 "Play as Dedicated Server" 가 아니라 **별도 프로세스의 서버 exe** 에 클라를 붙인다.
> 재현 스크립트: 이 문서 아래 "실행 절차". 절차 상세·크래시 원인은 `mds/build.md`.

## 전제 — 언쿡 실행은 불가 (2026-08-02 확인)

`CrazyArcade3DServer.exe` 에 `.uproject` 를 넘기면 엔진 기본 에셋을 읽다 **시작 중 크래시**한다.
언쿡 패키지 헤더의 `PersistentGuid`(16B)를 `WITH_EDITORONLY_DATA=0` 인 서버 타깃이 건너뛰어
스트림이 어긋나는 엔진 구조 문제 — 우리 코드가 아니다. **쿡·스테이징이 전제다.**

## 실행 절차

```powershell
$uat  = "C:\UnrealEngine5.8\Engine\Build\BatchFiles\RunUAT.bat"
$proj = "C:\Sung Unreal Project\CrazyArcade_3D\CrazyArcade3D.uproject"

# 1) 쿡 + 스테이징 (2026-08-02 실측 2분 55초)
& $uat BuildCookRun -project="$proj" -noP4 -nodebuginfo -utf8output `
    -platform=Win64 -server -serverconfig=Development -noclient -cook -build -stage -pak

# 2) 서버 기동
$srv = "C:\Sung Unreal Project\CrazyArcade_3D\Saved\StagedBuilds\WindowsServer\CrazyArcade3D\Binaries\Win64\CrazyArcade3DServer.exe"
& $srv /Game/Maps/L_Arena -log -port=7777 -LogCmds="LogCA3D Verbose"

# 3) 클라 접속 — 언쿡(에디터 바이너리)이어도 붙는다. 같은 빌드라 레이아웃이 같다
& "C:\UnrealEngine5.8\Engine\Binaries\Win64\UnrealEditor.exe" "$proj" 127.0.0.1:7777 -game -log -windowed
```

서버 로그 위치는 **스테이징 트리 안**이다 (프로젝트 `Saved/` 가 아니다):
`Saved/StagedBuilds/WindowsServer/CrazyArcade3D/Saved/Logs/CrazyArcade3D.log`

## 검증 (2026-08-02 · 서버 1 + 헤드리스 클라 2, 30초 관찰)

- [x] 서버 exe 가 크래시 없이 맵을 열고 리스닝 — `IpNetDriver listening on port 7777`
- [x] `NetServerMaxTickRate=30` 이 쿡된 서버에도 적용 — `up for play (max tick rate 30)`
- [x] 클라 2개 접속 성공 — 양쪽 `Welcomed by server (Level: /Game/Maps/L_Arena, Game: BP_CA3DGameMode_C)`
- [x] **참가자 배정이 순서대로** — `총 1명, ColorIndex 0` → `총 2명, ColorIndex 1` (Task 18 배선이 실전에서 동작)
- [x] **시드 복제 → 클라가 같은 지형을 만든다** — 두 클라 모두 `총 셀 1764 / 솔리드 756 / 표면 인스턴스 756`
- [x] 양쪽 클라가 상대 캐릭터까지 스폰 (각 로그에 캐릭터 2개 CMC 튜닝)
- [x] 서버·클라 로그에 **Warning·Error 0건**

### GDD 7.4 — 데디에서 시각·사운드가 도는가

- [x] 오디오 미초기화 — `Audio Device Manager not initializing due to all audio being disabled`
- [x] RHI = Null (렌더링 없음)
- [x] 셰이더 컴파일 0건 · `LogMaterial` 0건 · `LogParticle` 0건 · `LogSlate` 0건
- [x] Niagara 는 **플러그인 마운트 로그만** — 실행·틱 기록 없음

## 봇 투입 후 추가 검증 (2026-08-02 · Task 20 · `ca3d.BotFill 4` + 관전 클라 1, 사람 입력 0)

- [x] 실제 폭탄 설치 → 파괴 → **서버·클라 그리드 해시 일치** (솔리드 수가 같은 23지점 전부 일치, 불일치 0)
- [x] 사망 복제 · **최후 1인 매치 종료** — 공동 4등 → 공동 3등(2명 동시) → 공동 2등 → 우승자 확정

## ⛔ 이 과정에서 드러난 데디 전용 버그 2건 (둘 다 수정 완료)

봇이 없었으면 못 찾았다. 사람 클라가 가만히 서 있으면 두 버그 모두 증상이 보이지 않는다.

### 1. 데디 서버에 지형 컬리전이 없었다

`AVoxelWorld::BeginPlay` 가 "시각 전용이니 데디에서는 파괴한다"(불변식 5)며 HISM 컴포넌트를 지웠는데,
**그 HISM 인스턴스가 블록의 유일한 물리 형상**이었다. `BuildFromGrid`/`RemoveBlock` 안에도 같은 가드가 있었다.

| | 리슨 서버 | 데디 (수정 전) | 데디 (수정 후) |
|---|---|---|---|
| 이동 모드 | `MOVE_Walking` | **`MOVE_Falling` (항상)** | `MOVE_Walking` |
| 액터 Z | 190.1 고정 | 95~229 진동(바닥면 아래 침투) | 190.2 고정 |
| 지상 판정 비율 | 정상 | **3.6%** | 58% |

서버가 이동 권한을 가지므로 이건 게임 전체가 성립하지 않는 상태였다.
**PIE 로는 영원히 못 잡는다** — PIE 데디 모드는 에디터 프로세스라 `IsRunningDedicatedServer()` 가 false다.

### 2. 중간 접속 클라의 지형이 서버와 달랐다

Multicast RPC 는 **그 순간 접속해 있는 클라에게만** 간다. 그래서 이력이 없으면 늦게 들어온 클라는
시드로 만든 원본 지형만 갖는다. 실측: 서버가 `#11`(솔리드 740)을 적용한 **1ms 뒤** 접속 클라가
자기 `#1`(솔리드 **755**)을 적용 — 이미 부서진 15칸을 멀쩡한 벽으로 보고 있었다.
(서버는 통과시키는데 클라는 막히고, 그 반대도 난다.)

수정: `AVoxelWorld::DestroyedCells` 복제 배열에 이력을 쌓고 `OnRep` 에서 **미적용분만** 따라잡는다.
프로퍼티 복제는 접속 시점에 현재 값 전체를 보내주므로 이력을 들고 있는 것만으로 해결된다.
회귀 테스트 `CrazyArcade3D.Voxel.VoxelWorld` 에 "중간 접속 따라잡기 + 재수신 시 이중 적용 없음" 추가.

## 미검증

- [ ] 데디에서 사망 시각 처리(액터 숨김)가 실행되지 않는가 (Task 27 잔여) — 로그로는 확인 불가
