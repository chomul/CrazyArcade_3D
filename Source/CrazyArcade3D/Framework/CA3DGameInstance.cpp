#include "Framework/CA3DGameInstance.h"

#include "CrazyArcade3D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "OnlineSubsystem.h"
#include "OnlineSubsystemNames.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Online/CoreOnline.h"
#include "Online/OnlineSessionNames.h" // SETTING_MAPNAME 등 표준 세션 키

#if WITH_EOS_SDK
	#include "EOSShared.h"             // LexToString(EOS_EResult)
	#include "IEOSSDKManager.h"
	#include "eos_connect.h"
	#include "eos_sdk.h"
#endif

// EOS 세션 버킷 커스텀 키. 엔진 헤더
// (OnlineSubsystemEOS/Public/OnlineSubsystemEOSTypesPublic.h) 의
// OSSEOS_BUCKET_ID_ATTRIBUTE_KEY 와 반드시 같은 문자열이어야 한다.
// 문자열로 두는 이유 — 이 키 하나 때문에 OnlineSubsystemEOS 모듈 의존을 공개로 끌어올릴
// 필요가 없다. 값이 어긋나면 "방은 만들어지는데 검색에 안 잡히는" 증상으로 드러난다.
static const FName CA3DBucketIdKey = FName(TEXT("OSSEOS_BUCKET_ID_ATTRIBUTE_KEY"));

// 방 목록에 보여줄 호스트 이름을 실어 나르는 세션 속성 키.
static const FName CA3DHostNameKey = FName(TEXT("CA3D_HOSTNAME"));

