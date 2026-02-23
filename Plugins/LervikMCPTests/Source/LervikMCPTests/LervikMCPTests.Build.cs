using UnrealBuildTool;

public class LervikMCPTests : ModuleRules
{
    public LervikMCPTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "LervikMCP",
            "HTTP", "HTTPServer",
            "Json", "JsonUtilities",
            "UnrealEd",
            "BlueprintGraph",
            "KismetCompiler",
            "AssetTools",
            "AssetRegistry",
            "MaterialEditor",
        });
    }
}
