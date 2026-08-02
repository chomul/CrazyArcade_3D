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

			// EOS 세션·로비 (Task 19). 전부 Private — 공개 헤더(CA3DGameInstance.h)는
			// OSS 타입을 노출하지 않으므로 이 의존이 다른 파일로 번지지 않는다.
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"CoreOnline",
			// EOS_Connect_CreateDeviceId 직접 호출용. OSS EOS 가 감싸지 않는 유일한 단계다
			// (Device ID 자격이 로컬에 없으면 로그인이 EOS_NotFound 로 떨어진다).
			"EOSShared",
			"EOSSDK",
		});

		// 도메인 폴더를 짧은 경로로 include 하기 위한 설정.
		//   #include "Voxel/VoxelGrid.h"  (O)
		//   #include "../../Voxel/VoxelGrid.h"  (X)
		PublicIncludePaths.Add(ModuleDirectory);

	}
}
