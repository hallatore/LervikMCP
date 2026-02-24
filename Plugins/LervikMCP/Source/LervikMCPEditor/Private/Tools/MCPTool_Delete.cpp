#include "Tools/MCPTool_Delete.h"
#include "MCPGameThreadHelper.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"
#include "MCPToolHelp.h"
#include "MCPObjectResolver.h"

#include "ScopedTransaction.h"
#include "Editor.h"

#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "GameFramework/Actor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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

    // ── Help data ────────────────────────────────────────────────────────

    static const FMCPParamHelp sDeleteAssetParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Asset path(s) to delete"), nullptr, TEXT("/Game/BP_OldActor") },
    };

    static const FMCPParamHelp sDeleteActorParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Actor label(s) to delete"), nullptr, TEXT("PointLight_0") },
    };

    static const FMCPParamHelp sDeleteNodeParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Node GUID(s) to delete"), nullptr, nullptr },
        { TEXT("parent"), TEXT("string"), true, TEXT("Owning Blueprint or Material path"), nullptr, TEXT("/Game/BP_MyActor") },
        { TEXT("graph"),  TEXT("string"), false, TEXT("Graph name. Default: EventGraph"), nullptr, nullptr },
    };

    static const FMCPParamHelp sDeleteVarParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Variable name(s) to delete"), nullptr, nullptr },
        { TEXT("parent"), TEXT("string"), true, TEXT("Owning Blueprint path"), nullptr, nullptr },
    };

    static const FMCPParamHelp sDeleteExprParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Expression GUID(s) to delete"), nullptr, nullptr },
        { TEXT("parent"), TEXT("string"), true, TEXT("Owning Material path"), nullptr, nullptr },
    };

    static const FMCPParamHelp sDeleteCompParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Component name(s) to delete"), nullptr, nullptr },
        { TEXT("parent"), TEXT("string"), true, TEXT("Owning Blueprint path"), nullptr, nullptr },
    };

    static const FMCPParamHelp sDeleteConnParams[] = {
        { TEXT("parent"),     TEXT("string"), true, TEXT("Owning Blueprint or Material path"), nullptr, nullptr },
        { TEXT("pin_source"), TEXT("object"), true, TEXT("Output pin {\"node\":\"GUID\",\"pin\":\"PinName\"}"), nullptr, nullptr },
        { TEXT("pin_dest"),   TEXT("object"), true, TEXT("Input pin {\"node\":\"GUID\",\"pin\":\"PinName\"}"), nullptr, nullptr },
    };

    static const FMCPParamHelp sDeleteFolderParams[] = {
        { TEXT("target"), TEXT("string|array"), true, TEXT("Content Browser folder path(s)"), nullptr, TEXT("/Game/OldFolder") },
    };

    static const FMCPActionHelp sDeleteActions[] = {
        { TEXT("asset"),      TEXT("Delete asset(s) from Content Browser"), sDeleteAssetParams, UE_ARRAY_COUNT(sDeleteAssetParams), nullptr },
        { TEXT("actor"),      TEXT("Delete actor(s) from the level"), sDeleteActorParams, UE_ARRAY_COUNT(sDeleteActorParams), nullptr },
        { TEXT("node"),       TEXT("Delete Blueprint graph node(s)"), sDeleteNodeParams, UE_ARRAY_COUNT(sDeleteNodeParams), nullptr },
        { TEXT("variable"),   TEXT("Delete Blueprint variable(s)"), sDeleteVarParams, UE_ARRAY_COUNT(sDeleteVarParams), nullptr },
        { TEXT("expression"), TEXT("Delete Material expression(s)"), sDeleteExprParams, UE_ARRAY_COUNT(sDeleteExprParams), nullptr },
        { TEXT("component"),  TEXT("Delete Blueprint component(s)"), sDeleteCompParams, UE_ARRAY_COUNT(sDeleteCompParams), nullptr },
        { TEXT("connection"), TEXT("Disconnect a specific pin connection"), sDeleteConnParams, UE_ARRAY_COUNT(sDeleteConnParams), nullptr },
        { TEXT("folder"),     TEXT("Delete Content Browser folder(s)"), sDeleteFolderParams, UE_ARRAY_COUNT(sDeleteFolderParams), nullptr },
    };

    static const FMCPToolHelpData sDeleteHelp = {
        TEXT("delete"),
        TEXT("Delete assets, actors, or Blueprint/material elements"),
        TEXT("type"),
        sDeleteActions, UE_ARRAY_COUNT(sDeleteActions),
        nullptr, 0
    };
}