FString FCA3DSessionSummary::ToDisplayString() const
{
	return FString::Printf(TEXT("[%d] %s  %d/%d  %dms"), Index, *HostName, CurrentPlayers, MaxPlayers, PingMs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Device ID 자격 생성 (EOS SDK 직접 호출)
//
// 왜 SDK 를 직접 부르나 — OnlineSubsystemEOS 는 EOS_Connect_Login 까지만 감싸고
// EOS_Connect_CreateDeviceId 는 노출하지 않는다. Device ID 자격이 로컬에 없으면
// 로그인은 EOS_NotFound 로 떨어지므로 첫 실행에서 반드시 한 번 만들어야 한다.
// 이미 있으면 EOS_DuplicateNotAllowed 가 오는데 그것도 정상 경로다.
// ─────────────────────────────────────────────────────────────────────────────
#if WITH_EOS_SDK
namespace
{
	// 콜백이 돌아올 때 GameInstance 가 이미 사라졌을 수 있다 (레벨 전환·에디터 종료).
	// 약참조로 들고 있다가 살아 있을 때만 다음 단계로 넘어간다.
	struct FCA3DCreateDeviceIdContext
	{
		TWeakObjectPtr<UCA3DGameInstance> Owner;
	};

	EOS_HConnect CA3DGetConnectHandle()
	{
		IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
		if (SDKManager == nullptr || !SDKManager->IsInitialized())
		{
			return nullptr;
		}

		// OSS EOS 가 Init 에서 만든 플랫폼이 유일하게 활성 상태다.
		TArray<IEOSPlatformHandlePtr> Platforms = SDKManager->GetActivePlatforms();
		if (Platforms.Num() == 0 || !Platforms[0].IsValid())
		{
			return nullptr;
		}

		return EOS_Platform_GetConnectInterface(*Platforms[0]);
	}
}
#endif // WITH_EOS_SDK

void UCA3DGameInstance::Init()
{
	Super::Init();

	if (ArenaMapPath.IsEmpty())
	{
		ArenaMapPath = TEXT("/Game/Maps/L_Arena");
	}
	if (SessionBucketId.IsEmpty())
	{
		SessionBucketId = TEXT("CrazyArcade3D");
	}

	if (!bEnableEOS)
	{
		// 보류 상태 (2026-08-02 사용자 결정). 로그인·세션을 시도조차 하지 않는다 —
		// 실패한 로그인을 매 실행 재시도하면 로그가 에러로 더럽혀져 진짜 문제를 못 본다.
		// 접속은 IP 직접(`open <주소>:7777`)이고 그 경로는 이미 검증돼 있다.
		UE_LOG(LogCA3D, Log,
			TEXT("[GameInstance] EOS 비활성 (bEnableEOS=false) — IP 직접 접속 모드. 로비는 배포 시점으로 이월"));
		return;
	}

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] Init — EOS 로그인 시작 (Device ID 익명)"));
	LoginDeviceId();
}

void UCA3DGameInstance::Shutdown()
{
	// 세션을 남긴 채 프로세스가 죽으면 EOS 쪽에 유령 방이 남아 다음 검색을 오염시킨다.
	if (IOnlineSubsystem* OSS = GetOnlineSubsystem())
	{
		if (IOnlineSessionPtr Sessions = OSS->GetSessionInterface())
		{
			if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
			{
				UE_LOG(LogCA3D, Log, TEXT("[GameInstance] Shutdown — 남은 세션 파기"));
				Sessions->DestroySession(NAME_GameSession);
			}
		}
	}

	ClearOnlineDelegates();
	SessionSearch.Reset();

	Super::Shutdown();
}

IOnlineSubsystem* UCA3DGameInstance::GetOnlineSubsystem() const
{
	// PIE 는 인스턴스별로 이름이 다른 서브시스템을 쓴다. Online::GetSubsystem 이
	// 월드에서 그 이름을 뽑아준다 (월드가 없으면 전역 인스턴스로 폴백).
	return Online::GetSubsystem(GetWorld(), EOS_SUBSYSTEM);
}

// ─────────────────────────────────────────────────────────────────────────────
// 인증
// ─────────────────────────────────────────────────────────────────────────────

bool UCA3DGameInstance::IsLoggedIn() const
{
	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	if (OSS == nullptr)
	{
		return false;
	}

	IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
	return Identity.IsValid() && Identity->GetLoginStatus(LocalUserNum) == ELoginStatus::LoggedIn;
}

void UCA3DGameInstance::LoginDeviceId()
{
	if (bLoginInProgress)
	{
		UE_LOG(LogCA3D, Verbose, TEXT("[GameInstance] 로그인이 이미 진행 중 — 중복 요청 무시"));
		return;
	}

	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	if (OSS == nullptr)
	{
		// 자격 증명이 없으면 EOS 서브시스템 자체가 뜨지 않는다. 오프라인 개발은 계속 가능해야
		// 하므로 에러가 아니라 경고로 남기고 통지만 한다.
		const FString Error = TEXT("EOS 온라인 서브시스템 없음 (Config/EOSCredentials.ini 또는 플러그인 확인)");
		UE_LOG(LogCA3D, Warning, TEXT("[GameInstance] 로그인 불가 — %s"), *Error);
		OnLoginComplete.Broadcast(false, Error);
		return;
	}

	IOnlineIdentityPtr Identity = OSS->GetIdentityInterface();
	if (!Identity.IsValid())
	{
		const FString Error = TEXT("EOS Identity 인터페이스 없음");
		UE_LOG(LogCA3D, Warning, TEXT("[GameInstance] 로그인 불가 — %s"), *Error);
		OnLoginComplete.Broadcast(false, Error);
		return;
	}

	if (Identity->GetLoginStatus(LocalUserNum) == ELoginStatus::LoggedIn)
	{
		UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 이미 로그인됨 — 재로그인 생략"));
		OnLoginComplete.Broadcast(true, FString());
		return;
	}

	bLoginInProgress = true;
	LoginCompleteHandle = Identity->AddOnLoginCompleteDelegate_Handle(LocalUserNum,
		FOnLoginCompleteDelegate::CreateUObject(this, &UCA3DGameInstance::HandleLoginComplete));

	EnsureDeviceIdCreated();
}

void UCA3DGameInstance::EnsureDeviceIdCreated()
{
#if WITH_EOS_SDK
	EOS_HConnect ConnectHandle = CA3DGetConnectHandle();
	if (ConnectHandle == nullptr)
	{
		UE_LOG(LogCA3D, Warning,
			TEXT("[GameInstance] EOS 플랫폼 핸들 없음 — Device ID 생성을 건너뛰고 바로 로그인 시도"));
		StartOnlineLogin();
		return;
	}

	FCA3DCreateDeviceIdContext* Context = new FCA3DCreateDeviceIdContext{ this };

	// DeviceModel 은 포털의 계정 연결 관리에서 기기를 구분하기 위한 자유 문자열이다 (필수 필드).
	const FString DeviceModel = FString::Printf(TEXT("PC %s"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
	const auto DeviceModelUtf8 = StringCast<UTF8CHAR>(*DeviceModel);

	EOS_Connect_CreateDeviceIdOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = (const char*)DeviceModelUtf8.Get();

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] EOS_Connect_CreateDeviceId 호출 (DeviceModel=%s)"), *DeviceModel);

	EOS_Connect_CreateDeviceId(ConnectHandle, &Options, Context,
		[](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
		{
			TUniquePtr<FCA3DCreateDeviceIdContext> Owned(static_cast<FCA3DCreateDeviceIdContext*>(Data->ClientData));

			// 이미 있으면 DuplicateNotAllowed — 두 번째 실행부터는 항상 이쪽이다.
			const bool bUsable = Data->ResultCode == EOS_EResult::EOS_Success
				|| Data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed;

			UE_LOG(LogCA3D, Log, TEXT("[GameInstance] CreateDeviceId 결과 = %s%s"),
				*LexToString(Data->ResultCode), bUsable ? TEXT(" (사용 가능)") : TEXT(""));

			UCA3DGameInstance* GameInstance = Owned.IsValid() ? Owned->Owner.Get() : nullptr;
			if (GameInstance == nullptr)
			{
				return;
			}

			// 실패해도 로그인은 시도한다 — 이전 실행에서 만들어 둔 자격이 있을 수 있고,
			// 여기서 멈추면 실패 사유가 EOS 로그인 에러 코드로 드러나지 않는다.
			GameInstance->StartOnlineLogin();
		});
#else
	UE_LOG(LogCA3D, Warning, TEXT("[GameInstance] EOS SDK 미포함 빌드 — Device ID 생성 생략"));
	StartOnlineLogin();
#endif // WITH_EOS_SDK
}

void UCA3DGameInstance::StartOnlineLogin()
{
	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineIdentityPtr Identity = OSS ? OSS->GetIdentityInterface() : nullptr;
	if (!Identity.IsValid())
	{
		HandleLoginComplete(LocalUserNum, false, *FUniqueNetIdString::EmptyId(), TEXT("Identity 인터페이스 소실"));
		return;
	}

	// bUseEAS=false + bUseEOSConnect=true 설정에서 OSS EOS 는 이 자격을 그대로
	// EOS_Connect_Login 의 ExternalCredentialType 으로 번역한다
	// ("DeviceIdAccessToken" 문자열은 EOSShared 의 LexFromString 이 파싱).
	// Device ID 는 토큰이 없다 — 자격 자체가 로컬 기기에 저장돼 있다.
	FOnlineAccountCredentials Credentials;
	Credentials.Type = TEXT("externalauth:DeviceIdAccessToken");
	Credentials.Id = FString();
	Credentials.Token = FString();

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] EOS Connect 로그인 요청 (Type=%s)"), *Credentials.Type);
	Identity->Login(LocalUserNum, Credentials);
}

void UCA3DGameInstance::HandleLoginComplete(int32 InLocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	bLoginInProgress = false;

	if (IOnlineSubsystem* OSS = GetOnlineSubsystem())
	{
		if (IOnlineIdentityPtr Identity = OSS->GetIdentityInterface())
		{
			Identity->ClearOnLoginCompleteDelegate_Handle(InLocalUserNum, LoginCompleteHandle);
		}
	}
	LoginCompleteHandle.Reset();

	if (bWasSuccessful)
	{
		UE_LOG(LogCA3D, Log, TEXT("[GameInstance] EOS 로그인 성공 — ProductUserId=%s"), *UserId.ToString());
	}
	else
	{
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] EOS 로그인 실패 — %s"), *Error);
	}

	OnLoginComplete.Broadcast(bWasSuccessful, Error);
}

// ─────────────────────────────────────────────────────────────────────────────
// 세션 — 생성
// ─────────────────────────────────────────────────────────────────────────────

void UCA3DGameInstance::FailHost(const FString& Error)
{
	bHostInProgress = false;
	UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 생성 실패 — %s"), *Error);
	OnHostSessionComplete.Broadcast(false, Error);
}

void UCA3DGameInstance::HostSession(int32 MaxPlayers)
{
	if (bHostInProgress)
	{
		FailHost(TEXT("이미 방 생성 중"));
		return;
	}

	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		FailHost(TEXT("EOS Session 인터페이스 없음"));
		return;
	}

	if (!IsLoggedIn())
	{
		FailHost(TEXT("EOS 로그인 전 — 방을 만들 수 없음"));
		return;
	}

	// 남아 있는 이전 세션이 있으면 CreateSession 이 즉시 실패한다. 먼저 정리한다.
	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		FailHost(TEXT("이전 세션이 남아 있음 — LeaveToLobby 로 먼저 정리할 것"));
		return;
	}

	PendingMaxPlayers = MaxPlayers > 0 ? MaxPlayers : DefaultMaxPlayers;
	bHostInProgress = true;

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = PendingMaxPlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bIsLANMatch = false;
	Settings.bShouldAdvertise = true;          // 공개방 목록에 노출 (GDD 6.3)
	Settings.bAllowJoinInProgress = false;     // 난입 없음 (GDD 6.3)
	Settings.bAllowInvites = false;
	Settings.bUsesPresence = false;            // Device ID 익명 — Epic 프레즌스가 없다
	Settings.bAllowJoinViaPresence = false;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bUseLobbiesIfAvailable = false;   // EOS Lobby 가 아니라 Session — 데디 서버 등록 대비
	Settings.bUsesStats = false;
	Settings.bAntiCheatProtected = false;
	Settings.bIsDedicated = IsDedicatedServerInstance();
	Settings.BuildUniqueId = GetBuildUniqueId();

	Settings.Set(CA3DBucketIdKey, SessionBucketId, EOnlineDataAdvertisementType::ViaOnlineService);
	Settings.Set(SETTING_MAPNAME, ArenaMapPath, EOnlineDataAdvertisementType::ViaOnlineService);

	FString HostName = TEXT("CrazyArcade3D");
	if (IOnlineIdentityPtr Identity = OSS->GetIdentityInterface())
	{
		const FString Nickname = Identity->GetPlayerNickname(LocalUserNum);
		if (!Nickname.IsEmpty())
		{
			HostName = Nickname;
		}
	}
	Settings.Set(CA3DHostNameKey, HostName, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateSessionCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UCA3DGameInstance::HandleCreateSessionComplete));

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 생성 요청 — 정원 %d, 버킷 %s, 맵 %s"),
		PendingMaxPlayers, *SessionBucketId, *ArenaMapPath);

	if (!Sessions->CreateSession(LocalUserNum, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		CreateSessionCompleteHandle.Reset();
		FailHost(TEXT("CreateSession 호출이 즉시 거부됨"));
	}
}

