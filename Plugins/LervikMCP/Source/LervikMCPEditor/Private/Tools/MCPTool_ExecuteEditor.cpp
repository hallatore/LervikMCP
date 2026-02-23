#include "Tools/MCPTool_ExecuteEditor.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
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

FMCPToolInfo FMCPTool_ExecuteEditor::GetToolInfo() const
{
    FMCPToolInfo Info = FMCPTool_Execute::GetToolInfo();
    Info.Description = TEXT("Execute console commands or get/set/list console variables in the UE5 editor");
    return Info;
}

FMCPToolResult FMCPTool_ExecuteEditor::Execute(const TSharedPtr<FJsonObject>& Params)
{
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