FMCPToolInfo FMCPTool_Delete::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("delete");
    Info.Description = TEXT("Delete assets, actors, or Blueprint/material elements in the UE5 editor");
    Info.Parameters  = {
        { TEXT("type"),       TEXT("Values: asset|actor|node|variable|expression|component|connection|folder"),              TEXT("string"),       true  },
        { TEXT("target"),     TEXT("Path, label, or GUID(s). String or array for batch. [connection] Not used"),            TEXT("string|array"), false, TEXT("string") },
        { TEXT("parent"),     TEXT("[node|variable|expression|component|connection] Owning Blueprint or Material path"),   TEXT("string"), false },
        { TEXT("graph"),      TEXT("[node] Graph name. Default: EventGraph"),                                              TEXT("string"),       false },
        { TEXT("pin_source"), TEXT("[connection] Output pin. Format: {\"node\":\"GUID\",\"pin\":\"PinName\"}"),              TEXT("object"),       false },
        { TEXT("pin_dest"),   TEXT("[connection] Input pin. Format: {\"node\":\"GUID\",\"pin\":\"PinName\"}"),               TEXT("object"),       false },
        { TEXT("help"),       TEXT("Pass help=true for overview, help='type_name' for detailed parameter info"), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Delete::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sDeleteHelp, HelpResult))
        return HelpResult;

    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Type;
        if (!Params->TryGetStringField(TEXT("type"), Type))
        {
            return FMCPToolResult::Error(TEXT("'type' is required"));
        }

        if (!GEditor)
        {
            return FMCPToolResult::Error(TEXT("Editor not available"));
        }

        TArray<FString> Deleted;

        // ── type=asset ───────────────────────────────────────────────────────
        if (Type.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' is required for type=asset"));
            }

            UEditorAssetSubsystem* AssetSub = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
            if (!AssetSub)
            {
                return FMCPToolResult::Error(TEXT("EditorAssetSubsystem not available"));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteAsset", "MCP Delete Asset"));
            for (const FString& Target : Targets)
            {
                if (AssetSub->DeleteAsset(Target))
                {
                    Deleted.Add(Target);
                }
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=actor ───────────────────────────────────────────────────────
        if (Type.Equals(TEXT("actor"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' is required for type=actor"));
            }

            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteActor", "MCP Delete Actor"));
            TArray<FString> Warnings;
            for (const FString& Target : Targets)
            {
                FString Error;
                AActor* Actor = FMCPObjectResolver::ResolveActor(Target, Error);
                if (!Actor)
                {
                    Warnings.Add(Error.IsEmpty() ? FString::Printf(TEXT("Could not resolve '%s'"), *Target) : Error);
                    continue;
                }
                FString Label = Actor->GetActorLabel();
                if (ActorSub->DestroyActor(Actor))
                {
                    Deleted.Add(Label);
                }
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=node ────────────────────────────────────────────────────────
        if (Type.Equals(TEXT("node"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' (node GUID) is required for type=node"));
            }

            FString ParentStr;
            if (!Params->TryGetStringField(TEXT("parent"), ParentStr) || ParentStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'parent' (Blueprint path) is required for type=node"));
            }

            FString ResolveError;
            UObject* ParentObj = FMCPObjectResolver::ResolveAsset(ParentStr, ResolveError);
            UBlueprint* Blueprint = Cast<UBlueprint>(ParentObj);
            if (!Blueprint)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint: %s"), *ParentStr, *ResolveError));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteNode", "MCP Delete Blueprint Node"));
            Blueprint->Modify();

            for (const FString& Target : Targets)
            {
                UEdGraphNode* Node = FMCPGraphHelpers::FindNodeByGuid(Blueprint, Target);
                if (!Node)
                {
                    continue;
                }
                FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
                Deleted.Add(Target);
            }

            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=variable ────────────────────────────────────────────────────
        if (Type.Equals(TEXT("variable"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' (variable name) is required for type=variable"));
            }

            FString ParentStr;
            if (!Params->TryGetStringField(TEXT("parent"), ParentStr) || ParentStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'parent' (Blueprint path) is required for type=variable"));
            }

            FString ResolveError;
            UObject* ParentObj = FMCPObjectResolver::ResolveAsset(ParentStr, ResolveError);
            UBlueprint* Blueprint = Cast<UBlueprint>(ParentObj);
            if (!Blueprint)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint: %s"), *ParentStr, *ResolveError));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteVariable", "MCP Delete Blueprint Variable"));
            Blueprint->Modify();

            for (const FString& Target : Targets)
            {
                FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*Target));
                Deleted.Add(Target);
            }

            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=expression ──────────────────────────────────────────────────
        if (Type.Equals(TEXT("expression"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' (expression GUID) is required for type=expression"));
            }

            FString ParentStr;
            if (!Params->TryGetStringField(TEXT("parent"), ParentStr) || ParentStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'parent' (Material path) is required for type=expression"));
            }

            FString ResolveError;
            UObject* ParentObj = FMCPObjectResolver::ResolveAsset(ParentStr, ResolveError);
            UMaterial* Material = Cast<UMaterial>(ParentObj);
            if (!Material)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Material: %s"), *ParentStr, *ResolveError));
            }

            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteExpression", "MCP Delete Material Expression"));
            Material->Modify();
            Material->PreEditChange(nullptr);

            for (const FString& Target : Targets)
            {
                UMaterialExpression* FoundExpr = FMCPGraphHelpers::FindExpressionByGuid(Material, Target);

                if (FoundExpr)
                {
                    Material->GetExpressionCollection().RemoveExpression(FoundExpr);
                    Deleted.Add(Target);
                }
            }

            Material->PostEditChange();
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=component ───────────────────────────────────────────────────
        if (Type.Equals(TEXT("component"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' (component name) is required for type=component"));
            }

            FString ParentStr;
            if (!Params->TryGetStringField(TEXT("parent"), ParentStr) || ParentStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'parent' (Blueprint path) is required for type=component"));
            }

            FString ResolveError;
            UObject* ParentObj = FMCPObjectResolver::ResolveAsset(ParentStr, ResolveError);
            UBlueprint* Blueprint = Cast<UBlueprint>(ParentObj);
            if (!Blueprint || !Blueprint->SimpleConstructionScript)
            {
                return FMCPToolResult::Error(FString::Printf(
                    TEXT("'%s' is not a Blueprint with a construction script: %s"), *ParentStr, *ResolveError));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteComponent", "MCP Delete Blueprint Component"));
            Blueprint->Modify();

            USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
            for (const FString& Target : Targets)
            {
                USCS_Node* SCSNode = SCS->FindSCSNode(FName(*Target));
                if (!SCSNode)
                {
                    continue;
                }
                SCS->RemoveNodeAndPromoteChildren(SCSNode);
                Deleted.Add(Target);
            }

            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=connection ──────────────────────────────────────────────────
        if (Type.Equals(TEXT("connection"), ESearchCase::IgnoreCase))
        {
            FString ParentStr;
            if (!Params->TryGetStringField(TEXT("parent"), ParentStr) || ParentStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'parent' (Blueprint path) is required for type=connection"));
            }

            const TSharedPtr<FJsonObject>* PinSourceObj = nullptr;
            const TSharedPtr<FJsonObject>* PinDestObj   = nullptr;
            if (!Params->TryGetObjectField(TEXT("pin_source"), PinSourceObj) ||
                !Params->TryGetObjectField(TEXT("pin_dest"),   PinDestObj))
            {
                return FMCPToolResult::Error(TEXT("'pin_source' and 'pin_dest' are required for type=connection"));
            }

            FString SourceNodeGuid, SourcePinName, DestNodeGuid, DestPinName;
            (*PinSourceObj)->TryGetStringField(TEXT("node"), SourceNodeGuid);
            (*PinSourceObj)->TryGetStringField(TEXT("pin"),  SourcePinName);
            (*PinDestObj)->TryGetStringField  (TEXT("node"), DestNodeGuid);
            (*PinDestObj)->TryGetStringField  (TEXT("pin"),  DestPinName);

            FString ResolveError;
            UObject* ParentObj = FMCPObjectResolver::ResolveAsset(ParentStr, ResolveError);
            UBlueprint* Blueprint = Cast<UBlueprint>(ParentObj);
            if (!Blueprint)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint: %s"), *ParentStr, *ResolveError));
            }

            UEdGraphNode* SourceNode = FMCPGraphHelpers::FindNodeByGuid(Blueprint, SourceNodeGuid);
            UEdGraphNode* DestNode   = FMCPGraphHelpers::FindNodeByGuid(Blueprint, DestNodeGuid);
            if (!SourceNode || !DestNode)
            {
                return FMCPToolResult::Error(TEXT("One or both nodes not found in Blueprint"));
            }

            UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName), EGPD_Output);
            UEdGraphPin* DestPin   = DestNode->FindPin  (FName(*DestPinName),   EGPD_Input);
            if (!SourcePin || !DestPin)
            {
                return FMCPToolResult::Error(FString::Printf(
                    TEXT("Pin '%s' (output) or '%s' (input) not found"), *SourcePinName, *DestPinName));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPDeleteConnection", "MCP Delete Blueprint Connection"));
            Blueprint->Modify();
            SourceNode->Modify();
            DestNode->Modify();
            SourcePin->BreakLinkTo(DestPin);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

            Deleted.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
                *SourceNodeGuid, *SourcePinName, *DestNodeGuid, *DestPinName));

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),   Deleted.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=folder ──────────────────────────────────────────────────────
        if (Type.Equals(TEXT("folder"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Targets = ParseTargets(Params);
            if (Targets.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' is required for type=folder"));
            }

            IAssetRegistry& AssetRegistry =
                FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

            TArray<FString> Warnings;
            for (const FString& Target : Targets)
            {
                if (!Target.StartsWith(TEXT("/Game/")))
                {
                    Warnings.Add(FString::Printf(TEXT("'%s' is not a /Game/ path"), *Target));
                    continue;
                }

                if (Target.Contains(TEXT("..")))
                {
                    return FMCPToolResult::Error(FString::Printf(
                        TEXT("Path traversal not allowed: '%s'"), *Target));
                }

                FARFilter Filter;
                Filter.PackagePaths.Add(FName(*Target));
                Filter.bRecursivePaths = true;
                TArray<FAssetData> Assets;
                AssetRegistry.GetAssets(Filter, Assets);
                if (Assets.Num() > 0)
                {
                    Warnings.Add(FString::Printf(TEXT("'%s' is not empty (%d assets)"), *Target, Assets.Num()));
                    continue;
                }

                FString RelPath = Target.RightChop(FString(TEXT("/Game/")).Len());
                FString DiskPath = FPaths::ProjectContentDir() / RelPath;

                if (!IFileManager::Get().DirectoryExists(*DiskPath))
                {
                    Warnings.Add(FString::Printf(TEXT("'%s' does not exist on disk"), *Target));
                    continue;
                }

                if (IFileManager::Get().DeleteDirectory(*DiskPath, false, true))
                {
                    Deleted.Add(Target);
                }
                else
                {
                    Warnings.Add(FString::Printf(TEXT("Failed to delete '%s'"), *Target));
                }
            }

            if (Deleted.IsEmpty() && Warnings.Num() > 0)
            {
                return FMCPToolResult::Error(FString::Join(Warnings, TEXT("; ")));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("deleted"), FMCPJsonHelpers::ArrayFromStrings(Deleted));
            Result->SetNumberField(TEXT("count"),  Deleted.Num());
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown type: '%s'. Valid: asset, actor, node, variable, expression, component, connection, folder"), *Type));
    });
}

#undef LOCTEXT_NAMESPACE