void UCA3DGameInstance::HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSubsystem* OSS = GetOnlineSubsystem())
	{
		if (IOnlineSessionPtr Sessions = OSS->GetSessionInterface())
		{
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		}
	}
	CreateSessionCompleteHandle.Reset();
	bHostInProgress = false;

	if (!bWasSuccessful)
	{
		OnHostSessionComplete.Broadcast(false, TEXT("EOS 세션 생성 실패 (LogOnline 에 EOS 결과 코드)"));
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 생성 실패 — 세션 %s"), *SessionName.ToString());
		return;
	}

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 생성 성공 — %s 로 ServerTravel"), *ArenaMapPath);
	OnHostSessionComplete.Broadcast(true, FString());

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogCA3D, Warning, TEXT("[GameInstance] 월드 없음 — ServerTravel 생략"));
		return;
	}

	// ?listen 이 IpNetDriver 리슨 서버를 띄운다. 전송은 EOS 가 아니라 IP 다 (GDD 8장).
	World->ServerTravel(ArenaMapPath + TEXT("?listen"));
}

// ─────────────────────────────────────────────────────────────────────────────
// 세션 — 검색
// ─────────────────────────────────────────────────────────────────────────────

void UCA3DGameInstance::FailFind(const FString& Error)
{
	bFindInProgress = false;
	LastFoundSessions.Reset();
	UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 검색 실패 — %s"), *Error);
	OnSessionsFound.Broadcast(false, LastFoundSessions, Error);
}

