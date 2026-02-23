using UnrealBuildTool;

public class LervikMCPTestProjectTarget : TargetRules
{
	public LervikMCPTestProjectTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("LervikMCPTestProject");
	}
}
