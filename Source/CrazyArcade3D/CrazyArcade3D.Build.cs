using UnrealBuildTool;

public class CrazyArcade3D : ModuleRules
{
	public CrazyArcade3D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			// 봇(Task 20) — ABotController 의 부모 AAIController. BT/블랙보드는 쓰지 않지만
			// (구조 결정 12: 순수 C++ FSM) 컨트롤러 기반 클래스와 PlayerState 배선은 재사용한다.
			// 헤더가 AAIController 를 공개 상속하므로 Public 의존이어야 한다.
			"AIModule",
			// HUD·위젯 (Task 25/26) — UI/MatchWidget.h 가 UUserWidget 을 **공개 상속**하고
			// 공개 헤더가 Slate 타입(ESlateVisibility 등)을 노출하므로 Public 의존이어야 한다.
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// EOS 세션·로비 (Task 19). 전부 Private — 공개 헤더(CA3DGameInstance.h)는
			// OSS 타입을 노출하지 않으므로 이 의존이 다른 파일로 번지지 않는다.
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"CoreOnline",
			// EOS_Connect_CreateDeviceId 직접 호출용. OSS EOS 가 감싸지 않는 유일한 단계다
			// (Device ID 자격이 로컬에 없으면 로그인이 EOS_NotFound 로 떨어진다).
			"EOSShared",
			"EOSSDK",
			// 큐 이펙트 재생 (Gameplay/CA3DFeedback.cpp 의 UNiagaraFunctionLibrary).
			//
			// **Public 이 아닌 이유**: 나이아가라 타입이 나오는 곳은 두 군데뿐이고 둘 다
			// 전방 선언으로 끝난다 — UCA3DRuleSet 의 TObjectPtr<UNiagaraSystem> 슬롯과
			// CA3DFeedback::ResolveCueAssets 의 인자. 실제 헤더(NiagaraFunctionLibrary.h /
			// NiagaraSystem.h)가 필요한 것은 .cpp 하나뿐이다. Public 으로 올리면 이 모듈을
			// 쓰는 모든 파일이 나이아가라 헤더 트리를 함께 파싱해 빌드 시간만 늘어난다
			// (OnlineSubsystem 을 Private 으로 둔 것과 같은 판단).
			"Niagara",
		});

		// 에디터 전용 툴 — EditorTools/CA3DThumbnailExporter (썸네일 PNG 추출).
		//
		// **bBuildEditor 안에서만** 건다. UnrealEd·ContentBrowser 는 에디터 타깃에만 존재하는
		// 모듈이라 Game/Server 타깃에 넣으면 링크가 아니라 UBT 단계에서 바로 깨진다.
		// 코드도 #if WITH_EDITOR 로 함께 막혀 있어 데디 서버 바이너리에는 심볼이 남지 않는다.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",       // ThumbnailTools (ObjectTools.h) · GEditor
				"ContentBrowser", // 지금 선택된 에셋 조회
				"AssetRegistry",  // 경로 밑 에셋 일괄 조회
				// FObjectThumbnail::GetImage() 의 본체는 Misc/ObjectThumbnail.inl 에 있고,
				// 그 .inl 전체가 #if defined(IMAGECORE_API) 로 감싸여 있다. 이 의존이 빠지면
				// 매크로가 정의되지 않아 .inl 이 통째로 비고 — 컴파일은 통과한 뒤 링크에서
				// "GetImage 미해결 외부 심볼"로 터진다. 원인 찾기 아주 고약한 자리다.
				"ImageCore",
			});
		}

		// 도메인 폴더를 짧은 경로로 include 하기 위한 설정.
		//   #include "Voxel/VoxelGrid.h"  (O)
		//   #include "../../Voxel/VoxelGrid.h"  (X)
		PublicIncludePaths.Add(ModuleDirectory);

	}
}