void UCA3DGameInstance::FindSessions()
{
	if (bFindInProgress)
	{
		UE_LOG(LogCA3D, Verbose, TEXT("[GameInstance] 검색이 이미 진행 중 — 중복 요청 무시"));
		return;
	}

	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		FailFind(TEXT("EOS Session 인터페이스 없음"));
		return;
	}

	if (!IsLoggedIn())
	{
		FailFind(TEXT("EOS 로그인 전 — 검색할 수 없음"));
		return;
	}

	bFindInProgress = true;

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = 50;
	SessionSearch->bIsLanQuery = false;
	// 생성 쪽과 같은 버킷이어야 서로 보인다.
	SessionSearch->QuerySettings.Set(CA3DBucketIdKey, SessionBucketId, EOnlineComparisonOp::Equals);

	FindSessionsCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UCA3DGameInstance::HandleFindSessionsComplete));

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 검색 요청 — 버킷 %s"), *SessionBucketId);

	if (!Sessions->FindSessions(LocalUserNum, SessionSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		FindSessionsCompleteHandle.Reset();
		FailFind(TEXT("FindSessions 호출이 즉시 거부됨"));
	}
}

void UCA3DGameInstance::HandleFindSessionsComplete(bool bWasSuccessful)
{
	if (IOnlineSubsystem* OSS = GetOnlineSubsystem())
	{
		if (IOnlineSessionPtr Sessions = OSS->GetSessionInterface())
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		}
	}
	FindSessionsCompleteHandle.Reset();
	bFindInProgress = false;

	LastFoundSessions.Reset();

	if (!bWasSuccessful || !SessionSearch.IsValid())
	{
		const FString Error = TEXT("EOS 검색 실패 (LogOnline 에 EOS 결과 코드)");
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 검색 실패 — %s"), *Error);
		OnSessionsFound.Broadcast(false, LastFoundSessions, Error);
		return;
	}

	for (int32 Index = 0; Index < SessionSearch->SearchResults.Num(); ++Index)
	{
		const FOnlineSessionSearchResult& Result = SessionSearch->SearchResults[Index];

		FCA3DSessionSummary Summary;
		Summary.Index = Index;
		Summary.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
		// OpenPublicConnections 는 "남은 자리" 다. 현재 인원은 정원에서 빼서 구한다.
		Summary.CurrentPlayers = FMath::Max(0, Summary.MaxPlayers - Result.Session.NumOpenPublicConnections);
		Summary.PingMs = Result.PingInMs;

		if (!Result.Session.SessionSettings.Get(CA3DHostNameKey, Summary.HostName) || Summary.HostName.IsEmpty())
		{
			Summary.HostName = Result.Session.OwningUserName.IsEmpty() ? TEXT("알 수 없음") : Result.Session.OwningUserName;
		}

		LastFoundSessions.Add(Summary);
		UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 발견 %s"), *Summary.ToDisplayString());
	}

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 검색 완료 — %d개"), LastFoundSessions.Num());
	OnSessionsFound.Broadcast(true, LastFoundSessions, FString());
}

