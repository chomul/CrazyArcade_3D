#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CA3DThumbnailExporter.generated.h"

// 컨텐트 브라우저가 보여주는 그 썸네일을 **그대로 PNG 파일로** 뽑는 에디터 전용 툴.
//
// 쓰는 이유: 캐릭터 선택 UI·로비 아이콘처럼 "에셋의 생김새"가 그대로 필요한 자리에
// 썸네일을 손으로 스크린샷 찍어 만들면 매번 각도·조명·크롭이 달라진다. 엔진의 썸네일
// 렌더러를 그대로 호출하면 8개 캐릭터가 전부 같은 규격으로 나온다.
//
// **전부 #if WITH_EDITOR.** 게임·데디 서버 빌드에는 함수 자체가 없다 (Build.cs 의
// UnrealEd·ContentBrowser 의존도 bBuildEditor 안에서만 걸려 있다).
//
// 쓰는 법 세 가지 —
//   ① 콘솔  : 컨텐트 브라우저에서 에셋 선택 후 `CA3D.ExportSelectedThumbnails`
//   ② 콘솔  : `CA3D.ExportThumbnails /Game/Characters` (폴더 통째로)
//   ③ 에디터 유틸리티 위젯/블루프린트·파이썬에서 아래 함수 직접 호출
UCLASS()
class CRAZYARCADE3D_API UCA3DThumbnailExporter : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#if WITH_EDITOR

	// 에셋 하나를 PNG 로 저장한다.
	//
	// OutputPath : 절대경로든 프로젝트 상대경로든 상관없다. 확장자를 빼면 .png 를 붙인다.
	//              중간 폴더는 알아서 만든다.
	// Size       : 0 이면 **컨텐트 브라우저가 패키지에 저장하는 것과 완전히 같은 썸네일**
	//              (256px 고정, ThumbnailTools::DefaultThumbnailSize). 0 보다 크면 그 해상도로
	//              **새로 렌더**한다 — UI 아이콘용으로 512·1024 가 필요할 때 쓴다.
	//
	// ⚠️ Size==0 경로(GenerateThumbnailForObjectToSaveToDisk)는 부작용이 하나 있다:
	//    렌더한 썸네일을 그 에셋 패키지의 ThumbnailMap 에 **캐시로 심는다.** 나중에 그 에셋을
	//    저장하면 이 썸네일이 같이 저장된다는 뜻이다 (에디터가 원래 하는 일과 동일해서 해롭진
	//    않지만, 패키지를 건드리기 싫으면 Size 를 지정해 렌더 경로로 가면 된다).
	UFUNCTION(BlueprintCallable, Category = "CA3D|Thumbnail")
	static bool ExportThumbnail(UObject* Asset, const FString& OutputPath, int32 Size = 0);

	// 여러 에셋을 OutputDir 밑에 `<에셋이름>.png` 로 저장한다. 반환값은 성공 개수.
	// OutputDir 를 비우면 `<프로젝트>/Saved/Thumbnails`.
	UFUNCTION(BlueprintCallable, Category = "CA3D|Thumbnail")
	static int32 ExportThumbnails(const TArray<UObject*>& Assets, const FString& OutputDir, int32 Size = 0);

	// 지금 컨텐트 브라우저에서 **선택된** 에셋들을 저장한다. 실사용은 거의 이거 하나로 끝난다.
	UFUNCTION(BlueprintCallable, Category = "CA3D|Thumbnail")
	static int32 ExportSelectedThumbnails(const FString& OutputDir, int32 Size = 0);

	// 에셋 경로(`/Game/Characters` 같은 **패키지 경로**, 디스크 경로가 아니다) 밑을 통째로 저장한다.
	// 로드되지 않은 에셋도 이 과정에서 로드된다 — 폴더가 크면 그만큼 걸린다.
	UFUNCTION(BlueprintCallable, Category = "CA3D|Thumbnail")
	static int32 ExportThumbnailsInPath(const FString& PackagePath, const FString& OutputDir, bool bRecursive = true, int32 Size = 0);

#endif // WITH_EDITOR
};
