#include "Tools/MCPTool_Inspect.h"
#include "MCPGameThreadHelper.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"
#include "MCPSearchPatterns.h"
#include "MCPObjectResolver.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"

// Blueprint
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Engine/Texture.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    constexpr float NodeMinHeight    = 48.f;   // minimum header+body height
    constexpr float NodeBaseHeight   = 24.f;   // header row height
    constexpr float NodePinRowHeight = 26.f;   // height per pin row
    constexpr float NodeMinWidth     = 128.f;  // minimum node width
    constexpr float NodeCharWidth    = 7.f;    // approximate pixels per title character
    constexpr float NodeWidthPadding = 60.f;   // icon + margin padding

    FVector2D EstimateNodeSize(int32 NumInputPins, int32 NumOutputPins, const FString& Title)
    {
        float MaxPins = (float)FMath::Max(NumInputPins, NumOutputPins);
        float Height  = FMath::Max(NodeMinHeight,   NodeBaseHeight + MaxPins * NodePinRowHeight);
        float Width   = FMath::Max(NodeMinWidth,    Title.Len() * NodeCharWidth + NodeWidthPadding);
        return FVector2D(Width, Height);
    }

    FVector2D ResolveNodeSize(UEdGraphNode* Node, int32 NumInputPins, int32 NumOutputPins, const FString& Title)
    {
        if (Node && Node->NodeWidth > 0 && Node->NodeHeight > 0)
        {
            return FVector2D((float)Node->NodeWidth, (float)Node->NodeHeight);
        }
        return EstimateNodeSize(NumInputPins, NumOutputPins, Title);
    }

    TSharedPtr<FJsonObject> MakePinJson(UEdGraphPin* Pin)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"),          Pin->PinName.ToString());
        Obj->SetStringField(TEXT("direction"),      Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
        Obj->SetStringField(TEXT("type"),           UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString());
        Obj->SetStringField(TEXT("default_value"),  Pin->DefaultValue);
        Obj->SetBoolField  (TEXT("is_connected"),   Pin->LinkedTo.Num() > 0);

        if (Pin->LinkedTo.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ConnectedTo;
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNodeUnchecked()) continue;
                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                ConnObj->SetStringField(TEXT("node"), FMCPJsonHelpers::GuidToCompact(LinkedPin->GetOwningNode()->NodeGuid));
                ConnObj->SetStringField(TEXT("pin"),  LinkedPin->PinName.ToString());
                ConnectedTo.Add(MakeShared<FJsonValueObject>(ConnObj));
            }
            Obj->SetArrayField(TEXT("connected_to"), ConnectedTo);
        }
        return Obj;
    }

    TSharedPtr<FJsonObject> MakeNodeJson(UEdGraphNode* Node)
    {
        FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("node_id"),  FMCPJsonHelpers::GuidToCompact(Node->NodeGuid));
        Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        Obj->SetStringField(TEXT("name"),  Title);
        Obj->SetNumberField(TEXT("pos_x"),  Node->NodePosX);
        Obj->SetNumberField(TEXT("pos_y"),  Node->NodePosY);

        int32 VisibleInputs = 0, VisibleOutputs = 0;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden) continue;
            if (Pin->Direction == EGPD_Input) ++VisibleInputs;
            else ++VisibleOutputs;
        }
        FVector2D Size = ResolveNodeSize(Node, VisibleInputs, VisibleOutputs, Title);
        Obj->SetNumberField(TEXT("width"),  Size.X);
        Obj->SetNumberField(TEXT("height"), Size.Y);

        TArray<TSharedPtr<FJsonValue>> Connections;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output || Pin->LinkedTo.Num() == 0) continue;
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin || !LinkedPin->GetOwningNodeUnchecked()) continue;
                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                ConnObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                ConnObj->SetStringField(TEXT("to_node"),  FMCPJsonHelpers::GuidToCompact(LinkedPin->GetOwningNode()->NodeGuid));
                ConnObj->SetStringField(TEXT("to_pin"),   LinkedPin->PinName.ToString());
                Connections.Add(MakeShared<FJsonValueObject>(ConnObj));
            }
        }
        if (Connections.Num() > 0)
        {
            Obj->SetArrayField(TEXT("connections"), Connections);
        }
        return Obj;
    }

    TSharedPtr<FJsonObject> MakeExpressionJson(UMaterialExpression* Expr)
    {
        FString ExprTitle = Expr->GetDescription();
        const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("node_id"),  FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid));
        Obj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
        Obj->SetStringField(TEXT("name"),  ExprTitle);
        Obj->SetNumberField(TEXT("pos_x"),  Expr->MaterialExpressionEditorX);
        Obj->SetNumberField(TEXT("pos_y"),  Expr->MaterialExpressionEditorY);

        FVector2D Size = ResolveNodeSize(Expr->GraphNode, InputCount, Expr->GetOutputs().Num(), ExprTitle);
        Obj->SetNumberField(TEXT("width"),  Size.X);
        Obj->SetNumberField(TEXT("height"), Size.Y);

        TArray<TSharedPtr<FJsonValue>> Connections;
        for (int32 i = 0; i < InputCount; ++i)
        {
            FExpressionInput* CurInput = FMCPGraphHelpers::GetExpressionInput(Expr, i);
            if (!CurInput || !CurInput->Expression) continue;
            TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
            ConnObj->SetStringField(TEXT("input_name"),       Expr->GetInputName(i).ToString());
            ConnObj->SetStringField(TEXT("from_node"),        FMCPJsonHelpers::GuidToCompact(CurInput->Expression->MaterialExpressionGuid));
            ConnObj->SetNumberField(TEXT("from_output_index"), CurInput->OutputIndex);
            Connections.Add(MakeShared<FJsonValueObject>(ConnObj));
        }
        if (Connections.Num() > 0)
        {
            Obj->SetArrayField(TEXT("connections"), Connections);
        }
        return Obj;
    }

}