// ─────────────────────────────────────────────────────────────────────────────
// 세션 — 참가
// ─────────────────────────────────────────────────────────────────────────────

void UCA3DGameInstance::FailJoin(const FString& Error)
{
	bJoinInProgress = false;
	UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 참가 실패 — %s"), *Error);
	OnJoinSessionComplete.Broadcast(false, Error);
}

void UCA3DGameInstance::JoinFoundSession(int32 SearchResultIndex)
{
	if (bJoinInProgress)
	{
		FailJoin(TEXT("이미 참가 중"));
		return;
	}

	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		FailJoin(TEXT("EOS Session 인터페이스 없음"));
		return;
	}

	if (!SessionSearch.IsValid() || !SessionSearch->SearchResults.IsValidIndex(SearchResultIndex))
	{
		FailJoin(FString::Printf(TEXT("검색 결과 인덱스 %d 가 유효하지 않음 (FindSessions 를 먼저 실행할 것)"), SearchResultIndex));
		return;
	}

	if (Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		FailJoin(TEXT("이전 세션이 남아 있음 — LeaveToLobby 로 먼저 정리할 것"));
		return;
	}

	bJoinInProgress = true;

	JoinSessionCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateWeakLambda(this,
			[this](FName SessionName, EOnJoinSessionCompleteResult::Type Result)
			{
				const bool bOk = Result == EOnJoinSessionCompleteResult::Success;
				HandleJoinSessionComplete(SessionName, bOk, LexToString(Result));
			}));

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 참가 요청 — 인덱스 %d"), SearchResultIndex);

	if (!Sessions->JoinSession(LocalUserNum, NAME_GameSession, SessionSearch->SearchResults[SearchResultIndex]))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		JoinSessionCompleteHandle.Reset();
		FailJoin(TEXT("JoinSession 호출이 즉시 거부됨"));
	}
}

