using UnrealBuildTool;

public class LervikMCPTestProjectEditorTarget : TargetRules
{
	public LervikMCPTestProjectEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V4;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("LervikMCPTestProject");
	}
}
