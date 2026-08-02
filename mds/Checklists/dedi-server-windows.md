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

## 미검증 (입력이 필요해 헤드리스로는 불가)

- [ ] 실제 폭탄 설치 → 파괴 → **그리드 해시 일치** (1주차 게이트는 PIE 데디 모드로 통과함)
- [ ] 사망 복제 · 최후 1인 매치 종료 (Task 18)
- [ ] 데디에서 사망 시각 처리(액터 숨김)가 실행되지 않는가 (Task 27 잔여)

> 위 3건은 **Task 20(봇)** 이 들어오면 입력 없이 자동으로 검증된다 — 봇으로 인원을 채워
> 한 판을 완주시키면 파괴·사망·종료가 전부 서버에서 자발적으로 일어난다.