void UCA3DGameInstance::HandleJoinSessionComplete(FName SessionName, bool bWasSuccessful, const FString& ResultText)
{
	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
	}
	JoinSessionCompleteHandle.Reset();
	bJoinInProgress = false;

	if (!bWasSuccessful || !Sessions.IsValid())
	{
		OnJoinSessionComplete.Broadcast(false, ResultText);
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 참가 실패 — %s (%s)"), *ResultText, *SessionName.ToString());
		return;
	}

	// EOS 는 세션에 등록된 호스트 주소(IP:포트)를 돌려준다 — 전송은 IpNetDriver 그대로다.
	FString ConnectString;
	if (!Sessions->GetResolvedConnectString(NAME_GameSession, ConnectString) || ConnectString.IsEmpty())
	{
		OnJoinSessionComplete.Broadcast(false, TEXT("접속 주소를 얻지 못함"));
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 방 참가 실패 — 접속 주소 없음"));
		return;
	}

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 방 참가 성공 — ClientTravel %s"), *ConnectString);
	OnJoinSessionComplete.Broadcast(true, FString());

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(ConnectString, TRAVEL_Absolute);
	}
	else
	{
		UE_LOG(LogCA3D, Warning, TEXT("[GameInstance] 로컬 PlayerController 없음 — ClientTravel 생략"));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 세션 — 로비 복귀
// ─────────────────────────────────────────────────────────────────────────────

void UCA3DGameInstance::LeaveToLobby()
{
	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;

	if (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr)
	{
		DestroySessionCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &UCA3DGameInstance::HandleDestroySessionComplete));

		UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 세션 파기 요청 (로비 복귀)"));

		if (!Sessions->DestroySession(NAME_GameSession))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
			DestroySessionCompleteHandle.Reset();
			UE_LOG(LogCA3D, Error, TEXT("[GameInstance] DestroySession 호출이 즉시 거부됨"));
			OnLeftToLobby.Broadcast(false, TEXT("DestroySession 호출이 즉시 거부됨"));
		}
		return;
	}

	// 세션이 없어도 로비로는 돌아가야 한다 (실패로 취급하지 않는다).
	HandleDestroySessionComplete(NAME_GameSession, true);
}

void UCA3DGameInstance::HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	if (IOnlineSubsystem* OSS = GetOnlineSubsystem())
	{
		if (IOnlineSessionPtr Sessions = OSS->GetSessionInterface())
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
		}
	}
	DestroySessionCompleteHandle.Reset();

	SessionSearch.Reset();
	LastFoundSessions.Reset();

	if (!bWasSuccessful)
	{
		UE_LOG(LogCA3D, Error, TEXT("[GameInstance] 세션 파기 실패 — %s"), *SessionName.ToString());
		OnLeftToLobby.Broadcast(false, TEXT("세션 파기 실패"));
		return;
	}

	UE_LOG(LogCA3D, Log, TEXT("[GameInstance] 세션 정리 완료"));
	OnLeftToLobby.Broadcast(true, FString());

	// 로비 맵은 UI Task 에서 만든다. 지정 전이면 세션만 정리하고 트래블하지 않는다.
	if (LobbyMapPath.IsEmpty())
	{
		UE_LOG(LogCA3D, Verbose, TEXT("[GameInstance] LobbyMapPath 미지정 — 트래블 생략"));
		return;
	}

	if (APlayerController* PC = GetFirstLocalPlayerController())
	{
		PC->ClientTravel(LobbyMapPath, TRAVEL_Absolute);
	}
}

