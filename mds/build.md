# 빌드 환경 상세

`CLAUDE.md` 의 빌드 명령이 실패했을 때, 또는 엔진·툴체인을 건드릴 때 읽는다.

## ⚠️ 엔진은 소스 빌드를 쓴다 — 런처 버전 아님

이 머신에는 UE 5.8이 **두 개** 있다.

| 경로 | 종류 | Server 타깃 |
|---|---|---|
| `C:\UnrealEngine5.8` | **소스 빌드 ← 이걸 사용** | ✅ 지원 |
| `C:\Program Files\Epic Games\UE_5.8` | 런처 바이너리 | ❌ 미지원 |

런처 바이너리로 서버 타깃을 빌드하면 이렇게 실패한다:

```
Server targets are not currently supported from this engine distribution.
```

`.uproject` 의 `EngineAssociation` 은 소스 빌드 GUID `{A89AD46F-4322-FE9A-C3F3-2FA167BBBE36}`
(레지스트리 `HKCU:\Software\Epic Games\Unreal Engine\Builds` 에 등록됨)로 지정되어 있다. **바꾸지 말 것.**

## ⚠️ UnrealBuildTool.exe 를 직접 부르지 말 것

UE 5.8 UBT는 .NET 10을 요구하는데 이 머신 시스템에는 9까지만 있어 `exit 150` 으로 실패한다.
엔진이 .NET 10을 번들로 갖고 있으므로 **설치할 필요 없다.**

- **빌드** → `Build.bat` 이 번들 dotnet 을 알아서 설정해 준다.
- **프로젝트 파일 재생성** → 엔진 번들 `dotnet.exe` 로 `UnrealBuildTool.dll` 을 실행한다.

두 명령 모두 `CLAUDE.md` 빌드 절에 있다.

### 프로젝트 파일 재생성 시 주의

