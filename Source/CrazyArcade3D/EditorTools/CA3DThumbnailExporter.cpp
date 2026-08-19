#include "EditorTools/CA3DThumbnailExporter.h"

#if WITH_EDITOR

#include "CrazyArcade3D.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "ImageUtils.h"       // FImageUtils::SaveImageByExtension
#include "Misc/Paths.h"
#include "ObjectTools.h"      // ThumbnailTools

// FObjectThumbnail::GetImage() 는 **헤더에 선언만** 있고 본체가 .inl 에 있다.
// 그리고 그 .inl 은 통째로 `#if defined(IMAGECORE_API)` 로 감싸여 있다 —
// Build.cs 에 "ImageCore" 의존이 없으면 이 include 가 조용히 빈 파일이 되고,
// 컴파일은 통과한 뒤 링크에서 "GetImage 미해결 외부 심볼"로 터진다. 원인 찾기 고약한 자리.
#include "ImageCore.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/ObjectThumbnail.inl"

namespace
{
	// 출력 폴더 기본값 — 비워서 부르면 여기로 간다.
	FString ResolveOutputDir(const FString& InDir)
	{
		const FString Dir = InDir.IsEmpty()
			? FPaths::ProjectSavedDir() / TEXT("Thumbnails")
			: InDir;
		return FPaths::ConvertRelativePathToFull(Dir);
	}

	// 확장자가 없으면 .png 를 붙인다. SaveImageByExtension 은 **확장자를 보고** 포맷을 정하므로
	// 이걸 빼먹으면 PNG 가 아니라 엔진이 고른 다른 포맷으로 저장된다.
	FString ResolveOutputFile(const FString& InPath)
	{
		FString Path = InPath;
		if (FPaths::GetExtension(Path).IsEmpty())
		{
			Path += TEXT(".png");
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}

	// FAssetData 목록 → PNG 일괄 저장. 선택/경로 두 진입점이 이걸 공유한다.
	int32 ExportAssetDataList(const TArray<FAssetData>& AssetDataList, const FString& OutputDir, int32 Size)
	{
		const FString Dir = ResolveOutputDir(OutputDir);

		int32 SuccessCount = 0;
		for (const FAssetData& Data : AssetDataList)
		{
			// 리다이렉터는 실체가 아니라 이정표다. 썸네일도 없고 로드하면 대상만 딸려 온다.
			if (Data.IsRedirector())
			{
				continue;
			}

			// 로드되지 않은 에셋이면 여기서 동기 로드된다 — 썸네일 렌더러가 실체를 요구한다.
			UObject* Asset = Data.GetAsset();
			if (!Asset)
			{
				UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 로드 실패: %s"), *Data.GetObjectPathString());
				continue;
			}

			// 파일명은 **에셋 이름만** 쓴다. 서로 다른 폴더에 같은 이름이 있으면 뒤엣것이 덮어쓴다.
			if (UCA3DThumbnailExporter::ExportThumbnail(Asset, Dir / Asset->GetName(), Size))
			{
				++SuccessCount;
			}
		}

		UE_LOG(LogCA3D, Log, TEXT("[Thumbnail] %d/%d 개 저장 → %s"),
			SuccessCount, AssetDataList.Num(), *Dir);
		return SuccessCount;
	}
}

bool UCA3DThumbnailExporter::ExportThumbnail(UObject* Asset, const FString& OutputPath, int32 Size)
{
	if (!Asset)
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 에셋이 null 이다."));
		return false;
	}

	// 썸네일 렌더링은 GUnrealEd 의 ThumbnailManager 와 RHI 를 쓴다. 커맨드릿·-nullrhi 에서는 못 돈다.
	if (!GEditor || !FApp::CanEverRender())
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 렌더링 불가 환경이다 (에디터 밖이거나 -nullrhi)."));
		return false;
	}

	const FString FullPath = ResolveOutputFile(OutputPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), /*Tree*/ true);

	// ── 썸네일 확보: 두 갈래 ─────────────────────────────────────────────
	FObjectThumbnail  RenderedThumbnail;         // Size > 0 경로에서만 채워진다
	FObjectThumbnail* Thumbnail = nullptr;

	if (Size > 0)
	{
		// 원하는 해상도로 **새로 렌더**한다. 패키지의 ThumbnailMap 은 건드리지 않는다.
		// RT 를 nullptr 로 주면 엔진이 스크래치 렌더타깃을 잡아 준다 (인자가 참조로 다시 채워진다).
		// AlwaysFlush: 텍스처 스트리밍을 강제로 채우고 찍는다 — 느리지만 흐릿한 결과가 안 나온다.
		FTextureRenderTargetResource* ScratchRT = nullptr;
		ThumbnailTools::RenderThumbnail(
			Asset,
			static_cast<uint32>(Size), static_cast<uint32>(Size),
			ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
			ScratchRT,
			&RenderedThumbnail);

		Thumbnail = &RenderedThumbnail;
	}
	else
	{
		// 컨텐트 브라우저가 패키지에 굽는 것과 **완전히 같은** 썸네일 (256px 고정).
		// 반환 포인터는 패키지 ThumbnailMap 안을 가리킨다 — 우리가 소유하지 않는다.
		Thumbnail = ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(Asset);
	}

	// 렌더러가 없는 클래스(썸네일 대신 클래스 아이콘만 쓰는 에셋)는 여기서 걸러진다.
	if (!Thumbnail || !Thumbnail->HasValidImageData())
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] '%s' 는 썸네일을 만들 수 없다 (전용 렌더러 없음)."),
			*Asset->GetName());
		return false;
	}

	// GetImage() 는 썸네일 내부 버퍼(BGRA8)를 **가리키는** FImageView 다 — 복사가 아니다.
	// Thumbnail 이 살아 있는 동안에만 유효하므로 이 줄 안에서 다 쓰고 끝낸다.
	// Quality 100 = EImageCompressionQuality::Max → PNG 에서는 최대 압축.
	const bool bSaved = FImageUtils::SaveImageByExtension(*FullPath, Thumbnail->GetImage(), 100);

	if (bSaved)
	{
		UE_LOG(LogCA3D, Log, TEXT("[Thumbnail] %s (%dx%d) → %s"),
			*Asset->GetName(), Thumbnail->GetImageWidth(), Thumbnail->GetImageHeight(), *FullPath);
	}
	else
	{
		UE_LOG(LogCA3D, Error, TEXT("[Thumbnail] 저장 실패: %s"), *FullPath);
	}

	return bSaved;
}

