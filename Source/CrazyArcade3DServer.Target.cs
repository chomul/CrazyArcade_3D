using UnrealBuildTool;
using System.Collections.Generic;

// 데디케이트 서버 타깃 (GDD 8장 2주차 / 구조설계서 1주차 1번 항목).
// 1주차부터 존재해야 하는 이유: 서버 빌드에서만 터지는 컴파일 에러
// (에디터 전용 헤더 참조 등)를 즉시 발견하기 위함.
public class CrazyArcade3DServerTarget : TargetRules
{
	public CrazyArcade3DServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("CrazyArcade3D");
	}
}
