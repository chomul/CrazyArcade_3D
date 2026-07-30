# CrazyArcade3D

마인크래프트식 복셀 그리드 위에서 벌어지는 3D 크레이지 아케이드. 최대 8인 라스트맨 스탠딩.
**공중은 안전지대가 아니고, 발판만이 안전하다** — 폭발이 지형을 실시간으로 무너뜨린다.

솔로 개발 · 목표 2~3주 · UE 5.8 **소스 빌드** · 대부분 C++ · GAS 미사용
버전 관리는 **이중**: Diversion(사용자가 직접 관리) + git(Claude가 각 Task 후 커밋·push — 불변식 6).
리모트: `https://github.com/chomul/CrazyArcade_3D` (`origin/main`)

## 문서

작업 전에 해당 영역의 문서를 **먼저 읽는다.** 기획과 구조가 충돌하면 구조 설계서가 우선(더 나중에 확정됨).

| 파일 | 언제 읽나 |
|---|---|
| `mds/crazy-arcade-3d-architecture.md` | 코드 작성 전 — 클래스 설계·데이터 흐름·권한 매트릭스 |
| `mds/crazy-arcade-3d-gdd-v2.md` | 게임 규칙·아이템·맵·로드맵이 필요할 때 |
| `mds/tasks.md` | **매 작업 시작 시** — 현재 어디까지 했는지. 진행 상황은 여기서만 갱신 |
| `mds/build.md` | 빌드가 실패했을 때, 엔진·툴체인을 건드릴 때 |
| `mds/claude-context-rules.md` | 엔진 소스·빌드 로그·BP 를 다루거나 검색 범위를 정할 때 — 컨텍스트 절약 규칙 |

## Task 수행 절차

사용자가 Task 실행을 요청하면 (예: "2번 작업 해줘", "`FVoxelGrid` 구현해줘"):

1. `mds/tasks.md` 로 범위를 확인하고 구조 설계서의 해당 절을 읽는다.
2. **구현은 서브에이전트에 위임한다.** 서로 의존하지 않는 Task는 한 번에 병렬로 띄운다.
   위임 프롬프트에는 **Task 번호 · 대상 파일 경로 · 아래 불변식 · 폴더 의존 규칙 · 코딩 규칙**을 반드시 넣는다.
3. 결과를 받으면 **빌드로 검증**한다. 서브에이전트의 "완료" 보고를 그대로 믿지 않는다.
4. **`mds/tasks.md` 의 체크박스를 `[x]` 로 바꾸고 무엇을 만들었는지 한 줄 남긴다.**
   빌드가 통과하기 전에는 체크하지 않는다. 이 단계를 절대 빼먹지 않는다.
5. **Task 완료 보고 끝에 "에디터에서 할 일"을 정리해서 알려준다** — BP 서브클래스 생성,
   에셋(메시·머티리얼·데이터 에셋) 지정, 맵 배치, World Settings 연결 등 C++ 밖의 작업.
   각 Task 문서(`mds/Tasks/NN-*.md`)의 "연결 작업" 절이 그 목록이다. 없으면 "없음"이라고 명시한다.

---

## 빌드

```powershell
$bat  = "C:\UnrealEngine5.8\Engine\Build\BatchFiles\Build.bat"
$proj = "C:\Sung Unreal Project\CrazyArcade_3D\CrazyArcade3D.uproject"

& $bat CrazyArcade3DEditor Win64 Development -Project="$proj" -WaitMutex
& $bat CrazyArcade3DServer Win64 Development -Project="$proj" -WaitMutex   # 데디 서버

# 프로젝트 파일 재생성 — .h/.cpp 를 추가·삭제하면 반드시 먼저 실행
$dn  = "C:\UnrealEngine5.8\Engine\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe"
$dll = "C:\UnrealEngine5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll"
& $dn $dll -projectfiles -project="$proj" -game -progress
```

- 엔진은 **`C:\UnrealEngine5.8` (소스 빌드)**. 런처 버전(`C:\Program Files\Epic Games\UE_5.8`)은 서버 타깃을 못 만든다.
- **`UnrealBuildTool.exe` 를 직접 부르지 말 것** (.NET 버전 불일치로 `exit 150`). 위 두 명령만 쓴다.
- 그 밖의 빌드 함정·환경 정보는 `mds/build.md`.

---

## 구조

