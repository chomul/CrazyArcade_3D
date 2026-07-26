# Task 19 — CA3DGameInstance (2주차)

> 선행: Task 18 · 후행: 8인 접속 테스트, 데디 서버 배포
> 체크리스트: `mds/Checklists/19-CA3DGameInstance.md`

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
