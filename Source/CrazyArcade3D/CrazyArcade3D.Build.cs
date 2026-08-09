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

		// 도메인 폴더를 짧은 경로로 include 하기 위한 설정.
		//   #include "Voxel/VoxelGrid.h"  (O)
		//   #include "../../Voxel/VoxelGrid.h"  (X)
		PublicIncludePaths.Add(ModuleDirectory);

	}
}