void UCA3DGameInstance::ClearOnlineDelegates()
{
	IOnlineSubsystem* OSS = GetOnlineSubsystem();
	if (OSS == nullptr)
	{
		return;
	}

	if (IOnlineIdentityPtr Identity = OSS->GetIdentityInterface())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(LocalUserNum, LoginCompleteHandle);
	}

	if (IOnlineSessionPtr Sessions = OSS->GetSessionInterface())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateSessionCompleteHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindSessionsCompleteHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinSessionCompleteHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroySessionCompleteHandle);
	}

	LoginCompleteHandle.Reset();
	CreateSessionCompleteHandle.Reset();
	FindSessionsCompleteHandle.Reset();
	JoinSessionCompleteHandle.Reset();
	DestroySessionCompleteHandle.Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 개발용 콘솔 명령
//
// EOS 는 UI 없이 헤드리스로 검증해야 하는 구간이 많다 (`-ExecCmds` 로 호출).
// 로비 위젯이 생기기 전까지 이게 유일한 조작 수단이다.
// ─────────────────────────────────────────────────────────────────────────────
#if !UE_BUILD_SHIPPING
namespace
{
	UCA3DGameInstance* CA3DFindGameInstance(UWorld* World)
	{
		UCA3DGameInstance* GameInstance = World ? Cast<UCA3DGameInstance>(World->GetGameInstance()) : nullptr;
		if (GameInstance == nullptr)
		{
			UE_LOG(LogCA3D, Error, TEXT("[ca3d] UCA3DGameInstance 없음 — DefaultEngine.ini 의 GameInstanceClass 확인"));
		}
		return GameInstance;
	}

#if WITH_EOS_SDK
	// ca3d.EOSDeviceIdProbe — "자격 증명 문제인가, 엔진 래퍼 문제인가"를 가르는 진단.
	//
	// OSS EOS(v1) 는 Win64 에서 EOS_Connect_Login 에 UserLoginInfo 를 넘기지 않는다:
	//   OnlineSubsystemEOS.Build.cs  bAddUserLoginInfo => false  (ADD_USER_LOGIN_INFO=0)
	//   UserManagerEOS.cpp:1137      #if ADD_USER_LOGIN_INFO ... Options.UserLoginInfo = ...
	// 그런데 Device ID 로그인은 UserLoginInfo.DisplayName 이 **필수**다
	// (eos_connect_types.h EOS_Connect_LoginOptions 주석). 그래서 OSS 경유 로그인은
	// 반드시 EOS_InvalidParameters 로 떨어진다 — 자격 증명과 무관하다.
	//
	// 이 명령은 같은 자격·같은 Device ID 로 SDK 를 직접 불러 로그인이 성공함을 보여준다.
	// VPS 에서도 "포털 설정이 틀렸나"를 배제하는 데 그대로 쓴다.
	void CA3DLogProbeUser(const TCHAR* Stage, EOS_ProductUserId UserId)
	{
		UE_LOG(LogCA3D, Log, TEXT("[ca3d] DeviceId 프로브 %s — ProductUserId=%s"), Stage, *LexToString(UserId));
	}

