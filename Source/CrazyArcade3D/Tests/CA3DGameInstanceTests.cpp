// UCA3DGameInstance (EOS 세션·로비) 자동화 테스트 (Task 19).
// -ExecCmds="Automation RunTests CrazyArcade3D.Framework.GameInstance" 로 실행.
//
// 헤드리스로 검증 가능한 것만 다룬다: ini 배선(GameInstanceClass·환경 설정), 요약 포맷,
// **실패 통지 규약**(조용히 실패하면 로비가 영원히 "검색 중"으로 멈춘다), 그리고
// 자격 증명이 커밋되는 파일로 새지 않았는지 확인하는 보안 회귀 테스트.
//
// 실제 EOS 로그인·방 생성·검색·참가는 네트워크와 EOS 백엔드가 필요하므로 여기서 다루지
// 않는다 — 헤드리스 실행(`-game` + `ca3d.*` 콘솔 명령)과 2클라 검증이 그 몫이다
// (체크리스트 19).
//
// ⚠️ 무명 네임스페이스 헬퍼 이름은 번역 단위 병합에서 모듈 전체와 합쳐진다 —
// 접두사 Gi~ 로 고유하게 유지할 것 (mds/build.md "번역 단위 병합 빌드" 절).

#include "Misc/AutomationTest.h"
#include "CrazyArcade3D.h"
#include "Framework/CA3DGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCA3DGameInstanceTest, "CrazyArcade3D.Framework.GameInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	const TCHAR* GiEOSSettingsSection = TEXT("/Script/OnlineSubsystemEOS.EOSSettings");

	// 커밋되는 설정 파일 전문을 읽어온다 (없으면 빈 문자열).
	FString GiReadProjectConfig(const TCHAR* FileName)
	{
		FString Contents;
		FFileHelper::LoadFileToString(Contents, *FPaths::Combine(FPaths::ProjectConfigDir(), FileName));
		return Contents;
	}
}