```
Source/CrazyArcade3D/
├── Voxel/       그리드 데이터 + 권한/리플리케이션 + HISM 렌더링
├── MapGen/      시드 기반 결정론적 생성 + 검증 (전부 순수 함수)
├── Gameplay/    Bomb/ Character/ Item/ SuddenDeath/
├── Framework/   GameMode·GameState·PlayerState·RuleSet
├── Core/        제네릭 오브젝트 풀
├── AI/          봇 (C++ FSM AIController)
└── UI/          HUD·위젯 C++ 베이스
```

`PublicIncludePaths` 에 모듈 루트가 등록되어 있으므로 **도메인 루트 기준 경로**를 쓴다.

```cpp
#include "Voxel/VoxelGrid.h"        // O
#include "../../Voxel/VoxelGrid.h"  // X
```

### 폴더 간 의존 규칙

```
MapGen    ──▶ Voxel
Voxel     ──▶ (없음)     ⬅ 게임 규칙을 몰라야 함
Gameplay  ──▶ Voxel, Core
Framework ──▶ 전부
UI        ──▶ Framework (읽기 전용)
Core      ──▶ (없음)
```

**`Voxel` 이 `Gameplay` 를 참조하는 순간 구조가 무너진다.** 지형은 "누가 왜 부쉈는지" 몰라야 하고,
그래야 맵 생성기·에디터 툴에서 재사용되며 `IVoxelRenderer` 교체(그리디 메싱 승격)가 가능하다.

---

## 반드시 지킬 불변식

이 5개는 구조 설계의 근간이다. 어기면 버그가 아니라 설계가 무너진다.

### 1. 서버·클라가 같은 함수를 통과한다

블록 파괴는 서버든 클라든 `AVoxelWorld::ApplyDestruction()` 단일 경로로만 들어간다. 경로가 갈라지면 반드시 어긋난다.

```
[서버] ServerDestroyBlocks → ApplyDestruction + Multicast
                                                    └─▶ [클라] ApplyDestruction
```

### 2. `ExplosionSubsystem::Propagate` 는 static 순수 함수다

부작용 없이 `FVoxelGrid` 를 읽어 `FExplosionResult` 만 반환한다. 이걸 지키면 공짜로 따라오는 것:

- **위험 구역 프리뷰 데칼**이 실제 폭발과 같은 함수를 호출 → 표시와 실제가 어긋날 수 없음
- 봇 AI가 "여기 놓으면 어디까지 터지나"를 부작용 없이 조회
- 유닛 테스트 가능

### 3. 예측 폭탄은 타이머를 돌리지 않는다

`APredictedBombVisual` 은 **시각 전용**. 타이머·판정·연쇄는 100% 서버의 `ABomb` 소유.
서버가 거부하면 이펙트만 지우면 되므로 상태 불일치가 원천적으로 불가능하다.

### 4. 맵 생성기 안에서는 정수 연산과 `FRandomStream` 만

`float` 연산 / `FMath::Rand()` / `TMap` 순회 / 액터 이터레이션 순서는 플랫폼·실행마다 다르다.
컨테이너는 `TArray` 만. 리눅스 서버 ↔ 윈도우 클라에서 시드 재현이 깨지면 진단이 매우 어렵다.

### 5. 1주차부터 권한을 분리한다

"나중에 멀티로 이전"은 하지 않는다. 처음부터:

```cpp
if (!HasAuthority()) return;          // 상태를 바꾸는 모든 함수 최상단
if (IsRunningDedicatedServer()) return;  // 시각만 만지는 모든 함수 최상단
```
### 6. 각 task 실행 후 내용을 git관리 작업도 진행.

---

## 언리얼 코딩 컨벤션

Epic 표준을 따른다. 이름은 **PascalCase**, 약어는 대문자 유지(`HISMVoxelRenderer`), 변수명에 타입을 넣지 않는다.

| 접두사 | 대상 | 예 |
|---|---|---|
| `U` / `A` | `UObject` 파생 / `AActor` 파생 | `UPoolSubsystem`, `ABomb` |
| `F` | 일반 struct·클래스 | `FVoxelGrid`, `FExplosionResult` |
| `I` / `E` / `T` | 인터페이스 / enum class / 템플릿 | `IVoxelRenderer`, `EBlockType`, `TArray` |
| `b` | bool | `bIsFalling` |

