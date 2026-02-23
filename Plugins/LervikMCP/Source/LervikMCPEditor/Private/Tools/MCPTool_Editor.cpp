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
#include "LevelEditorViewport.h"
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
    Info.Description = TEXT("Editor state management: open, close, select, deselect, focus, save, navigate assets/actors, or get_viewport_info");
    Info.Parameters  = {
        { TEXT("action"), TEXT("Values: open|close|save|select|deselect|focus|navigate|get_viewport_info. open/close/save operate on assets. select/deselect/focus operate on level actors. navigate syncs Content Browser. get_viewport_info returns viewport/camera/PIE state"), TEXT("string"),       true  },
        { TEXT("target"), TEXT("Asset path(s) or actor label(s). String or array. deselect with no target deselects all"), TEXT("string|array"), false, TEXT("string") },
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

        // ── get_viewport_info ────────────────────────────────────────────
        if (Action.Equals(TEXT("get_viewport_info"), ESearchCase::IgnoreCase))
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"), TEXT("get_viewport_info"));

            // PIE status
            TSharedPtr<FJsonObject> PieObj = MakeShared<FJsonObject>();
            const bool bPlaying = GEditor->IsPlaySessionInProgress();
            const bool bSimulating = GEditor->IsSimulatingInEditor();
            PieObj->SetBoolField(TEXT("active"), bPlaying);
            PieObj->SetStringField(TEXT("mode"), bSimulating ? TEXT("SIE") : (bPlaying ? TEXT("PIE") : TEXT("none")));
            Result->SetObjectField(TEXT("pie"), PieObj);

            // Viewports
            TArray<TSharedPtr<FJsonValue>> ViewportArray;
            const TArray<FLevelEditorViewportClient*>& Clients = GEditor->GetLevelViewportClients();

            for (int32 i = 0; i < Clients.Num(); ++i)
            {
                FLevelEditorViewportClient* Client = Clients[i];
                if (!Client)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> VpObj = MakeShared<FJsonObject>();
                VpObj->SetNumberField(TEXT("index"), i);
                VpObj->SetBoolField(TEXT("active"), Client == GCurrentLevelEditingViewportClient);

                // Viewport type
                const ELevelViewportType VpType = Client->GetViewportType();
                const bool bPerspective = Client->IsPerspective();
                const TCHAR* TypeStr = TEXT("unknown");
                switch (VpType)
                {
                case LVT_Perspective:      TypeStr = TEXT("perspective"); break;
                case LVT_OrthoXY:          TypeStr = TEXT("top"); break;
                case LVT_OrthoXZ:          TypeStr = TEXT("front"); break;
                case LVT_OrthoYZ:          TypeStr = TEXT("left"); break;
                case LVT_OrthoNegativeXY:  TypeStr = TEXT("bottom"); break;
                case LVT_OrthoNegativeXZ:  TypeStr = TEXT("back"); break;
                case LVT_OrthoNegativeYZ:  TypeStr = TEXT("right"); break;
                case LVT_OrthoFreelook:    TypeStr = TEXT("ortho_freelook"); break;
                default: break;
                }
                VpObj->SetStringField(TEXT("type"), TypeStr);
                VpObj->SetBoolField(TEXT("is_perspective"), bPerspective);
                VpObj->SetBoolField(TEXT("realtime"), Client->IsRealtime());

                // FOV (perspective only)
                if (bPerspective)
                {
                    VpObj->SetField(TEXT("fov"), FMCPJsonHelpers::RoundedJsonNumber(Client->ViewFOV, 1));
                }

                // Dimensions — skip zero-size (null Viewport or collapsed) viewports
                int32 W = 0, H = 0;
                if (Client->Viewport)
                {
                    const FIntPoint Size = Client->Viewport->GetSizeXY();
                    W = Size.X;
                    H = Size.Y;
                }
                if (W == 0 || H == 0)
                {
                    continue;
                }
                VpObj->SetNumberField(TEXT("width"), W);
                VpObj->SetNumberField(TEXT("height"), H);

                // Camera
                TSharedPtr<FJsonObject> CamObj = MakeShared<FJsonObject>();
                const FVector Loc = Client->GetViewLocation();
                TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
                LocObj->SetField(TEXT("x"), FMCPJsonHelpers::RoundedJsonNumber(Loc.X));
                LocObj->SetField(TEXT("y"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Y));
                LocObj->SetField(TEXT("z"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Z));
                CamObj->SetObjectField(TEXT("location"), LocObj);

                const FRotator Rot = Client->GetViewRotation();
                TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
                RotObj->SetField(TEXT("pitch"), FMCPJsonHelpers::RoundedJsonNumber(Rot.Pitch));
                RotObj->SetField(TEXT("yaw"), FMCPJsonHelpers::RoundedJsonNumber(Rot.Yaw));
                RotObj->SetField(TEXT("roll"), FMCPJsonHelpers::RoundedJsonNumber(Rot.Roll));
                CamObj->SetObjectField(TEXT("rotation"), RotObj);

                VpObj->SetObjectField(TEXT("camera"), CamObj);
                ViewportArray.Add(MakeShared<FJsonValueObject>(VpObj));
            }

            Result->SetArrayField(TEXT("viewports"), ViewportArray);
            Result->SetNumberField(TEXT("count"), ViewportArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown action: '%s'. Valid: open, close, select, deselect, focus, save, navigate, get_viewport_info"), *Action));
    });
}

#undef LOCTEXT_NAMESPACE
