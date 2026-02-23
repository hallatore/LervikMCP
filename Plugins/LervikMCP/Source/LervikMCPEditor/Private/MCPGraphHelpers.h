#pragma once

#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionIO.h"
#include "Misc/EngineVersionComparison.h"
#include "MaterialGraph/MaterialGraph.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "IMaterialEditor.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "MCPJsonHelpers.h"

struct FMCPGraphHelpers
{
    // Find a BP node across all graphs by compact ID or raw GUID string
    static UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FString& GuidStr)
    {
        if (!Blueprint) return nullptr;
        FGuid TargetGuid = FMCPJsonHelpers::CompactToGuid(GuidStr);
        if (!TargetGuid.IsValid()) return nullptr;
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);
        for (UEdGraph* Graph : AllGraphs)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && Node->NodeGuid == TargetGuid)
                {
                    return Node;
                }
            }
        }
        return nullptr;
    }

    // Derive the user-facing output pin name; returns "" for unnamed default outputs
    // (empty string is what UMaterialEditingLibrary::ConnectMaterialProperty expects for the primary output)
    static FString ExprOutputPinName(const FExpressionOutput& Out)
    {
        if (!Out.OutputName.IsNone())
            return Out.OutputName.ToString();
        if (Out.MaskR && !Out.MaskG && !Out.MaskB && !Out.MaskA) return TEXT("R");
        if (!Out.MaskR && Out.MaskG && !Out.MaskB && !Out.MaskA) return TEXT("G");
        if (!Out.MaskR && !Out.MaskG && Out.MaskB && !Out.MaskA) return TEXT("B");
        if (!Out.MaskR && !Out.MaskG && !Out.MaskB && Out.MaskA) return TEXT("A");
        return TEXT("");  // default/first output
    }

    // Find a material expression by compact ID or raw GUID string
    static UMaterialExpression* FindExpressionByGuid(UMaterial* Material, const FString& GuidStr)
    {
        if (!Material) return nullptr;
        FGuid TargetGuid = FMCPJsonHelpers::CompactToGuid(GuidStr);
        if (!TargetGuid.IsValid()) return nullptr;
        for (UMaterialExpression* Expr : Material->GetExpressions())
        {
            if (Expr && Expr->MaterialExpressionGuid == TargetGuid)
            {
                return Expr;
            }
        }
        return nullptr;
    }

    // Get the open Material editor for a material, or nullptr if none is open
    static IMaterialEditor* FindMaterialEditor(UMaterial* Material)
    {
        if (!Material || !GEditor) return nullptr;
        UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!Sub) return nullptr;
        IAssetEditorInstance* EditorInstance = Sub->FindEditorForAsset(Material, /*bFocusIfOpen=*/false);
        if (!EditorInstance || EditorInstance->GetEditorName() != FName(TEXT("MaterialEditor")))
            return nullptr;
        return static_cast<IMaterialEditor*>(EditorInstance);
    }

    // Get the Material Editor's internal preview copy, or nullptr if no editor is open
    static UMaterial* GetEditorPreviewMaterial(UMaterial* OriginalMaterial)
    {
        IMaterialEditor* MatEditor = FindMaterialEditor(OriginalMaterial);
        return MatEditor ? Cast<UMaterial>(MatEditor->GetMaterialInterface()) : nullptr;
    }

    // Resolve which material to edit: preview copy if editor is open, original otherwise
    static UMaterial* ResolveMaterialForEditing(UMaterial* OriginalMaterial)
    {
        if (UMaterial* Preview = GetEditorPreviewMaterial(OriginalMaterial))
            return Preview;
        return OriginalMaterial;
    }

    // Rebuild the material graph in any open Material editor to reflect data changes
    static void RefreshMaterialEditor(UMaterial* Material)
    {
        if (!Material) return;

        IMaterialEditor* MatEditor = FindMaterialEditor(Material);
        UMaterial* WorkingMaterial = MatEditor ? Cast<UMaterial>(MatEditor->GetMaterialInterface()) : nullptr;
        if (!WorkingMaterial) WorkingMaterial = Material;

        if (WorkingMaterial->MaterialGraph)
            WorkingMaterial->MaterialGraph->RebuildGraph();

        if (MatEditor)
        {
            MatEditor->NotifyExternalMaterialChange();
            MatEditor->UpdateMaterialAfterGraphChange();
            MatEditor->UpdateDetailView();
        }
    }

    // ── Cross-version material expression input access ─────────────────────
    static inline int32 GetExpressionInputCount(UMaterialExpression* Expr)
    {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
        return Expr->GetInputsView().Num();
#else
        int32 Count = 0;
        while (Expr->GetInput(Count)) ++Count;
        return Count;
#endif
    }

    static inline FExpressionInput* GetExpressionInput(UMaterialExpression* Expr, int32 Index)
    {
#if UE_VERSION_OLDER_THAN(5, 5, 0)
        TArrayView<FExpressionInput*> Inputs = Expr->GetInputsView();
        return Inputs.IsValidIndex(Index) ? Inputs[Index] : nullptr;
#else
        return Expr->GetInput(Index);
#endif
    }

    // ── Material property table ────────────────────────────────────────────
    struct FMaterialPropertyEntry { EMaterialProperty Prop; const TCHAR* Name; };

    static TArrayView<const FMaterialPropertyEntry> KnownMaterialProperties()
    {
        static const FMaterialPropertyEntry Entries[] = {
            { MP_BaseColor,           TEXT("BaseColor") },
            { MP_Metallic,            TEXT("Metallic") },
            { MP_Specular,            TEXT("Specular") },
            { MP_Roughness,           TEXT("Roughness") },
            { MP_EmissiveColor,       TEXT("EmissiveColor") },
            { MP_Normal,              TEXT("Normal") },
            { MP_Opacity,             TEXT("Opacity") },
            { MP_OpacityMask,         TEXT("OpacityMask") },
            { MP_WorldPositionOffset, TEXT("WorldPositionOffset") },
            { MP_AmbientOcclusion,    TEXT("AmbientOcclusion") },
            { MP_Refraction,          TEXT("Refraction") },
            { MP_Anisotropy,          TEXT("Anisotropy") },
            { MP_Tangent,             TEXT("Tangent") },
            { MP_Displacement,        TEXT("Displacement") },
            { MP_SubsurfaceColor,     TEXT("SubsurfaceColor") },
            { MP_CustomData0,         TEXT("CustomData0") },
            { MP_CustomData1,         TEXT("CustomData1") },
            { MP_PixelDepthOffset,    TEXT("PixelDepthOffset") },
            { MP_ShadingModel,        TEXT("ShadingModel") },
            { MP_SurfaceThickness,    TEXT("SurfaceThickness") },
            { MP_FrontMaterial,       TEXT("FrontMaterial") },
            { MP_MaterialAttributes,  TEXT("MaterialAttributes") },
            { MP_CustomizedUVs0,      TEXT("CustomizedUV0") },
            { MP_CustomizedUVs1,      TEXT("CustomizedUV1") },
            { MP_CustomizedUVs2,      TEXT("CustomizedUV2") },
            { MP_CustomizedUVs3,      TEXT("CustomizedUV3") },
            { MP_CustomizedUVs4,      TEXT("CustomizedUV4") },
            { MP_CustomizedUVs5,      TEXT("CustomizedUV5") },
            { MP_CustomizedUVs6,      TEXT("CustomizedUV6") },
            { MP_CustomizedUVs7,      TEXT("CustomizedUV7") },
        };
        return TArrayView<const FMaterialPropertyEntry>(Entries);
    }

    static bool MapMaterialProperty(const FString& Name, EMaterialProperty& OutProp)
    {
        for (const FMaterialPropertyEntry& Entry : KnownMaterialProperties())
        {
            if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
            {
                OutProp = Entry.Prop;
                return true;
            }
        }
        return false;
    }

    // Get the open Blueprint editor for a blueprint, or nullptr if none is open
    static IBlueprintEditor* FindBlueprintEditor(UBlueprint* Blueprint)
    {
        if (!Blueprint || !GEditor) return nullptr;
        UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!Sub) return nullptr;
        IAssetEditorInstance* EditorInstance = Sub->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);
        if (!EditorInstance || EditorInstance->GetEditorName() != FName(TEXT("BlueprintEditor")))
            return nullptr;
        return static_cast<IBlueprintEditor*>(EditorInstance);
    }

    // Refresh BP editor nodes and UI (visual refresh only; dirty marking handled by callers)
    static void RefreshBlueprintEditor(UBlueprint* Blueprint)
    {
        if (!Blueprint) return;
        FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
        if (IBlueprintEditor* BPEditor = FindBlueprintEditor(Blueprint))
        {
            BPEditor->RefreshEditors(ERefreshBlueprintEditorReason::UnknownReason);
            BPEditor->RefreshMyBlueprint();
        }
    }

    // Refresh all open Material and Blueprint editors
    static void RefreshAllOpenEditors()
    {
        if (!GEditor) return;
        UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (!Sub) return;
        for (UObject* Asset : Sub->GetAllEditedAssets())
        {
            if (UMaterial* Mat = Cast<UMaterial>(Asset))
                RefreshMaterialEditor(Mat);
            else if (UBlueprint* BP = Cast<UBlueprint>(Asset))
                RefreshBlueprintEditor(BP);
        }
    }

    // Find a graph by name in a Blueprint. Defaults to EventGraph if GraphName is empty.
    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint) return nullptr;

        if (GraphName.IsEmpty() || GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
        {
            return FBlueprintEditorUtils::FindEventGraph(Blueprint);
        }

        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                return Graph;
            }
        }

        // Also check UbergraphPages
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                return Graph;
            }
        }

        return nullptr;
    }
};
