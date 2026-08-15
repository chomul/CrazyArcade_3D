# 패키징 산출물 로컬 테스트 — 데디 서버 1 + 클라 N (기본 2)
# 사용법:  .\run-packaged-test.ps1          (클라 2개)
#          .\run-packaged-test.ps1 -Clients 3
# 탈주 테스트: 클라 창 **하나만** 닫으면 된다 — PIE 와 달리 프로세스가 분리돼 있다.
param([int]$Clients = 2)

$Root   = Split-Path -Parent $MyInvocation.MyCommand.Path
$Server = Join-Path $Root "Saved\StagedBuilds\WindowsServer\CrazyArcade3D\Binaries\Win64\CrazyArcade3DServer.exe"
$Client = Join-Path $Root "Saved\StagedBuilds\Windows\CrazyArcade3D\Binaries\Win64\CrazyArcade3D.exe"

if (-not (Test-Path $Server)) { Write-Error "서버 스테이징 없음 — mds/build.md 패키징 절 참조"; exit 1 }
if (-not (Test-Path $Client)) { Write-Error "클라 스테이징 없음 — mds/build.md 패키징 절 참조"; exit 1 }

# 데디는 GameDefaultMap 을 쓰지 않는다 — 맵을 인자로 줘야 한다 (없으면 엔진 Entry 맵으로 뜬다)
Start-Process $Server -ArgumentList "L_Arena", "-log"
Start-Sleep -Seconds 3   # 리스닝(7777) 준비 여유

for ($i = 0; $i -lt $Clients; $i++) {
    Start-Process $Client -ArgumentList "127.0.0.1", "-windowed", "-resx=1280", "-resy=720"
    Start-Sleep -Seconds 1
}
