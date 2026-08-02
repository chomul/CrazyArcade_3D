# Task 19 — CA3DGameInstance (2주차)

> 선행: Task 18 · 후행: 8인 접속 테스트, 데디 서버 배포
> 체크리스트: `mds/Checklists/19-CA3DGameInstance.md`

---

## ⏸️ 보류 상태 (2026-08-02 사용자 결정)

**구현은 끝났고 꺼져 있다.** 목표가 "게임 자체를 얼마나 빨리 만드느냐"이고 배포 여부가 아직
미정이라, 로비/세션을 **배포 시점으로 이월**했다. 접속은 IP 직접(`open <주소>:7777`) —
이미 데디 서버에서 봇 포함 매치 완주까지 검증된 경로다.

### 왜 켜기만 해서는 안 되나 — 엔진 블로커

Device ID 익명 로그인이 **UE 5.8 의 OSS v1 EOS 로는 Win64/Linux 데스크톱에서 불가능하다.**

| 근거 | 위치 |
|---|---|
| `UserLoginInfo` 는 *"Device ID feature login"* 에 **필수** | `Engine/Source/ThirdParty/EOSSDK/SDK/Include/eos_connect_types.h:81` |
| 그런데 그 필드를 채우는 코드 전체가 조건부 컴파일 안 | `OnlineSubsystemEOS/Private/UserManagerEOS.cpp:1137` (`#if ADD_USER_LOGIN_INFO`) |
| 그 스위치의 데스크톱 기본값이 꺼짐 (`virtual` — 콘솔 플랫폼 확장이 켜라고 만든 구조) | `OnlineSubsystemEOS.Build.cs:45` |

결과: `Options.UserLoginInfo = nullptr` → 서버가 `EOS_InvalidParameters` 로 거절.
**자격 증명 문제가 아니다** — 같은 프로세스에서 SDK 를 직접 호출하면 로그인이 성공한다
(`ca3d.EOSDeviceIdProbe` 로 재현 가능, ProductUserId 획득 확인).

### 다시 켤 때 할 일

1. 엔진 블로커 해소 — 둘 중 하나를 **먼저** 정한다:
   - **엔진 1줄 패치**: `OnlineSubsystemEOS.Build.cs:45` 의 `bAddUserLoginInfo` 를 `true` 로.
     엔진 포크가 되므로 `mds/build.md` 에 기록 필수. 리눅스 데디도 같은 엔진에서 크로스
     컴파일되므로 한 번으로 양쪽 해결. ⚠️ 켠 뒤 `DisplayName` 이 빈 문자열이면 또 거절될 수
     있다(`GetPlatformDisplayName`) — 그때는 `[OnlineSubsystem] NativePlatformService=NULL` 추가 검토
   - **OSSv2(`OnlineServicesEOS`) 이관**: 엔진 무수정이지만 세션 코드 대부분 재작성.
     자격 증명 ini 섹션도 다르다(`[OnlineServices.EOS]` 계열)
2. `Config/DefaultEngine.ini` → `[OnlineSubsystem] DefaultPlatformService=EOS`
3. `Config/DefaultGame.ini` → `bEnableEOS=true`
4. `Config/EOSCredentials.ini` 존재 확인 (`.gitignore` 대상 — 새 머신이면 `.example` 복사 후 값 입력)

### 이미 되어 있는 것 (재작업 불필요)

`UCA3DGameInstance`(로그인·방 생성/검색/참가/파기, 실패 통지 델리게이트) · 자격 증명 로더
(모듈 StartupModule 이 `EOSCredentials.ini` → `GEngineIni` 병합, `Saved/Config` 유출 없음) ·
`ca3d.*` 진단 콘솔 명령 · 테스트 `CrazyArcade3D.Framework.GameInstance`(자격 증명 유출 회귀 포함).

### 넷드라이버는 IP 그대로 (결정 완료)

EOS 를 켜더라도 전송은 `IpNetDriver` 다. `FOnlineSessionEOS::GetResolvedConnectString` 이
세션에 등록된 호스트 IP:포트를 그대로 돌려주므로 "EOS 로 발견 → IP 로 접속"이 성립한다.
공인 IP 를 가진 VPS 데디 서버에 정확히 맞는 구성이다.
**단 리슨 호스트를 인터넷에 여는 건 이 구성으로 불가능하다**(NAT 뒤 사설 IP 가 등록됨) —
그게 필요해지면 `SocketSubsystemEOS` + EOS P2P 넷드라이버를 별도 결정으로 검토한다.

## 클래스 구조

| 항목 | 내용 |
|---|---|
| 클래스명 | `UCA3DGameInstance` |
| 부모 클래스 | `UGameInstance` |
| 역할 | EOS 세션/로비 수명 관리 — Device ID 익명 인증, 방 생성/목록/참가, 준비→시작, 종료 후 로비 복귀 (GDD 6.3) |

## 생성 파일

- `Source/CrazyArcade3D/Framework/CA3DGameInstance.h/.cpp`
- (수정) `CrazyArcade3D.Build.cs` — `OnlineSubsystem`, `OnlineSubsystemEOS` 의존 추가
- (수정) `Config/DefaultEngine.ini` — EOS 아티팩트·OSS 설정

## 구현 명세

```cpp
// CA3DGameInstance.h
// EOS 세션 수명 관리. 게임플레이 로직 금지 — 접속·이동만.
UCLASS()
class UCA3DGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;   // Device ID 익명 로그인 시작

    // ─── 세션 (전부 비동기 — 완료 델리게이트로 결과 통지) ───
    void HostSession(int32 MaxPlayers);      // 방 생성 → L_Arena로 ServerTravel
    void FindSessions();                     // 공개방 목록 (GDD 6.3)
    void JoinSession(int32 SearchResultIndex);
    void LeaveToLobby();                     // 매치 종료 → 로비 복귀

    // UI(로비 위젯)가 구독하는 결과 델리게이트들.
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnSessionsFound, const TArray<FString>& /*요약*/);
    FOnSessionsFound OnSessionsFound;

private:
    void OnLoginComplete(int32 LocalUserNum, bool bOk, const FUniqueNetId& Id, const FString& Error);
    // ... Create/Find/Join 완료 핸들러
};
```

**주의**
- EOS 포털에서 제품/아티팩트/클라이언트 자격 증명 발급 필요 — **코드 밖 선행 작업**. 자격 증명 값은 사용자에게 요청한다.
- 난입 없음(GDD) — 매치 시작 후 `bAllowJoinInProgress=false`.
- 재접속 불가 전제 — 중간 상태 복원 로직을 만들지 않는다 (GDD 6.2).
- 데디 서버(리눅스) 세션 등록 흐름은 `mds/build.md`의 크로스 컴파일 확인과 함께 진행.

## 검증 원칙

- 공통 원칙 + 아래.
- 로컬 2클라: 방 생성 → 목록 검색 → 참가 → 같은 매치 진입.
- 매치 종료 → 로비 복귀 → 재차 방 생성 가능 (세션 정리 누수 없음).
- 데디 서버 세션에 클라 참가 (주 1회 VPS 검증 항목 — GDD 9장, 로컬만으로 완료 선언 금지).

## 응답 원칙

- 공통 원칙.
- EOS 자격 증명 등 **사용자 액션이 필요한 항목을 맨 앞에** 정리해 보고한다.
- 로컬 검증과 VPS 실환경 검증을 구분해 보고한다 (VPS 미실시면 미검증).
