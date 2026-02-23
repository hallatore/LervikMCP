#include "Tools/MCPTool_Editor.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPObjectResolver.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "GameFramework/Actor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "LervikMCP"

namespace
{
    TArray<FString> ParseTargets(const TSharedPtr<FJsonObject>& Params)
    {
        TArray<FString> Targets;
        FString TargetStr;
        const TArray<TSharedPtr<FJsonValue>>* TargetArr = nullptr;

        if (Params->TryGetStringField(TEXT("target"), TargetStr))
        {
            Targets.Add(TargetStr);
        }
        else if (Params->TryGetArrayField(TEXT("target"), TargetArr))
        {
            for (const TSharedPtr<FJsonValue>& Val : *TargetArr)
            {
                FString S;
                if (Val->TryGetString(S))
                {
                    Targets.Add(S);
                }
            }
        }
        return Targets;
    }
}

FMCPToolInfo FMCPTool_Editor::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("editor");
    Info.Description = TEXT("Editor state management: open, close, select, deselect, focus, save, or navigate assets and actors");
    Info.Parameters  = {
        { TEXT("action"), TEXT("'open'/'close'/'save' (assets), 'select'/'deselect'/'focus' (level actors), 'navigate' (syncs Content Browser to asset path)"),          TEXT("string"),       true  },
        { TEXT("target"), TEXT("Asset path(s) or actor label(s). Pass as array for multiple targets. For 'deselect' with no target, deselects all."), TEXT("string|array"), false, TEXT("string") },
    };
    return Info;
}

FMCPToolResult FMCPTool_Editor::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Action;
        if (!Params->TryGetStringField(TEXT("action"), Action))
        {
            return FMCPToolResult::Error(TEXT("'action' is required"));
        }

        if (!GEditor)
        {
            return FMCPToolResult::Error(TEXT("Editor not available"));
        }

        TArray<FString> Targets = ParseTargets(Params);
        TArray<FString> Handled;
        TArray<FString> Warnings;

        // ── open ─────────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("open"), ESearchCase::IgnoreCase))
        {
            UAssetEditorSubsystem* EditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            if (!EditorSub)
            {
                return FMCPToolResult::Error(TEXT("AssetEditorSubsystem not available"));
            }

            for (const FString& Target : Targets)
            {
                FString Error;
                UObject* Asset = FMCPObjectResolver::ResolveAsset(Target, Error);
                if (!Asset)
                {
                    Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                    continue;
                }
                EditorSub->OpenEditorForAsset(Asset);
                Handled.Add(Target);
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("open"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── close ────────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("close"), ESearchCase::IgnoreCase))
        {
            UAssetEditorSubsystem* EditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            if (!EditorSub)
            {
                return FMCPToolResult::Error(TEXT("AssetEditorSubsystem not available"));
            }

            for (const FString& Target : Targets)
            {
                FString Error;
                UObject* Asset = FMCPObjectResolver::ResolveAsset(Target, Error);
                if (!Asset)
                {
                    Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                    continue;
                }
                EditorSub->CloseAllEditorsForAsset(Asset);
                Handled.Add(Target);
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("close"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── select ───────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("select"), ESearchCase::IgnoreCase))
        {
            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            TArray<AActor*> ActorsToSelect;
            for (const FString& Target : Targets)
            {
                FString Error;
                AActor* Actor = FMCPObjectResolver::ResolveActor(Target, Error);
                if (Actor)
                {
                    ActorsToSelect.Add(Actor);
                    Handled.Add(Target);
                }
                else
                {
                    Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                }
            }

            ActorSub->SetSelectedLevelActors(ActorsToSelect);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("select"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── deselect ─────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("deselect"), ESearchCase::IgnoreCase))
        {
            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            if (Targets.IsEmpty())
            {
                ActorSub->ClearActorSelectionSet();
            }
            else
            {
                for (const FString& Target : Targets)
                {
                    FString Error;
                    AActor* Actor = FMCPObjectResolver::ResolveActor(Target, Error);
                    if (Actor)
                    {
                        ActorSub->SetActorSelectionState(Actor, false);
                        Handled.Add(Target);
                    }
                    else
                    {
                        Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                    }
                }
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("deselect"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── focus ────────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("focus"), ESearchCase::IgnoreCase))
        {
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' is required for action=focus"));
            }

            FString Error;
            AActor* Actor = FMCPObjectResolver::ResolveActor(Targets[0], Error);
            if (!Actor)
            {
                return FMCPToolResult::Error(Error);
            }

            GEditor->MoveViewportCamerasToActor(*Actor, false);
            Handled.Add(Targets[0]);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("focus"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── save ─────────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("save"), ESearchCase::IgnoreCase))
        {
            UEditorAssetSubsystem* AssetSub = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
            if (!AssetSub)
            {
                return FMCPToolResult::Error(TEXT("EditorAssetSubsystem not available"));
            }

            for (const FString& Target : Targets)
            {
                if (AssetSub->SaveAsset(Target))
                {
                    Handled.Add(Target);
                }
                else
                {
                    Warnings.Add(FString::Printf(TEXT("Failed to save '%s'"), *Target));
                }
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("save"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── navigate ─────────────────────────────────────────────────────────
        if (Action.Equals(TEXT("navigate"), ESearchCase::IgnoreCase))
        {
            TArray<FAssetData> AssetDataArray;
            for (const FString& Target : Targets)
            {
                FString Error;
                UObject* Asset = FMCPObjectResolver::ResolveAsset(Target, Error);
                if (Asset)
                {
                    AssetDataArray.Add(FAssetData(Asset));
                    Handled.Add(Target);
                }
                else
                {
                    Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                }
            }

            if (AssetDataArray.Num() > 0)
            {
                FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
                CBModule.Get().SyncBrowserToAssets(AssetDataArray);
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),  TEXT("navigate"));
            Result->SetArrayField(TEXT("targets"),  FMCPJsonHelpers::ArrayFromStrings(Handled));
            Result->SetNumberField(TEXT("count"),   Handled.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown action: '%s'. Valid: open, close, select, deselect, focus, save, navigate"), *Action));
    });
}

#undef LOCTEXT_NAMESPACE
