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
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UMG",
		});

		// 도메인 폴더를 짧은 경로로 include 하기 위한 설정.
		//   #include "Voxel/VoxelGrid.h"  (O)
		//   #include "../../Voxel/VoxelGrid.h"  (X)
		PublicIncludePaths.Add(ModuleDirectory);

		// 2주차에 추가 예정:
		//   PrivateDependencyModuleNames.AddRange(new string[] { "OnlineSubsystem", "OnlineSubsystemUtils" });
		//   EOS 플러그인은 .uproject 의 Plugins 섹션에 등록
	}
}
