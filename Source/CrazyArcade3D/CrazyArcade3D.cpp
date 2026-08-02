#include "CrazyArcade3D.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogCA3D);

namespace
{
	// EOS 자격 증명이 사는 유일한 파일. .gitignore 대상이며 리모트가 public 이므로
	// **절대 커밋되는 파일로 옮기지 않는다.** 값도 로그에 찍지 않는다 (앞 4자 마스킹만).
	const TCHAR* CA3DCredentialsFileName = TEXT("EOSCredentials.ini");
	const TCHAR* CA3DEOSSettingsSection = TEXT("/Script/OnlineSubsystemEOS.EOSSettings");

	// 진단용 — ClientId 는 준식별자라 앞 4자만 남긴다. ClientSecret 은 마스킹조차 하지 않는다.
	FString CA3DMaskHead(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return TEXT("(없음)");
		}
		return Value.Left(4) + TEXT("…");
	}

	// Config/EOSCredentials.ini 를 GEngineIni 에 **병합**한다.
	//
	// 왜 모듈 StartupModule 인가 — UEOSSettings 는 Config=Engine UCLASS 라 CDO 가 만들어질 때
	// GEngineIni 에서 값을 읽는다. CDO 는 OnlineSubsystemEOS 플러그인 모듈이 로드될 때 생긴다.
	// 엔진은 같은 LoadingPhase 안에서 **프로젝트 모듈을 플러그인 모듈보다 먼저** 로드하므로
	// (LaunchEngineLoop.cpp: LoadModulesForProject → LoadModulesForEnabledPlugins),
	// 여기서 넣어두면 EOS 플랫폼이 만들어지기 전에 자격 증명이 자리를 잡는다.
	// GameInstance::Init 은 이미 늦다.
	//
	// CombineFromBuffer 를 쓰는 이유 — `+Artifacts=(...)` 같은 ini 지시어 의미를 그대로
	// 살려서 합쳐준다. GConfig->SetArray 로 직접 쓰면 값이 Saved/Config 로 흘러나가
	// 자격 증명이 예상 밖의 파일에 복제된다.
	void CA3DLoadEOSCredentials()
	{
		const FString CredentialsPath = FPaths::Combine(FPaths::ProjectConfigDir(), CA3DCredentialsFileName);

		if (!FPaths::FileExists(CredentialsPath))
		{
			// 자격 증명이 없어도 게임은 계속 돌아야 한다 — 오프라인/PIE 개발이 지금까지
			// 전부 그 상태로 돌았다. 경고 한 번만 남긴다.
			UE_LOG(LogCA3D, Warning,
				TEXT("[EOS] %s 없음 — 온라인 기능이 비활성화된 채로 계속합니다. ")
				TEXT("Config/EOSCredentials.example.ini 를 복사해 채우세요."),
				*CredentialsPath);
			return;
		}

		FString Buffer;
		if (!FFileHelper::LoadFileToString(Buffer, *CredentialsPath))
		{
			UE_LOG(LogCA3D, Error, TEXT("[EOS] %s 를 읽지 못했습니다."), *CredentialsPath);
			return;
		}

		FConfigFile* EngineConfig = GConfig ? GConfig->FindConfigFile(GEngineIni) : nullptr;
		if (EngineConfig == nullptr)
		{
			UE_LOG(LogCA3D, Error, TEXT("[EOS] GEngineIni 설정 파일을 찾지 못했습니다 — 자격 증명 병합 실패."));
			return;
		}

		EngineConfig->CombineFromBuffer(Buffer, CredentialsPath);

		// 플러그인 모듈이 이미 로드돼 CDO 가 만들어진 뒤라면(로드 순서가 바뀌는 경우 대비)
		// 다시 읽게 한다. 클래스를 이름으로 찾아 OnlineSubsystemEOS 모듈 의존을 만들지 않는다.
		if (UObjectInitialized())
		{
			if (UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/OnlineSubsystemEOS.EOSSettings")))
			{
				SettingsClass->GetDefaultObject()->ReloadConfig();
			}
		}

		FString DefaultArtifactName;
		EngineConfig->GetString(CA3DEOSSettingsSection, TEXT("DefaultArtifactName"), DefaultArtifactName);

		TArray<FString> Artifacts;
		EngineConfig->GetArray(CA3DEOSSettingsSection, TEXT("Artifacts"), Artifacts);

		UE_LOG(LogCA3D, Log, TEXT("[EOS] 자격 증명 병합 완료 — DefaultArtifactName=%s, Artifacts=%d개"),
			DefaultArtifactName.IsEmpty() ? TEXT("(없음)") : *DefaultArtifactName, Artifacts.Num());

		// 값 자체는 절대 찍지 않는다. 아티팩트 이름과 ClientId 앞 4자까지만.
		for (const FString& Entry : Artifacts)
		{
			FString ArtifactName;
			FString ClientId;
			FParse::Value(*Entry, TEXT("ArtifactName="), ArtifactName);
			FParse::Value(*Entry, TEXT("ClientId="), ClientId);
			UE_LOG(LogCA3D, Verbose, TEXT("[EOS]   아티팩트 %s (ClientId %s)"),
				*ArtifactName, *CA3DMaskHead(ClientId));
		}
	}
}

// 기본 게임 모듈 + EOS 자격 증명 로더.
class FCrazyArcade3DModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
		CA3DLoadEOSCredentials();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FCrazyArcade3DModule, CrazyArcade3D, "CrazyArcade3D");