- **UObject 포인터는 반드시 `UPROPERTY()`**. 안 붙이면 GC가 수거해 간다. UE5이므로 raw 포인터 대신 **`TObjectPtr<>`**.
- 컨테이너는 `TArray`/`TMap`/`TSet`. **std 컨테이너·`std::string` 금지.** 문자열은 `FString`/`FName`/`FText`(UI 표시용).
- 함수명은 동사로. bool 반환은 `Is`/`Has`/`Can`. 서버 RPC는 `Server`, 멀티캐스트는 `Multicast` 접두사.
- **const correctness**: 안 바꾸는 멤버 함수는 `const`, 큰 타입은 `const T&` 로 전달.
- 헤더에는 **전방 선언** 우선, `#include` 는 `.cpp` 에. 헤더 최소화가 UE 빌드 시간을 좌우한다.
- `override` 명시, `NULL` 대신 `nullptr`, 계약 위반은 `check()`, 복구 가능한 이상은 `ensure()`.

## 코딩 규칙 (이 프로젝트 고유)

- **C++ 베이스 + BP 서브클래스.** BP에 로직 금지 — 에셋 참조·수치 오버라이드·이펙트 재생만.
- **튜닝 값은 전부 `UCA3DRuleSet`(DataAsset)에.** 코드에 매직 넘버 금지.
  룰셋은 GameMode가 소유하되 **GameState에 에셋 포인터를 복제**해야 클라 프리뷰가 맞는다.
- **로그는 `LogCA3D` 카테고리** (`CrazyArcade3D.h`).
- **풀링은 클라 시각 요소만.** 서버 권한 상태를 가진 액터(`ABomb`)는 풀링하지 않는다 — 재사용 시 상태 오염 위험이 이득보다 크다.
- `stat unit` 을 켜고 개발한다. "다 만들고 최적화"는 3주 프로젝트에 없다.
- **컨텍스트 절약** (`mds/claude-context-rules.md` 상세): 엔진 소스는 grep 으로 필요한 부분만 ·
  빌드 에러는 파일+라인+코드만 · BP 는 노드 흐름 요약만 · 검색·분석 시 빌드 산출물
  (`Binaries/`·`Intermediate/`·`Saved/`·`DerivedDataCache/`·`.vs/` 등)과 `*.uasset`·`*.umap` 자동 제외.

---

## 알려진 함정

이 영역을 건드리면 먼저 확인할 것.

| 영역 | 함정 |
|---|---|
| 리플리케이션 순서 | `OnRep_Seed` 보다 파괴 Multicast가 **먼저 도착할 수 있음.** 클라는 그리드 초기화 전에 받은 파괴 셀을 큐에 쌓아두고 `OnRep_Seed` 직후 flush |
| HISM | `RemoveInstance` 는 마지막 인덱스를 그 자리로 당겨옴. 셀→인스턴스 인덱스 맵을 반드시 함께 갱신. 안 하면 엉뚱한 블록이 사라짐 |
| 데디 서버 | 나이아가라·사운드·머티리얼·BP가 서버에서 돌지 않는지 확인 (GDD 7.4) |
| 기본 맵 설정 | `Config/DefaultEngine.ini` 의 `GameDefaultMap` 은 **의도적으로 주석 처리됨.** 없는 에셋을 가리키면 에디터가 첫 실행부터 에러. 맵 제작 후 켤 것 |

---

## 아직 안 정한 값

구조가 아니라 **수치**라서 미룬 것들. **임의로 정하지 말고 물어볼 것.**

- 셀 크기 100 · 이동속도 4칸/초 — **현행 유지**(2026-07-30). 파생 값은 전부 계수라 언제든 바꿀 수 있게 유지
- 바닥 블록 파괴 허용 여부 / 스폰 무적 시간 / 아이템 스택 상한

확정된 값: **점프 높이 = 1칸**(2칸은 못 오름) · **점프 이동 거리 = 지상의 0.7배**(공중 수평 속도만 줄임 — 체공 시간 불변) ·
**WASD = 카메라 기준** · **이동 가감속 = 0.05초**(공중 포함, AirControl 1.0 — 룰셋의 시간 값에서 파생) ·
**카메라 붐 컬리전 off**(벽에서 확대되는 문제 — 가림은 3주차 디더 페이드) ·
**공중 폭탄 설치 = 바로 아래로 내려 찾은 셀, 피격 = 폭발 범위를 셀 단위로**(잠정 — 체감 후 재확인)

---

## 이 문서 운영 규약

- 같은 지적을 **두 번** 받으면 여기에 규칙으로 추가한다.
- **200줄을 넘기지 않는다.** 넘으면 상세 설명을 `mds/` 로 내리고 여기엔 한 줄 요약 + 경로만 남긴다.
- 진행 상황·완료 체크는 `mds/tasks.md` 에만 쓴다. 이 파일은 "항상 참인 규칙"만 담는다.