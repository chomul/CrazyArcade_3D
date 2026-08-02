#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "CA3DGameInstance.generated.h"

class FOnlineSessionSearch;
class FUniqueNetId;
class IOnlineSubsystem;

// 검색 결과 한 줄. 로비 위젯(Task 26)이 그대로 그릴 수 있는 최소 정보만 담는다.
//
// 왜 인덱스를 들고 다니나 — JoinSession 은 EOS 가 돌려준 FOnlineSessionSearchResult 원본이
// 있어야 한다. 그 타입은 UObject 가 아니라 UI 로 넘길 수 없으므로, GameInstance 가 검색 결과
// 배열을 보관하고 UI 는 인덱스만 되돌려준다.
USTRUCT(BlueprintType)
struct FCA3DSessionSummary
{
	GENERATED_BODY()

	// JoinSession(SearchResultIndex) 에 그대로 넣는 값.
	UPROPERTY(BlueprintReadOnly, Category="CA3D|Session")
	int32 Index = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="CA3D|Session")
	FString HostName;

	UPROPERTY(BlueprintReadOnly, Category="CA3D|Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category="CA3D|Session")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category="CA3D|Session")
	int32 PingMs = 0;

	// 로그·콘솔 명령 출력용 한 줄 요약.
	FString ToDisplayString() const;
};

// ─── UI(로비 위젯)가 구독하는 결과 델리게이트 ───
//
// 전부 "성공 여부 + 실패 사유"를 함께 넘긴다. 실패를 통지하지 않으면 로비가 영원히
// "검색 중" 으로 멈춘다 — EOS 는 실패가 로그 말고는 드러나지 않으므로 이 규약이 중요하다.
DECLARE_MULTICAST_DELEGATE_TwoParams(FCA3DOnLoginComplete, bool /*bSuccess*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCA3DOnSessionsFound, bool /*bSuccess*/, const TArray<FCA3DSessionSummary>& /*Sessions*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCA3DOnHostSessionComplete, bool /*bSuccess*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCA3DOnJoinSessionComplete, bool /*bSuccess*/, const FString& /*Error*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FCA3DOnLeftToLobby, bool /*bSuccess*/, const FString& /*Error*/);

// EOS 세션 수명 관리 (Task 19). Device ID 익명 인증 → 방 생성/목록/참가 → 로비 복귀.
//
// **게임플레이 로직 금지** — 승패·스탯·맵 규칙은 GameMode/GameState 소유다. 여기는
// "어느 프로세스에 붙을지"만 정한다. 그래야 매치 규칙을 바꿔도 접속 코드가 흔들리지 않는다.
//
// EOS 는 세션 발견·참가에만 쓴다. 전송은 여전히 IpNetDriver 다 (VPS 데디 서버가 배포 목표 —
// GDD 8장). GetResolvedConnectString 이 EOS 에 등록된 호스트 IP:포트를 돌려주므로
// ClientTravel 은 평범한 IP 접속이 된다.
UCLASS(Config=Game)
class CRAZYARCADE3D_API UCA3DGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// Device ID 익명 로그인 시작 + 세션 델리게이트 배선.
	virtual void Init() override;

	// 남아 있는 세션·델리게이트 정리. 안 하면 다음 실행에서 "이미 세션 있음" 으로 생성이 막힌다.
	virtual void Shutdown() override;

	// ─── 인증 (GDD 6.3 — Device ID 익명. Epic 계정 로그인 요구 없음) ───

	// 로컬 Device ID 자격 생성(없으면) → EOS Connect 로그인. Init 이 자동 호출한다.
	// 실패 후 재시도용으로 공개해 둔다 (콘솔 `ca3d.LoginDeviceId`).
	void LoginDeviceId();

	bool IsLoggedIn() const;

	// ─── 세션 (전부 비동기 — 완료 델리게이트로 결과 통지) ───

	// 방 생성 → 성공 시 아레나 맵으로 ServerTravel(?listen).
	// MaxPlayers <= 0 이면 설정값(DefaultMaxPlayers)을 쓴다.
	void HostSession(int32 MaxPlayers = 0);

	// 공개방 목록 (GDD 6.3).
	void FindSessions();

	// FindSessions 결과의 인덱스로 참가 → 성공 시 ClientTravel.
	//
	// 이름을 JoinSession 이 아니라 JoinFoundSession 으로 둔 이유 — UGameInstance 에 이미
	// 가상 함수 JoinSession(ULocalPlayer*, ...) 이 있어서 같은 이름을 쓰면 오버로드를 가린다
	// (C4263/C4264). 엔진이 부르는 쪽과 우리가 부르는 쪽이 갈라지면 진단이 매우 어렵다.
	void JoinFoundSession(int32 SearchResultIndex);

	// 매치 종료 → 세션 파기 후 로비 맵으로 복귀. 재접속 복원은 하지 않는다 (GDD 6.2).
	void LeaveToLobby();

	// 마지막 검색 결과 요약 — UI 가 델리게이트를 놓쳤을 때 다시 읽을 수 있게.
	const TArray<FCA3DSessionSummary>& GetLastFoundSessions() const { return LastFoundSessions; }

	FCA3DOnLoginComplete OnLoginComplete;
	FCA3DOnSessionsFound OnSessionsFound;
	FCA3DOnHostSessionComplete OnHostSessionComplete;
	FCA3DOnJoinSessionComplete OnJoinSessionComplete;
	FCA3DOnLeftToLobby OnLeftToLobby;

