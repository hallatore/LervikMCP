#include "LervikMCPEditorModule.h"
#include "IMCPTool.h"
#include "Features/IModularFeatures.h"
#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Tools/MCPTool_GetOpenAssets.h"
#include "Tools/MCPTool_Find.h"
#include "Tools/MCPTool_Inspect.h"
#include "Tools/MCPTool_Modify.h"
#include "Tools/MCPTool_Create.h"
#include "Tools/MCPTool_Delete.h"
#include "Tools/MCPTool_Editor.h"
#include "Tools/MCPTool_Graph.h"
#include "Tools/MCPTool_ExecutePython.h"
#include "Tools/MCPTool_ExecuteEditor.h"

void FLervikMCPEditorModule::StartupModule()
{
    // Unregister the runtime execute tool so we can replace it with the editor version
    TArray<IMCPTool*> RegisteredTools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(IMCPTool::GetModularFeatureName());
    for (IMCPTool* Tool : RegisteredTools)
    {
        if (Tool->GetToolInfo().Name == TEXT("execute"))
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), Tool);
            break;
        }
    }

    Tools.Add(MakeUnique<FMCPTool_ExecuteEditor>());
    Tools.Add(MakeUnique<FMCPTool_GetOpenAssets>());
    Tools.Add(MakeUnique<FMCPTool_Find>());
    Tools.Add(MakeUnique<FMCPTool_Inspect>());
    Tools.Add(MakeUnique<FMCPTool_Modify>());
    Tools.Add(MakeUnique<FMCPTool_Create>());
    Tools.Add(MakeUnique<FMCPTool_Delete>());
    Tools.Add(MakeUnique<FMCPTool_Editor>());
    Tools.Add(MakeUnique<FMCPTool_Graph>());
    Tools.Add(MakeUnique<FMCPTool_ExecutePython>());

    for (const auto& Tool : Tools)
    {
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), Tool.Get());
    }

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FLervikMCPEditorModule::RegisterMenus));
}

void FLervikMCPEditorModule::ShutdownModule()
{
    UToolMenus::UnregisterOwner(this);

    for (const auto& Tool : Tools)
    {
        IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), Tool.Get());
    }
    Tools.Empty();
}

void FLervikMCPEditorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
    FToolMenuSection& Section = ToolBar->FindOrAddSection("Play");

    FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
        "MCPMenu",
        FUIAction(),
        FNewMenuDelegate::CreateLambda([](FMenuBuilder& MenuBuilder)
        {
            IConsoleVariable* EnableCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.enable"));

            MenuBuilder.AddMenuEntry(
                NSLOCTEXT("MCPEditor", "EnableMCP", "Enable"),
                NSLOCTEXT("MCPEditor", "EnableMCPTooltip", "Toggle MCP server"),
                FSlateIcon(),
                FUIAction(
                    FExecuteAction::CreateLambda([EnableCVar]()
                    {
                        if (EnableCVar)
                        {
                            EnableCVar->Set(EnableCVar->GetInt() ? 0 : 1, ECVF_SetByConsole);
                        }
                    }),
                    FCanExecuteAction(),
                    FIsActionChecked::CreateLambda([EnableCVar]() -> bool
                    {
                        return EnableCVar && EnableCVar->GetInt() != 0;
                    })
                ),
                NAME_None,
                EUserInterfaceActionType::ToggleButton
            );

            IConsoleVariable* PortCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.port"));
            bool bEnabled = EnableCVar && EnableCVar->GetInt() != 0;
            int32 Port = PortCVar ? PortCVar->GetInt() : 8090;

            FText StatusText = bEnabled
                ? FText::Format(NSLOCTEXT("MCPEditor", "RunningStatus", "Running :{0}"), FText::FromString(FString::FromInt(Port)))
                : NSLOCTEXT("MCPEditor", "StoppedStatus", "Stopped");

            FString ServerUrl = FString::Printf(TEXT("http://localhost:%d/mcp"), Port);

            MenuBuilder.AddMenuEntry(
                StatusText,
                bEnabled
                    ? FText::Format(NSLOCTEXT("MCPEditor", "CopyUrlTooltip", "Copy {0} to clipboard"), FText::FromString(ServerUrl))
                    : NSLOCTEXT("MCPEditor", "StoppedTooltip", "Server is not running"),
                FSlateIcon(),
                FUIAction(
                    FExecuteAction::CreateLambda([ServerUrl, bEnabled]()
                    {
                        if (bEnabled)
                        {
                            FPlatformApplicationMisc::ClipboardCopy(*ServerUrl);
                        }
                    }),
                    FCanExecuteAction::CreateLambda([bEnabled]() { return bEnabled; })
                )
            );
        }),
        NSLOCTEXT("MCPEditor", "MCPLabel", "MCP"),
        NSLOCTEXT("MCPEditor", "MCPTooltip", "MCP Server Options"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Graph.Replication.AuthorityOnly")
    );
    Entry.StyleNameOverride = "CalloutToolbar";
    Entry.InsertPosition = FToolMenuInsert("PlatformsMenu", EToolMenuInsertType::After);
    Section.AddEntry(Entry);
}

IMPLEMENT_MODULE(FLervikMCPEditorModule, LervikMCPEditor)
