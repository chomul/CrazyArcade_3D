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

## 리눅스 데디 서버 (2주차)

Win64 서버 빌드는 소스 엔진으로 바로 된다. **리눅스** 타깃은 추가로 크로스 컴파일 툴체인
(`-v23 clang`)을 설치하고 `LINUX_MULTIARCH_ROOT` 환경변수를 설정해야 한다 — 2주차 착수 시 확인.

## 환경

| 항목 | 값 |
|---|---|
| 엔진 | `C:\UnrealEngine5.8` (소스 빌드, UE5 브랜치, 5.8.0) |
| VS | 2022 Community 17.14, MSVC 14.44 |
| Win SDK | 10.0.22621 / 10.0.26100 |
| 하드웨어 | 12 physical / 24 logical cores, 31GB RAM |
| 버전 관리 | Diversion (git 아님) |
