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
