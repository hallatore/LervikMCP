using UnrealBuildTool;

public class LervikMCP : ModuleRules
{
    public LervikMCP(ReadOnlyTargetRules Target) : base(Target)
    {
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		PCHUsage = PCHUsageMode.NoSharedPCHs;
		PrivatePCHHeaderFile = "Private/LervikMCPPrivatePCH.h";
		bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities" });
        PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "HTTPServer" });

        if (Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "TraceServices", "TraceAnalysis" });
            PrivateDefinitions.Add("LERVIKMCP_WITH_TRACE_ANALYSIS=1");
        }
        else
        {
            PrivateDefinitions.Add("LERVIKMCP_WITH_TRACE_ANALYSIS=0");
        }
    }
}
