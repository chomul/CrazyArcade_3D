# 패키징 빌드 실행 명령 모음 (로컬 멀티 테스트)

> 패키징(쿡·스테이징) 자체는 `mds/build.md` "클라이언트 패키징" 절 참조.
> 여기는 **이미 패키징된 산출물을 실행**하는 명령만 모았다. 실행 위치: 프로젝트 루트.

## 한 번에 (권장)

```powershell
.\run-packaged-test.ps1              # 서버 1 + 클라 2
.\run-packaged-test.ps1 -Clients 3   # 클라 3개
```

## 수동 실행

### ① 서버 먼저

```powershell
& "Saved\StagedBuilds\WindowsServer\CrazyArcade3D\Binaries\Win64\CrazyArcade3DServer.exe" L_Arena -log
```

- **`L_Arena` (맵 인자) 필수** — 데디는 `GameDefaultMap` 을 무시한다. 안 주면 엔진 Entry
  빈 맵으로 떠서 아무 일도 안 일어난다.
- **`-log` 필수** — 데디 서버는 기본이 완전 헤드리스라 이게 없으면 **창이 아예 안 뜬다**
  (더블클릭 실행이 조용히 백그라운드에 쌓이는 이유).
- 정상 기동 확인: 로그 창에 `IpNetDriver listening on port 7777` + `매치 시드 …`

### ② 클라 (서버 뜨고 몇 초 뒤, 원하는 수만큼)

```powershell
& "Saved\StagedBuilds\Windows\CrazyArcade3D\Binaries\Win64\CrazyArcade3D.exe" 127.0.0.1 -windowed -resx=1280 -resy=720
```

- 첫 인자 IP 로 바로 접속한다. 다른 PC 에서 붙이려면 서버 PC 의 LAN IP 로 교체
  (방화벽에서 UDP 7777 허용 필요).

## 뒷정리 / 문제 해결

```powershell
# 보이지 않게 쌓인 서버 프로세스 확인·정리 (더블클릭 실행의 흔적)
Get-Process CrazyArcade3DServer -ErrorAction SilentlyContinue
Stop-Process -Name CrazyArcade3DServer -Force
```

- 클라가 접속 못 하면: 서버 로그에 `listening on port 7777` 이 있는지부터.
  유령 서버가 포트를 물고 있으면 위로 정리 후 재시작.
- **탈주 테스트는 이 구성에서만 성립** — 클라 창 하나만 닫으면 된다.
  PIE 는 창 하나를 닫으면 세션 전체가 죽는다 (체크리스트 35).
