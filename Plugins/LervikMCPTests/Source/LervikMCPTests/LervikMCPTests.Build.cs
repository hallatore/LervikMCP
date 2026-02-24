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
            "LervikMCPEditor",
            "HTTP", "HTTPServer",
            "Json", "JsonUtilities",
            "UnrealEd",
            "BlueprintGraph",
            "KismetCompiler",
            "AssetTools",
            "AssetRegistry",
            "MaterialEditor",
        });

        // Access LervikMCPEditor private headers for test sync guards
        PrivateIncludePaths.Add(System.IO.Path.GetFullPath(
            System.IO.Path.Combine(ModuleDirectory, "../../../LervikMCP/Source/LervikMCPEditor/Private")));
    }
}