protected:
	// ─── 환경 설정 (Config/DefaultGame.ini) ───
	// 게임 규칙이 아니라 배포 환경이라 룰셋(UCA3DRuleSet)이 아니라 ini 에 둔다.

	// EOS 사용 여부. **기본 false — 2026-08-02 사용자 결정으로 로비를 배포 시점까지 미뤘다.**
	// 끈 상태에서는 로그인·세션을 아예 시도하지 않는다. 접속은 IP 직접(`open 1.2.3.4:7777`)이고,
	// 그건 이미 데디 서버에서 8인 매치까지 검증된 경로다.
	// 다시 켤 때 필요한 것은 `mds/Tasks/19-CA3DGameInstance.md` 의 "보류 상태" 절에 적어 뒀다
	// (엔진 쪽 Device ID 블로커가 먼저 해결돼야 한다 — 켜기만 하면 되는 게 아니다).
	UPROPERTY(Config)
	bool bEnableEOS = false;

	// 방 생성 후 ServerTravel 할 맵.
	UPROPERTY(Config)
	FString ArenaMapPath;

	// 매치 종료 후 돌아갈 맵. 비어 있으면 세션만 정리하고 트래블하지 않는다
	// (로비 맵은 UI Task 에서 만든다).
	UPROPERTY(Config)
	FString LobbyMapPath;

	// HostSession(0) 의 기본 정원 (GDD — 최대 8인).
	UPROPERTY(Config)
	int32 DefaultMaxPlayers = 8;

	// EOS 세션 버킷. 생성·검색이 같은 값이어야 서로 보인다. 지정하지 않으면 EOS OSS 가
	// 빌드 고유 ID 를 쓰는데, 그러면 에디터 빌드와 패키징 빌드가 서로를 못 본다.
	UPROPERTY(Config)
	FString SessionBucketId;

private:
	// ─── 로그인 내부 단계 ───

	// EOS_Connect_CreateDeviceId — 로컬 Device ID 자격이 없으면 만든다.
	// 이미 있으면 EOS_DuplicateNotAllowed 가 오는데 그것도 정상 경로다.
	void EnsureDeviceIdCreated();

	// EnsureDeviceIdCreated 완료 후 실제 OSS 로그인 호출.
	void StartOnlineLogin();

	void HandleLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);

	// ─── 세션 완료 핸들러 ───
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	// 결과 열거형(EOnJoinSessionCompleteResult)은 OSS 헤더에 있어 여기서 전방 선언이 불가능하다.
	// .cpp 에서 람다로 bool + 사유 문자열로 접어서 넘긴다 — 헤더가 OSS 를 끌고 오지 않게.
	void HandleJoinSessionComplete(FName SessionName, bool bWasSuccessful, const FString& ResultText);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// 실패를 한곳에서 통지 — 조용한 실패를 만들지 않기 위한 단일 경로.
	void FailHost(const FString& Error);
	void FailFind(const FString& Error);
	void FailJoin(const FString& Error);

	// PIE 인스턴스별 서브시스템까지 올바르게 골라준다 (Online::GetSubsystem).
	IOnlineSubsystem* GetOnlineSubsystem() const;

	// 등록해 둔 델리게이트를 모두 떼어낸다 (Shutdown·재시도 공용).
	void ClearOnlineDelegates();

	// 이 게임은 스플릿스크린이 없다 — 로컬 유저는 항상 0번.
	static constexpr int32 LocalUserNum = 0;

	// 진행 중 표시 — 같은 요청이 겹치면 EOS 콜백이 뒤엉킨다.
	bool bLoginInProgress = false;
	bool bHostInProgress = false;
	bool bFindInProgress = false;
	bool bJoinInProgress = false;

	// HostSession 이 요청한 정원. CreateSession 완료 후 ServerTravel 할 때는 이미 필요 없지만
	// 실패 로그에 남기면 원인 추적이 쉬워진다.
	int32 PendingMaxPlayers = 0;

	// EOS 가 돌려준 원본 검색 결과. JoinSession 이 인덱스로 참조한다.
	TSharedPtr<FOnlineSessionSearch> SessionSearch;

	TArray<FCA3DSessionSummary> LastFoundSessions;

	FDelegateHandle LoginCompleteHandle;
	FDelegateHandle CreateSessionCompleteHandle;
	FDelegateHandle FindSessionsCompleteHandle;
	FDelegateHandle JoinSessionCompleteHandle;
	FDelegateHandle DestroySessionCompleteHandle;

	friend class FCA3DGameInstanceTest; // 자동화 테스트가 설정값·요약 포맷을 직접 확인하기 위한 접근
};