	void CA3DRunDeviceIdProbe()
	{
		IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
		TArray<IEOSPlatformHandlePtr> Platforms = SDKManager ? SDKManager->GetActivePlatforms() : TArray<IEOSPlatformHandlePtr>();
		if (Platforms.Num() == 0 || !Platforms[0].IsValid())
		{
			UE_LOG(LogCA3D, Error, TEXT("[ca3d] DeviceId 프로브 — 활성 EOS 플랫폼 없음"));
			return;
		}

		EOS_HConnect Connect = EOS_Platform_GetConnectInterface(*Platforms[0]);

		// Device ID 로그인은 토큰이 없다 — Token 은 반드시 nullptr 이고,
		// DisplayName 이 그 자리를 대신한다 (OSS 가 빠뜨리는 바로 그 필드).
		static char ProbeDisplayName[] = "CA3DPlayer";

		EOS_Connect_Credentials Credentials = {};
		Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
		Credentials.Token = nullptr;
		Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;

		EOS_Connect_UserLoginInfo UserLoginInfo = {};
		UserLoginInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
		UserLoginInfo.DisplayName = ProbeDisplayName;
		UserLoginInfo.NsaIdToken = nullptr;

		EOS_Connect_LoginOptions Options = {};
		Options.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
		Options.Credentials = &Credentials;
		Options.UserLoginInfo = &UserLoginInfo;

		UE_LOG(LogCA3D, Log, TEXT("[ca3d] DeviceId 프로브 — EOS_Connect_Login (UserLoginInfo 포함) 호출"));

		EOS_Connect_Login(Connect, &Options, Connect,
			[](const EOS_Connect_LoginCallbackInfo* Data)
			{
				if (Data->ResultCode == EOS_EResult::EOS_Success)
				{
					CA3DLogProbeUser(TEXT("성공"), Data->LocalUserId);
					return;
				}

				if (Data->ResultCode != EOS_EResult::EOS_InvalidUser)
				{
					UE_LOG(LogCA3D, Error, TEXT("[ca3d] DeviceId 프로브 실패 — %s"), *LexToString(Data->ResultCode));
					return;
				}

				// 이 Device ID 에 아직 ProductUser 가 없다 — 연속 토큰으로 만들어 준다.
				UE_LOG(LogCA3D, Log, TEXT("[ca3d] DeviceId 프로브 — 신규 사용자, EOS_Connect_CreateUser 진행"));

				EOS_Connect_CreateUserOptions CreateOptions = {};
				CreateOptions.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
				CreateOptions.ContinuanceToken = Data->ContinuanceToken;

				EOS_Connect_CreateUser(static_cast<EOS_HConnect>(Data->ClientData), &CreateOptions, nullptr,
					[](const EOS_Connect_CreateUserCallbackInfo* CreateData)
					{
						if (CreateData->ResultCode == EOS_EResult::EOS_Success)
						{
							CA3DLogProbeUser(TEXT("성공(신규 생성)"), CreateData->LocalUserId);
						}
						else
						{
							UE_LOG(LogCA3D, Error, TEXT("[ca3d] DeviceId 프로브 CreateUser 실패 — %s"),
								*LexToString(CreateData->ResultCode));
						}
					});
			});
	}

	FAutoConsoleCommandWithWorldAndArgs GCA3DProbeCmd(
		TEXT("ca3d.EOSDeviceIdProbe"),
		TEXT("EOS SDK 를 직접 불러 Device ID 로그인을 시험한다 (자격 증명 문제와 엔진 래퍼 문제를 가른다)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			CA3DRunDeviceIdProbe();
		}));
#endif // WITH_EOS_SDK

	FAutoConsoleCommandWithWorldAndArgs GCA3DLoginCmd(
		TEXT("ca3d.LoginDeviceId"),
		TEXT("EOS Device ID 익명 로그인을 (재)시도한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World))
			{
				GameInstance->LoginDeviceId();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCA3DHostCmd(
		TEXT("ca3d.HostSession"),
		TEXT("EOS 방을 만든다. 인자: [정원]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World))
			{
				GameInstance->HostSession(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCA3DFindCmd(
		TEXT("ca3d.FindSessions"),
		TEXT("공개 EOS 방 목록을 검색해 로그로 출력한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World))
			{
				GameInstance->FindSessions();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCA3DJoinCmd(
		TEXT("ca3d.JoinSession"),
		TEXT("검색 결과 인덱스로 방에 참가한다. 인자: <인덱스>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World))
			{
				GameInstance->JoinFoundSession(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCA3DLeaveCmd(
		TEXT("ca3d.LeaveToLobby"),
		TEXT("세션을 파기하고 로비로 돌아간다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			if (UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World))
			{
				GameInstance->LeaveToLobby();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GCA3DStatusCmd(
		TEXT("ca3d.EOSStatus"),
		TEXT("EOS 로그인·세션 상태를 로그로 출력한다."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCA3DGameInstance* GameInstance = CA3DFindGameInstance(World);
			if (GameInstance == nullptr)
			{
				return;
			}

			IOnlineSubsystem* OSS = IOnlineSubsystem::Get(EOS_SUBSYSTEM);
			UE_LOG(LogCA3D, Log, TEXT("[ca3d] EOS 서브시스템 = %s, 로그인 = %s, 검색 결과 = %d개"),
				OSS ? TEXT("있음") : TEXT("없음"),
				GameInstance->IsLoggedIn() ? TEXT("O") : TEXT("X"),
				GameInstance->GetLastFoundSessions().Num());
		}));
}
#endif // !UE_BUILD_SHIPPING
