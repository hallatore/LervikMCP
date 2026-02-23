#include "Tools/MCPTool_GetOpenAssets.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

FMCPToolInfo FMCPTool_GetOpenAssets::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("get_open_assets");
    Info.Description = TEXT("Returns the name, path and type of all currently open assets in the editor");
    return Info;
}

FMCPToolResult FMCPTool_GetOpenAssets::Execute(const TSharedPtr<FJsonObject>& Params)
{
    auto DoWork = []() -> FMCPToolResult
    {
        if (!GEditor)
        {
            return FMCPToolResult::Error(TEXT("Editor not available"));
        }

        UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!AssetEditorSubsystem)
        {
            return FMCPToolResult::Error(TEXT("AssetEditorSubsystem not available"));
        }

        TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();

        TArray<TSharedPtr<FJsonValue>> AssetsArray;
        for (UObject* Asset : EditedAssets)
        {
            if (!Asset) continue;

            TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
            AssetObj->SetStringField(TEXT("name"), Asset->GetName());
            AssetObj->SetStringField(TEXT("path"), Asset->GetPathName());
            AssetObj->SetStringField(TEXT("type"), Asset->GetClass()->GetName());
            AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
        }

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetArrayField(TEXT("assets"), AssetsArray);
        ResultObj->SetNumberField(TEXT("count"), AssetsArray.Num());

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
        Writer->Close();

        return FMCPToolResult::Text(OutputString);
    };

    if (IsInGameThread())
    {
        return DoWork();
    }

    FMCPToolResult Result;
    FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);

    AsyncTask(ENamedThreads::GameThread, [&Result, &DoWork, DoneEvent]()
    {
        Result = DoWork();
        DoneEvent->Trigger();
    });

    DoneEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
    return Result;
}
