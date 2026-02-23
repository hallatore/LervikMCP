using UnrealBuildTool;

public class LervikMCPEditor : ModuleRules
{
    public LervikMCPEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		PCHUsage = PCHUsageMode.NoSharedPCHs;
		PrivatePCHHeaderFile = "Private/LervikMCPEditorPrivatePCH.h";
		bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "LervikMCP", "UnrealEd", "Json",
            "AssetRegistry", "AssetTools", "BlueprintGraph", "KismetCompiler", "Kismet",
            "MaterialEditor", "ContentBrowser", "LevelEditor", "JsonUtilities",
            "Slate", "SlateCore", "EditorSubsystem", "PythonScriptPlugin",
            "ToolMenus", "ApplicationCore"
        });
    }
}
