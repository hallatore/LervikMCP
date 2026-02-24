#include "Tools/MCPTool_ExecuteEditor.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPToolHelp.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    struct FMCPStringOutputDevice : public FOutputDevice
    {
        FString Log;
        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            Log += V;
            Log += TEXT("\n");
        }
        virtual bool IsMemoryOnly() const override { return true; }
    };

    bool IsBlockedCommand(const FString& Command)
    {
        static const TArray<FString> BlockList = { TEXT("exit"), TEXT("quit"), TEXT("shutdown") };
        FString Trimmed = Command.TrimStart();
        for (const FString& Blocked : BlockList)
        {
            if (Trimmed.StartsWith(Blocked, ESearchCase::IgnoreCase))
            {
                int32 Len = Blocked.Len();
                if (Trimmed.Len() == Len || FChar::IsWhitespace(Trimmed[Len]))
                {
                    return true;
                }
            }
        }
        return false;
    }
}

namespace
{
    static const FMCPParamHelp sExecCommandParams[] = {
        { TEXT("command"), TEXT("string"), true, TEXT("Console command to execute"), nullptr, TEXT("obj list class=Actor") },
    };

    static const FMCPParamHelp sExecGetCvarParams[] = {
        { TEXT("name"), TEXT("string"), true, TEXT("Console variable name"), nullptr, TEXT("r.ScreenPercentage") },
    };

    static const FMCPParamHelp sExecSetCvarParams[] = {
        { TEXT("name"),  TEXT("string"), true, TEXT("Console variable name"), nullptr, TEXT("r.ScreenPercentage") },
        { TEXT("value"), TEXT("string"), true, TEXT("Value to set"), nullptr, TEXT("100") },
    };

    static const FMCPParamHelp sExecListCvarsParams[] = {
        { TEXT("filter"),      TEXT("string"),  false, TEXT("Prefix or wildcard filter for variable names"), nullptr, TEXT("r.Shadow*") },
        { TEXT("includeHelp"), TEXT("boolean"), false, TEXT("Include help text and type. Default: false"), nullptr, nullptr },
    };

    static const FMCPActionHelp sExecEditorActions[] = {
        { TEXT("command"),    TEXT("Execute a console command in the editor"), sExecCommandParams, UE_ARRAY_COUNT(sExecCommandParams), nullptr },
        { TEXT("get_cvar"),   TEXT("Get the current value of a console variable"), sExecGetCvarParams, UE_ARRAY_COUNT(sExecGetCvarParams), nullptr },
        { TEXT("set_cvar"),   TEXT("Set a console variable value"), sExecSetCvarParams, UE_ARRAY_COUNT(sExecSetCvarParams), nullptr },
        { TEXT("list_cvars"), TEXT("List console variables matching a filter"), sExecListCvarsParams, UE_ARRAY_COUNT(sExecListCvarsParams), nullptr },
    };

    static const FMCPToolHelpData sExecEditorHelp = {
        TEXT("execute"),
        TEXT("Execute console commands or get/set/list console variables in the UE5 editor"),
        TEXT("action"),
        sExecEditorActions, UE_ARRAY_COUNT(sExecEditorActions),
        nullptr, 0
    };
}

FMCPToolInfo FMCPTool_ExecuteEditor::GetToolInfo() const
{
    FMCPToolInfo Info = FMCPTool_Execute::GetToolInfo();
    Info.Description = TEXT("Execute console commands or get/set/list console variables in the UE5 editor");
    Info.Parameters.Add({ TEXT("help"), TEXT("Pass help=true for overview, help='action_name' for detailed parameter info"), TEXT("string"), false });
    return Info;
}

FMCPToolResult FMCPTool_ExecuteEditor::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sExecEditorHelp, HelpResult))
        return HelpResult;

    FString Action;
    if (!Params->TryGetStringField(TEXT("action"), Action))
    {
        return FMCPToolResult::Error(TEXT("'action' is required"));
    }

    if (Action.Equals(TEXT("command"), ESearchCase::IgnoreCase))
    {
        return ExecuteOnGameThread([Params]() -> FMCPToolResult
        {
            FString Command;
            if (!Params->TryGetStringField(TEXT("command"), Command))
            {
                return FMCPToolResult::Error(TEXT("'command' is required for action=command"));
            }

            if (IsBlockedCommand(Command))
            {
                return FMCPToolResult::Error(FString::Printf(
                    TEXT("Command '%s' is not permitted"), *Command));
            }

            if (!GEditor)
            {
                return FMCPToolResult::Error(TEXT("Editor not available"));
            }

            FMCPStringOutputDevice OutputDevice;
            UWorld* World = GEditor->GetEditorWorldContext().World();
            GLog->AddOutputDevice(&OutputDevice);
            GEditor->Exec(World, *Command, OutputDevice);
            GLog->RemoveOutputDevice(&OutputDevice);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("command"), Command);
            Result->SetStringField(TEXT("output"), OutputDevice.Log);
            return FMCPJsonHelpers::SuccessResponse(Result);
        });
    }

    // Delegate all other actions to the runtime base class
    return FMCPTool_Execute::Execute(Params);
}