bool FCA3DGameInstanceTest::RunTest(const FString& Parameters)
{
	// ── 1. ini 배선: 게임이 실제로 이 GameInstance 를 쓰는가 ──
	{
		FString ConfiguredClass;
		GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"),
			ConfiguredClass, GEngineIni);
		TestEqual(TEXT("GameInstanceClass 가 UCA3DGameInstance 를 가리킨다"),
			ConfiguredClass, FString(TEXT("/Script/CrazyArcade3D.CA3DGameInstance")));
	}

	// ── 2. 환경 설정이 DefaultGame.ini 에서 CDO 로 들어왔는가 ──
	{
		const UCA3DGameInstance* Defaults = GetDefault<UCA3DGameInstance>();
		if (TestNotNull(TEXT("UCA3DGameInstance CDO"), Defaults))
		{
			TestEqual(TEXT("기본 정원은 8인 (GDD)"), Defaults->DefaultMaxPlayers, 8);
			TestEqual(TEXT("아레나 맵 경로"), Defaults->ArenaMapPath, FString(TEXT("/Game/Maps/L_Arena")));
			TestFalse(TEXT("세션 버킷이 비어 있지 않다 — 비면 빌드마다 방이 갈라진다"),
				Defaults->SessionBucketId.IsEmpty());
		}
	}

	// ── 3. 검색 결과 요약 포맷 ──
	{
		FCA3DSessionSummary Summary;
		Summary.Index = 2;
		Summary.HostName = TEXT("Host");
		Summary.CurrentPlayers = 3;
		Summary.MaxPlayers = 8;
		Summary.PingMs = 42;
		TestEqual(TEXT("요약 한 줄 포맷"), Summary.ToDisplayString(), FString(TEXT("[2] Host  3/8  42ms")));
	}

	// ── 4. 실패 통지 규약 — 조용히 실패하지 않는다 ──
	// 검색을 하지 않은 상태에서 참가를 요청하면 반드시 실패가 통지돼야 한다.
	// (통지가 없으면 UI 가 "참가 중" 스피너에 영구히 갇힌다.)
	{
		UCA3DGameInstance* GameInstance = NewObject<UCA3DGameInstance>(GEngine);

		bool bNotified = false;
		bool bReportedSuccess = true;
		FString ReportedError;
		FDelegateHandle Handle = GameInstance->OnJoinSessionComplete.AddLambda(
			[&bNotified, &bReportedSuccess, &ReportedError](bool bSuccess, const FString& Error)
			{
				bNotified = true;
				bReportedSuccess = bSuccess;
				ReportedError = Error;
			});

		AddExpectedError(TEXT("방 참가 실패"), EAutomationExpectedErrorFlags::Contains, 0);
		GameInstance->JoinFoundSession(0);
		GameInstance->OnJoinSessionComplete.Remove(Handle);

		TestTrue(TEXT("검색 없이 참가하면 결과가 통지된다"), bNotified);
		TestFalse(TEXT("그 결과는 실패다"), bReportedSuccess);
		TestFalse(TEXT("실패 사유 문자열이 비어 있지 않다"), ReportedError.IsEmpty());
	}

	// ── 5. 🔒 보안 회귀 — 자격 증명이 커밋되는 파일로 새지 않았는가 ──
	// 리모트가 public 이다. 여기서 걸리면 이미 유출이므로 반드시 실패로 처리한다.
	{
		const FString DefaultEngineIni = GiReadProjectConfig(TEXT("DefaultEngine.ini"));
		TestFalse(TEXT("DefaultEngine.ini 에 ClientSecret 이 없다"), DefaultEngineIni.Contains(TEXT("ClientSecret")));
		TestFalse(TEXT("DefaultEngine.ini 에 Artifacts 항목이 없다"), DefaultEngineIni.Contains(TEXT("Artifacts=")));

		const FString DefaultGameIni = GiReadProjectConfig(TEXT("DefaultGame.ini"));
		TestFalse(TEXT("DefaultGame.ini 에 ClientSecret 이 없다"), DefaultGameIni.Contains(TEXT("ClientSecret")));

		// 템플릿은 커밋 대상이지만 자리표시자만 들어 있어야 한다.
		const FString ExampleIni = GiReadProjectConfig(TEXT("EOSCredentials.example.ini"));
		if (!ExampleIni.IsEmpty())
		{
			TestTrue(TEXT("example 템플릿은 자리표시자(<...>)만 담는다"), ExampleIni.Contains(TEXT("<")));
		}
	}

	// ── 6. 자격 증명 로더가 실제로 GEngineIni 에 병합했는가 ──
	// EOSCredentials.ini 가 있는 머신에서만 의미가 있다. 없으면 건너뛴다
	// (자격 증명 없이도 개발이 계속 가능해야 한다는 요구 자체를 여기서 확인).
	{
		const FString CredentialsPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("EOSCredentials.ini"));
		if (FPaths::FileExists(CredentialsPath))
		{
			TArray<FString> Artifacts;
			GConfig->GetArray(GiEOSSettingsSection, TEXT("Artifacts"), Artifacts, GEngineIni);
			TestTrue(TEXT("StartupModule 이 Artifacts 를 GEngineIni 에 병합했다"), Artifacts.Num() > 0);

			FString DefaultArtifactName;
			GConfig->GetString(GiEOSSettingsSection, TEXT("DefaultArtifactName"), DefaultArtifactName, GEngineIni);
			TestFalse(TEXT("DefaultArtifactName 이 비어 있지 않다"), DefaultArtifactName.IsEmpty());
		}
		else
		{
			AddInfo(TEXT("Config/EOSCredentials.ini 없음 — 자격 증명 병합 검증을 건너뜁니다 (오프라인 개발 경로)."));
		}
	}

	// ── 7. Device ID 익명 인증 설정이 켜져 있는가 (GDD 6.3) ──
	{
		bool bUseEAS = true;
		bool bUseEOSConnect = false;
		GConfig->GetBool(GiEOSSettingsSection, TEXT("bUseEAS"), bUseEAS, GEngineIni);
		GConfig->GetBool(GiEOSSettingsSection, TEXT("bUseEOSConnect"), bUseEOSConnect, GEngineIni);

		TestFalse(TEXT("bUseEAS=false — Epic 계정 로그인을 요구하지 않는다"), bUseEAS);
		TestTrue(TEXT("bUseEOSConnect=true — Connect 인터페이스만 쓴다"), bUseEOSConnect);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