- 소스 엔진이므로 `-rocket` 을 **붙이지 않는다.**
- `Visual Studio 2022 does not support .NET 10.0 C# projects` 경고가 뜨지만
  **C++ 빌드에는 영향이 없다** (Automation C# 프로젝트만 솔루션에서 빠짐). 무시할 것.

### 엔진을 바꾼 뒤에는

`Intermediate/`, `Binaries/`, `.vs/`, `*.sln`, `*.slnx` 를 지우고 프로젝트 파일을 재생성한다.
전부 재생성 가능한 빌드 아티팩트다.

## ⚠️ 번역 단위 병합 빌드 — 무명 네임스페이스 이름 충돌은 커밋 후에야 드러난다

UBT 는 빌드 속도를 위해 여러 .cpp 를 `Module.*.cpp` **한 번역 단위로 합쳐서** 컴파일한다.
그러면 파일마다 만든 **무명 네임스페이스가 병합**되어, 다른 .cpp 와 같은 이름의 static/헬퍼 함수가
있으면 C2084 로 터진다.

함정은 **적응형 병합**: `git status` 기준 "수정 중인 파일"은 병합에서 빠져 단독 컴파일된다.
즉 작업 중에는 빌드가 통과하고, **커밋해서 워킹 트리가 깨끗해진 순간** 병합에 합류하며 실패한다
(2026-07-30 PredictedBombVisualTests.cpp ↔ BombTests.cpp 헬퍼 충돌로 실제 발생).

- **규칙**: .cpp 로컬 헬퍼라도 이름은 모듈 전체에서 고유하게 (테스트 파일은 접두사 권장, 예: `Pbv~`).
- **검증**: 커밋 전 `-ForceUnity -DisableAdaptiveUnity` 를 빌드 명령에 붙이면 병합 경로를 강제해 미리 잡는다.

> 이 두 옵션 이름의 `Unity` 는 **UBT 가 이 병합 기법을 부르는 이름**일 뿐이다.
> 게임 엔진 Unity 와는 아무 관계가 없다 (옵션 이름이라 우리가 바꿀 수 없다).

## ⚠️ 데디 서버 exe 는 **쿡된 콘텐츠에서만 돈다** — 언쿡 실행은 즉시 크래시

`CrazyArcade3DServer.exe` 에 `.uproject` 를 넘겨 언쿡으로 띄우면 **엔진 기본 에셋을 읽다가
시작 도중 죽는다** (2026-08-02 확인).

```
Assertion failed: ReaderPos + Num <= ReaderSize  [BufferReader.h:52]
  FBufferReaderBase::Serialize → operator<<(FString) → operator<<(FEngineVersion)
  → FPackageFileSummary::operator<< → FAsyncArchive::ReadCallback
```

**우리 코드 문제가 아니다.** 패키지 헤더 파서가 에디터 전용 필드를 조건부 컴파일로 다룬다:

```cpp
// PackageFileSummary.cpp:354
#if WITH_EDITORONLY_DATA
    if (!BaseArchive.IsFilterEditorOnly()) { Record << Sum.PersistentGuid; }  // 16바이트
#endif
```

언쿡 패키지에는 이 `PersistentGuid` 가 **파일에 들어 있는데**, 서버 타깃은 `WITH_EDITORONLY_DATA=0`
이라 그 블록이 통째로 사라진다 → 16바이트를 안 읽고 지나가 **스트림이 어긋난다** → 바로 뒤
`GenerationCount` 가 GUID 조각을 읽어 쓰레기 값이 되고, 이어지는 엔진 버전 문자열의 길이가
버퍼를 넘겨 assert. `s.MaxPackageSummarySize` 를 올려도 소용없다 — 그 초기화 함수(`AsyncLoading.cpp:252`)
자체가 `#if WITH_EDITORONLY_DATA` 라 서버에서는 항상 하드코딩 8192다.

**해법: 쿡·스테이징 후 실행한다.**

```powershell
$uat  = "C:\UnrealEngine5.8\Engine\Build\BatchFiles\RunUAT.bat"
$proj = "C:\Sung Unreal Project\CrazyArcade_3D\CrazyArcade3D.uproject"
& $uat BuildCookRun -project="$proj" -noP4 -nodebuginfo -utf8output `
    -platform=Win64 -server -serverconfig=Development -noclient -cook -build -stage -pak
# 산출물: Saved/StagedBuilds/WindowsServer/CrazyArcade3D/Binaries/Win64/CrazyArcade3DServer.exe
```

클라는 언쿡(에디터 바이너리 `-game`)이어도 붙는다 — 같은 빌드라 클래스·프로퍼티 레이아웃이 같다.
리눅스 배포도 결국 같은 절차를 타므로(플랫폼만 교체) 이 경험은 그대로 재사용된다.

## 클라이언트 패키징 + 로컬 멀티 테스트 (2026-08-16 확립)

서버와 클라는 **타깃이 달라 따로 패키징**한다 (쿡도 `WindowsServer`/`Windows` 플랫폼별 별도).
`-map=L_Arena` 로 쿡 범위를 한정한다 — 에셋 팩 쇼케이스 맵 12개를 전부 쿡하면 낭비다.

```powershell
$uat  = "C:\UnrealEngine5.8\Engine\Build\BatchFiles\RunUAT.bat"
$proj = "C:\Sung Unreal Project\CrazyArcade_3D\CrazyArcade3D.uproject"
# 서버 (증분이면 ~1분)
& $uat BuildCookRun -project="$proj" -noP4 -nodebuginfo -utf8output `
    -platform=Win64 -server -serverconfig=Development -noclient -cook -build -stage -pak -map=L_Arena
# 클라 (첫 쿡 ~27분 — 셰이더. 이후는 증분)
& $uat BuildCookRun -project="$proj" -noP4 -nodebuginfo -utf8output `
    -platform=Win64 -clientconfig=Development -cook -build -stage -pak -map=L_Arena
# 산출물: Saved/StagedBuilds/Windows/CrazyArcade3D/Binaries/Win64/CrazyArcade3D.exe
#         콘텐츠는 .pak 이 아니라 .ucas/.utoc(IoStore)에 들어간다 — pak 10MB 만 보고 놀라지 말 것
```

- ⚠️ **데디 exe 는 `GameDefaultMap` 을 쓰지 않는다** — 맵을 첫 인자로 줘야 한다
  (`CrazyArcade3DServer.exe L_Arena -log`). 안 주면 엔진 `Entry` 맵으로 떠서 아무 일도 안 일어난다.
- 클라는 IP 를 첫 인자로 주면 바로 붙는다: `CrazyArcade3D.exe 127.0.0.1 -windowed -resx=1280 -resy=720`
- 한 번에 띄우기: 프로젝트 루트 **`run-packaged-test.ps1`** (서버 1 + 클라 N).
  **탈주 테스트는 이 구성에서만 성립한다** — PIE 는 창 하나를 닫으면 세션 전체가 죽는다 (체크리스트 35).

## 리눅스 데디 서버 (클라우드 배포 시점으로 이월 — 2026-07-30)

Win64 서버 빌드는 소스 엔진으로 바로 되고, **개발·테스트는 이걸로 충분하다.**
리눅스가 필요한 이유는 **배포 비용 하나**다 — Vultr·DigitalOcean·Linode 같은 시간당 과금 VPS 가
리눅스 기준이고 Windows 인스턴스는 라이선스가 얹힌다 (GDD 8장 도쿄 리전 계획).

착수 시 할 일: 크로스 컴파일 툴체인(`-v23 clang`) 설치 + `LINUX_MULTIARCH_ROOT` 환경변수 설정.
**코드 변경은 필요 없다** — 같은 소스가 그대로 크로스 컴파일된다.

## 환경

| 항목 | 값 |
|---|---|
| 엔진 | `C:\UnrealEngine5.8` (소스 빌드, UE5 브랜치, 5.8.0) |
| VS | 2022 Community 17.14, MSVC 14.44 |
| Win SDK | 10.0.22621 / 10.0.26100 |
| 하드웨어 | 12 physical / 24 logical cores, 31GB RAM |
| 버전 관리 | Diversion (git 아님) |