int32 UCA3DThumbnailExporter::ExportThumbnails(const TArray<UObject*>& Assets, const FString& OutputDir, int32 Size)
{
	const FString Dir = ResolveOutputDir(OutputDir);

	int32 SuccessCount = 0;
	for (UObject* Asset : Assets)
	{
		if (Asset && ExportThumbnail(Asset, Dir / Asset->GetName(), Size))
		{
			++SuccessCount;
		}
	}

	UE_LOG(LogCA3D, Log, TEXT("[Thumbnail] %d/%d 개 저장 → %s"), SuccessCount, Assets.Num(), *Dir);
	return SuccessCount;
}

int32 UCA3DThumbnailExporter::ExportSelectedThumbnails(const FString& OutputDir, int32 Size)
{
	FContentBrowserModule& ContentBrowser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TArray<FAssetData> SelectedAssets;
	ContentBrowser.Get().GetSelectedAssets(SelectedAssets);

	if (SelectedAssets.Num() == 0)
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 컨텐트 브라우저에서 선택된 에셋이 없다."));
		return 0;
	}

	return ExportAssetDataList(SelectedAssets, OutputDir, Size);
}

int32 UCA3DThumbnailExporter::ExportThumbnailsInPath(const FString& PackagePath, const FString& OutputDir, bool bRecursive, int32 Size)
{
	if (PackagePath.IsEmpty())
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 패키지 경로가 비었다 (예: /Game/Characters)."));
		return 0;
	}

	FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssetsByPath(FName(*PackagePath), Assets, bRecursive);

	if (Assets.Num() == 0)
	{
		UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] '%s' 밑에 에셋이 없다."), *PackagePath);
		return 0;
	}

	return ExportAssetDataList(Assets, OutputDir, Size);
}

// ── 콘솔 명령 ────────────────────────────────────────────────────────────────
// 에디터 유틸리티 위젯을 따로 만들지 않아도 바로 쓸 수 있게 하는 진입점.
// 블루프린트 함수는 "어딘가에서 불러 줘야" 하지만 콘솔은 ` 키 하나면 된다.

static FAutoConsoleCommand GExportSelectedThumbnailsCmd(
	TEXT("CA3D.ExportSelectedThumbnails"),
	TEXT("컨텐트 브라우저에서 선택한 에셋의 썸네일을 PNG 로 저장. 사용법: CA3D.ExportSelectedThumbnails [출력폴더] [해상도]"),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		const FString OutputDir = Args.IsValidIndex(0) ? Args[0] : FString();
		const int32   Size      = Args.IsValidIndex(1) ? FCString::Atoi(*Args[1]) : 0;
		UCA3DThumbnailExporter::ExportSelectedThumbnails(OutputDir, Size);
	}));

static FAutoConsoleCommand GExportThumbnailsCmd(
	TEXT("CA3D.ExportThumbnails"),
	TEXT("패키지 경로 밑의 에셋 썸네일을 PNG 로 저장. 사용법: CA3D.ExportThumbnails /Game/Characters [출력폴더] [해상도]"),
	FConsoleCommandWithArgsDelegate::CreateStatic([](const TArray<FString>& Args)
	{
		if (!Args.IsValidIndex(0))
		{
			UE_LOG(LogCA3D, Warning, TEXT("[Thumbnail] 사용법: CA3D.ExportThumbnails /Game/Characters [출력폴더] [해상도]"));
			return;
		}
		const FString OutputDir = Args.IsValidIndex(1) ? Args[1] : FString();
		const int32   Size      = Args.IsValidIndex(2) ? FCString::Atoi(*Args[2]) : 0;
		UCA3DThumbnailExporter::ExportThumbnailsInPath(Args[0], OutputDir, /*bRecursive*/ true, Size);
	}));

#endif // WITH_EDITOR