FMCPToolInfo FMCPTool_Inspect::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("inspect");
    Info.Description = TEXT("Inspect properties, components, nodes, variables, functions, pins, or parameters of an asset or actor");
    Info.Parameters = {
        { TEXT("target"), TEXT("Object path, actor label, 'selected', or 'AssetPath::NodeGUID' for pins"), TEXT("string"), true  },
        { TEXT("type"),   TEXT("'properties' (default), 'components' (level actors and Blueprints; use graph tool to add/edit BP components), 'nodes', 'expressions', 'variables', 'functions', 'pins', 'parameters' (Material only), 'connections' (Blueprints and Materials)"), TEXT("string"), false },
        { TEXT("filter"), TEXT("Glob/regex to filter results by name or class (does not filter by graph name)"), TEXT("string"), false },
        { TEXT("depth"),  TEXT("Property traversal depth (default: 1, currently informational)"),          TEXT("integer"), false },
        { TEXT("detail"), TEXT("Property detail level: 'all' or 'skip_defaults' (default: skip_defaults). When skip_defaults, omits properties with default/empty values."), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Inspect::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString TargetParam;
        if (!Params->TryGetStringField(TEXT("target"), TargetParam))
        {
            return FMCPToolResult::Error(TEXT("'target' is required"));
        }

        FString TypeParam = TEXT("properties");
        FString Filter;
        Params->TryGetStringField(TEXT("type"),   TypeParam);
        Params->TryGetStringField(TEXT("filter"), Filter);

        FString DetailParam = TEXT("skip_defaults");
        Params->TryGetStringField(TEXT("detail"), DetailParam);
        bool bSkipDefaults = !DetailParam.Equals(TEXT("all"), ESearchCase::IgnoreCase);

        auto PassesFilter = [&](const FString& Name) -> bool
        {
            return Filter.IsEmpty() || FMCPSearchPatterns::Matches(Filter, Name);
        };

        // ── pins / connections: target = "AssetPath::NodeGUID" ─────────────
        // These need special target parsing before object resolution.
        FString AssetPath  = TargetParam;
        FString NodeGuidStr;
        if (TargetParam.Contains(TEXT("::")))
        {
            TargetParam.Split(TEXT("::"), &AssetPath, &NodeGuidStr);
        }

        // Resolve the primary object
        FString ResolveError;
        UObject* Obj = FMCPObjectResolver::ResolveObject(AssetPath, ResolveError);

        // ── properties ──────────────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("properties"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            TSharedPtr<FJsonObject> Props = FMCPJsonHelpers::UObjectToJson(Obj, Filter, bSkipDefaults);
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetObjectField(TEXT("properties"), Props);
            Result->SetStringField(TEXT("name"),  Obj->GetName());
            Result->SetStringField(TEXT("class"), Obj->GetClass()->GetName());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── components ──────────────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("components"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            AActor* Actor     = Cast<AActor>(Obj);
            UBlueprint* Blueprint = Cast<UBlueprint>(Obj);
            TArray<TSharedPtr<FJsonValue>> ResultArray;

            if (Actor)
            {
                TArray<UActorComponent*> Components;
                Actor->GetComponents(Components);

                for (UActorComponent* Comp : Components)
                {
                    if (!Comp) continue;
                    if (!PassesFilter(Comp->GetName())) continue;

                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"),  Comp->GetName());
                    CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());

                    USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
                    if (SceneComp)
                    {
                        FTransform T = SceneComp->GetRelativeTransform();
                        TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

                        TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
                        Loc->SetNumberField(TEXT("x"), T.GetLocation().X);
                        Loc->SetNumberField(TEXT("y"), T.GetLocation().Y);
                        Loc->SetNumberField(TEXT("z"), T.GetLocation().Z);
                        TransObj->SetObjectField(TEXT("location"), Loc);

                        TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
                        FRotator R = T.GetRotation().Rotator();
                        Rot->SetNumberField(TEXT("pitch"), R.Pitch);
                        Rot->SetNumberField(TEXT("yaw"),   R.Yaw);
                        Rot->SetNumberField(TEXT("roll"),  R.Roll);
                        TransObj->SetObjectField(TEXT("rotation"), Rot);

                        TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
                        Scale->SetNumberField(TEXT("x"), T.GetScale3D().X);
                        Scale->SetNumberField(TEXT("y"), T.GetScale3D().Y);
                        Scale->SetNumberField(TEXT("z"), T.GetScale3D().Z);
                        TransObj->SetObjectField(TEXT("scale"), Scale);

                        CompObj->SetObjectField(TEXT("transform"), TransObj);
                    }

                    ResultArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
            }
            else if (Blueprint && Blueprint->SimpleConstructionScript)
            {
                // Blueprint fallback — iterate SCS nodes
                for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                {
                    if (!Node || !Node->ComponentClass) continue;
                    FString NodeName = Node->GetVariableName().ToString();
                    if (!PassesFilter(NodeName)) continue;

                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"),  NodeName);
                    CompObj->SetStringField(TEXT("class"), Node->ComponentClass->GetName());
                    ResultArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
            }
            else
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not an Actor or Blueprint"), *AssetPath));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("components"), ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── nodes / expressions ─────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("nodes"), ESearchCase::IgnoreCase) ||
            TypeParam.Equals(TEXT("expressions"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            TArray<TSharedPtr<FJsonValue>> ResultArray;

            // Blueprint path
            if (UBlueprint* Blueprint = Cast<UBlueprint>(Obj))
            {
                TArray<UEdGraph*> AllGraphs;
                Blueprint->GetAllGraphs(AllGraphs);

                for (UEdGraph* Graph : AllGraphs)
                {
                    for (UEdGraphNode* Node : Graph->Nodes)
                    {
                        if (!Node) continue;
                        FString NodeName = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                        if (!PassesFilter(NodeName) && !PassesFilter(Node->GetClass()->GetName())) continue;

                        TSharedPtr<FJsonObject> NodeObj = MakeNodeJson(Node);
                        NodeObj->SetStringField(TEXT("graph"), Graph->GetName());
                        ResultArray.Add(MakeShared<FJsonValueObject>(NodeObj));
                    }
                }
            }
            // Material path
            else if (UMaterial* Material = Cast<UMaterial>(Obj))
            {
                if (UMaterial* PreviewMat = FMCPGraphHelpers::GetEditorPreviewMaterial(Material))
                    Material = PreviewMat;
                for (UMaterialExpression* Expr : Material->GetExpressions())
                {
                    if (!Expr) continue;
                    if (!PassesFilter(Expr->GetClass()->GetName()) && !PassesFilter(Expr->GetDescription())) continue;

                    ResultArray.Add(MakeShared<FJsonValueObject>(MakeExpressionJson(Expr)));
                }
            }
            else
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint or Material"), *AssetPath));
            }

            FString ResponseKey = TypeParam.Equals(TEXT("expressions"), ESearchCase::IgnoreCase) ? TEXT("expressions") : TEXT("nodes");
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(ResponseKey, ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── variables ───────────────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("variables"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            UBlueprint* Blueprint = Cast<UBlueprint>(Obj);
            if (!Blueprint)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint"), *AssetPath));
            }

            TArray<TSharedPtr<FJsonValue>> ResultArray;
            for (const FBPVariableDescription& Var : Blueprint->NewVariables)
            {
                if (!PassesFilter(Var.VarName.ToString())) continue;

                TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
                VarObj->SetStringField(TEXT("name"),          Var.VarName.ToString());
                VarObj->SetStringField(TEXT("type"),          UEdGraphSchema_K2::TypeToText(Var.VarType).ToString());
                VarObj->SetStringField(TEXT("category"),      Var.Category.ToString());
                VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
                ResultArray.Add(MakeShared<FJsonValueObject>(VarObj));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("variables"), ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── functions ───────────────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("functions"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            UBlueprint* Blueprint = Cast<UBlueprint>(Obj);
            if (!Blueprint)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint"), *AssetPath));
            }

            TArray<TSharedPtr<FJsonValue>> ResultArray;
            for (UEdGraph* Graph : Blueprint->FunctionGraphs)
            {
                if (!Graph) continue;
                if (!PassesFilter(Graph->GetName())) continue;

                TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
                FuncObj->SetStringField(TEXT("name"), Graph->GetName());
                ResultArray.Add(MakeShared<FJsonValueObject>(FuncObj));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("functions"), ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── pins ─────────────────────────────────────────────────────────────
        // target format: "AssetPath::NodeGUID"
        if (TypeParam.Equals(TEXT("pins"), ESearchCase::IgnoreCase))
        {
            if (NodeGuidStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("For 'pins', use target format 'AssetPath::NodeGUID' (get GUIDs from TypeParam=nodes)"));
            }
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            if (UBlueprint* Blueprint = Cast<UBlueprint>(Obj))
            {
                UEdGraphNode* Node = FMCPGraphHelpers::FindNodeByGuid(Blueprint, NodeGuidStr);
                if (!Node)
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found in Blueprint '%s'"), *NodeGuidStr, *AssetPath));
                }

                TArray<TSharedPtr<FJsonValue>> ResultArray;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin || Pin->bHidden) continue;
                    if (!PassesFilter(Pin->PinName.ToString())) continue;
                    ResultArray.Add(MakeShared<FJsonValueObject>(MakePinJson(Pin)));
                }

                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                Result->SetStringField(TEXT("node_id"),  NodeGuidStr);
                Result->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                Result->SetArrayField (TEXT("pins"),       ResultArray);
                Result->SetNumberField(TEXT("count"),      ResultArray.Num());
                return FMCPJsonHelpers::SuccessResponse(Result);
            }
            else if (UMaterial* Material = Cast<UMaterial>(Obj))
            {
                if (UMaterial* PreviewMat = FMCPGraphHelpers::GetEditorPreviewMaterial(Material))
                    Material = PreviewMat;
                UMaterialExpression* Expr = FMCPGraphHelpers::FindExpressionByGuid(Material, NodeGuidStr);
                if (!Expr)
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Expression '%s' not found in Material '%s'"), *NodeGuidStr, *AssetPath));
                }

                TArray<TSharedPtr<FJsonValue>> ResultArray;

                // Inputs
                const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);
                for (int32 i = 0; i < InputCount; ++i)
                {
                    FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, i);
                    if (!Input) continue;
                    FString PinName = Expr->GetInputName(i).ToString();
                    if (!PassesFilter(PinName)) continue;

                    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                    PinObj->SetStringField(TEXT("name"),         PinName);
                    PinObj->SetStringField(TEXT("direction"),    TEXT("input"));
                    PinObj->SetBoolField  (TEXT("is_connected"), Input->Expression != nullptr);
                    if (Input->Expression)
                    {
                        TSharedPtr<FJsonObject> ConnTo = MakeShared<FJsonObject>();
                        ConnTo->SetStringField(TEXT("from_node"),         FMCPJsonHelpers::GuidToCompact(Input->Expression->MaterialExpressionGuid));
                        ConnTo->SetNumberField(TEXT("from_output_index"), Input->OutputIndex);
                        PinObj->SetObjectField(TEXT("connected_to"), ConnTo);
                    }
                    ResultArray.Add(MakeShared<FJsonValueObject>(PinObj));
                }

                // Outputs
                const TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
                for (const FExpressionOutput& Out : Outputs)
                {
                    FString PinName = FMCPGraphHelpers::ExprOutputPinName(Out);
                    if (!PassesFilter(PinName)) continue;

                    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
                    PinObj->SetStringField(TEXT("name"),      PinName);
                    PinObj->SetStringField(TEXT("direction"), TEXT("output"));
                    ResultArray.Add(MakeShared<FJsonValueObject>(PinObj));
                }

                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                Result->SetStringField(TEXT("expr_guid"), NodeGuidStr);
                Result->SetStringField(TEXT("class"),     Expr->GetClass()->GetName());
                Result->SetArrayField (TEXT("pins"),      ResultArray);
                Result->SetNumberField(TEXT("count"),     ResultArray.Num());
                return FMCPJsonHelpers::SuccessResponse(Result);
            }
            else
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint or Material"), *AssetPath));
            }
        }

        // ── parameters (Material) ────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("parameters"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            UMaterial* Material = Cast<UMaterial>(Obj);
            if (!Material)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Material"), *AssetPath));
            }
            if (UMaterial* PreviewMat = FMCPGraphHelpers::GetEditorPreviewMaterial(Material))
                Material = PreviewMat;

            TArray<TSharedPtr<FJsonValue>> ResultArray;
            for (UMaterialExpression* Expr : Material->GetExpressions())
            {
                if (!Expr) continue;

                FString ParamName;
                FString ParamType;
                FString DefaultVal;

                if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
                {
                    ParamName  = ScalarParam->ParameterName.ToString();
                    ParamType  = TEXT("scalar");
                    DefaultVal = FString::SanitizeFloat(ScalarParam->DefaultValue);
                }
                else if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr))
                {
                    ParamName  = VecParam->ParameterName.ToString();
                    ParamType  = TEXT("vector");
                    DefaultVal = VecParam->DefaultValue.ToString();
                }
                else if (UMaterialExpressionTextureSampleParameter2D* TexParam = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                {
                    ParamName  = TexParam->ParameterName.ToString();
                    ParamType  = TEXT("texture2d");
                    DefaultVal = TexParam->Texture ? TexParam->Texture->GetPathName() : TEXT("");
                }
                else if (UMaterialExpressionStaticBoolParameter* BoolParam = Cast<UMaterialExpressionStaticBoolParameter>(Expr))
                {
                    ParamName  = BoolParam->ParameterName.ToString();
                    ParamType  = TEXT("static_bool");
                    DefaultVal = BoolParam->DefaultValue ? TEXT("true") : TEXT("false");
                }
                else
                {
                    continue;
                }

                if (!PassesFilter(ParamName)) continue;

                TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
                ParamObj->SetStringField(TEXT("name"),          ParamName);
                ParamObj->SetStringField(TEXT("type"),          ParamType);
                ParamObj->SetStringField(TEXT("default_value"), DefaultVal);
                ResultArray.Add(MakeShared<FJsonValueObject>(ParamObj));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("parameters"), ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── connections ──────────────────────────────────────────────────────
        if (TypeParam.Equals(TEXT("connections"), ESearchCase::IgnoreCase))
        {
            if (!Obj) return FMCPToolResult::Error(ResolveError);

            TArray<TSharedPtr<FJsonValue>> ResultArray;

            if (UBlueprint* Blueprint = Cast<UBlueprint>(Obj))
            {
                TArray<UEdGraph*> AllGraphs;
                Blueprint->GetAllGraphs(AllGraphs);

                for (UEdGraph* Graph : AllGraphs)
                {
                    for (UEdGraphNode* Node : Graph->Nodes)
                    {
                        if (!Node) continue;
                        for (UEdGraphPin* Pin : Node->Pins)
                        {
                            if (!Pin || Pin->Direction != EGPD_Output || Pin->LinkedTo.Num() == 0) continue;

                            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                            {
                                if (!LinkedPin || !LinkedPin->GetOwningNodeUnchecked()) continue;
                                if (!PassesFilter(Pin->PinName.ToString()) && !PassesFilter(LinkedPin->PinName.ToString()) && !PassesFilter(Graph->GetName())) continue;

                                TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                                ConnObj->SetStringField(TEXT("from_node"),  FMCPJsonHelpers::GuidToCompact(Node->NodeGuid));
                                ConnObj->SetStringField(TEXT("from_pin"),   Pin->PinName.ToString());
                                ConnObj->SetStringField(TEXT("to_node"),    FMCPJsonHelpers::GuidToCompact(LinkedPin->GetOwningNode()->NodeGuid));
                                ConnObj->SetStringField(TEXT("to_pin"),     LinkedPin->PinName.ToString());
                                ConnObj->SetStringField(TEXT("graph"),      Graph->GetName());
                                ResultArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                            }
                        }
                    }
                }
            }
            else if (UMaterial* Material = Cast<UMaterial>(Obj))
            {
                if (UMaterial* PreviewMat = FMCPGraphHelpers::GetEditorPreviewMaterial(Material))
                    Material = PreviewMat;
                // Expression-to-expression edges
                for (UMaterialExpression* Expr : Material->GetExpressions())
                {
                    if (!Expr) continue;
                    FString ToGuid = FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid);
                    const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);
                    for (int32 i = 0; i < InputCount; ++i)
                    {
                        FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, i);
                        if (!Input || !Input->Expression) continue;
                        if (!PassesFilter(Expr->GetInputName(i).ToString())) continue;

                        TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                        ConnObj->SetStringField(TEXT("from_node"),         FMCPJsonHelpers::GuidToCompact(Input->Expression->MaterialExpressionGuid));
                        ConnObj->SetNumberField(TEXT("from_output_index"), Input->OutputIndex);
                        ConnObj->SetStringField(TEXT("to_node"),           ToGuid);
                        ConnObj->SetStringField(TEXT("to_pin"),            Expr->GetInputName(i).ToString());
                        ResultArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                    }
                }

                // Expression-to-property edges
                for (const FMCPGraphHelpers::FMaterialPropertyEntry& KP : FMCPGraphHelpers::KnownMaterialProperties())
                {
                    FExpressionInput* PropInput = Material->GetExpressionInputForProperty(KP.Prop);
                    if (!PropInput || !PropInput->Expression) continue;
                    if (!PassesFilter(KP.Name)) continue;

                    TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
                    ConnObj->SetStringField(TEXT("from_node"),         FMCPJsonHelpers::GuidToCompact(PropInput->Expression->MaterialExpressionGuid));
                    ConnObj->SetNumberField(TEXT("from_output_index"), PropInput->OutputIndex);
                    ConnObj->SetStringField(TEXT("to_property"),       KP.Name);
                    ResultArray.Add(MakeShared<FJsonValueObject>(ConnObj));
                }
            }
            else
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint or Material"), *AssetPath));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("connections"), ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown 'type': '%s'. Valid: properties, components, nodes, expressions, variables, functions, pins, parameters, connections"),
            *TypeParam));
    });
}
