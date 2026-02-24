#include "Tools/MCPTool_Graph.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPToolHelp.h"
#include "MCPObjectResolver.h"
#include "MCPGraphHelpers.h"

// Blueprint graph includes
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Self.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "ScopedTransaction.h"
#include "GameFramework/Actor.h"
#include "Editor.h"

// Material includes
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialExpressionIO.h"
#include "MaterialEditingLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "LervikMCP"

namespace
{
    // -------------------------------------------------------------------------
    // Action table (shared by help handler and error message)
    // -------------------------------------------------------------------------

    struct FGraphActionInfo
    {
        const TCHAR* Name;
        const TCHAR* Desc;
    };

    static const FGraphActionInfo GGraphActions[] = {
        { TEXT("add_node"),       TEXT("Add a node to a Blueprint graph or Material expression graph") },
        { TEXT("edit_node"),      TEXT("Edit properties or pin defaults of an existing node") },
        { TEXT("connect"),        TEXT("Connect pins between two nodes") },
        { TEXT("disconnect"),     TEXT("Disconnect pins between two nodes") },
        { TEXT("add_variable"),   TEXT("Add a variable to a Blueprint") },
        { TEXT("edit_variable"),  TEXT("Rename, retype, or recategorize a Blueprint variable") },
        { TEXT("add_function"),   TEXT("Add a function graph to a Blueprint") },
        { TEXT("add_component"),  TEXT("Add a component to a Blueprint") },
        { TEXT("edit_component"), TEXT("Edit properties of a Blueprint component") },
        { TEXT("compile"),        TEXT("Compile a Blueprint or recompile a Material") },
        { TEXT("help"),           TEXT("Show this list of valid actions") },
    };

    static FString GetValidActionsString()
    {
        FString Result;
        for (const FGraphActionInfo& A : GGraphActions)
        {
            if (!Result.IsEmpty()) Result += TEXT(", ");
            Result += A.Name;
        }
        return Result;
    }

    // -------------------------------------------------------------------------
    // Help data
    // -------------------------------------------------------------------------

    static const FMCPParamHelp sGraphCommonParams[] = {
        { TEXT("target"), TEXT("string"), true, TEXT("Blueprint or Material asset path"), nullptr, TEXT("/Game/BP_MyActor") },
    };

    static const FMCPParamHelp sAddNodeParams[] = {
        { TEXT("graph"),          TEXT("string"),  false, TEXT("Graph name (BP only). Default: EventGraph. Alias: graph_name"), nullptr, TEXT("EventGraph") },
        { TEXT("node_class"),     TEXT("string"),  false, TEXT("Node type"), TEXT("CallFunction, Event, CustomEvent, VariableGet, VariableSet, Branch, Sequence, Self, DynamicCast, SpawnActor, MakeArray, Select, SwitchOnInt, SwitchOnString, SwitchOnEnum, MacroInstance, ForEachLoop. Materials: Multiply, Add, Lerp, ScalarParameter, VectorParameter, TextureCoordinate, Constant"), TEXT("CallFunction") },
        { TEXT("function"),       TEXT("string"),  false, TEXT("Function name for CallFunction nodes"), nullptr, TEXT("PrintString") },
        { TEXT("function_owner"), TEXT("string"),  false, TEXT("Class owning the function; also cast target for DynamicCast"), nullptr, TEXT("KismetSystemLibrary") },
        { TEXT("event_name"),     TEXT("string"),  false, TEXT("Event name for Event/CustomEvent; macro name for MacroInstance"), nullptr, nullptr },
        { TEXT("variable_name"),  TEXT("string"),  false, TEXT("Variable name for VariableGet/VariableSet"), nullptr, nullptr },
        { TEXT("pos_x"),          TEXT("integer"), false, TEXT("Node X position"), nullptr, TEXT("200") },
        { TEXT("pos_y"),          TEXT("integer"), false, TEXT("Node Y position"), nullptr, TEXT("0") },
        { TEXT("nodes"),          TEXT("array"),   false, TEXT("Batch: array of node objects"), nullptr, nullptr },
    };

    static const FMCPParamHelp sEditNodeParams[] = {
        { TEXT("graph"),        TEXT("string"),  false, TEXT("Graph name. Default: EventGraph"), nullptr, nullptr },
        { TEXT("node"),         TEXT("string"),  true,  TEXT("Node GUID to edit"), nullptr, nullptr },
        { TEXT("properties"),   TEXT("object"),  false, TEXT("Reflection properties {PropName:value}"), nullptr, nullptr },
        { TEXT("pin_defaults"), TEXT("object"),  false, TEXT("Pin default values {PinName:value}"), nullptr, nullptr },
        { TEXT("pos_x"),        TEXT("integer"), false, TEXT("New X position"), nullptr, nullptr },
        { TEXT("pos_y"),        TEXT("integer"), false, TEXT("New Y position"), nullptr, nullptr },
        { TEXT("edits"),        TEXT("array"),   false, TEXT("Batch: array of edit objects"), nullptr, nullptr },
    };

    static const FMCPParamHelp sConnectParams[] = {
        { TEXT("graph"),       TEXT("string"), false, TEXT("Graph name. Default: EventGraph"), nullptr, nullptr },
        { TEXT("source"),      TEXT("object"), false, TEXT("Output pin {node:GUID, pin:PinName}"), nullptr, nullptr },
        { TEXT("dest"),        TEXT("object"), false, TEXT("Input pin {node:GUID, pin:PinName} or {property:PropName} for materials"), nullptr, nullptr },
        { TEXT("connections"), TEXT("array"),  false, TEXT("Batch: array of {source, dest} objects"), nullptr, nullptr },
    };

    static const FMCPParamHelp sAddVarParams[] = {
        { TEXT("name"),          TEXT("string"), true,  TEXT("Variable name (alias: var_name)"), nullptr, TEXT("Health") },
        { TEXT("var_type"),      TEXT("string"), true,  TEXT("Variable type"), TEXT("float, int, bool, string, byte, name, text, Vector, Rotator, Transform, Object:ClassName"), TEXT("float") },
        { TEXT("default_value"), TEXT("string"), false, TEXT("Default value as string"), nullptr, TEXT("100.0") },
        { TEXT("category"),      TEXT("string"), false, TEXT("Variable category"), nullptr, nullptr },
        { TEXT("variables"),     TEXT("array"),  false, TEXT("Batch: array of variable objects"), nullptr, nullptr },
    };

    static const FMCPParamHelp sEditVarParams[] = {
        { TEXT("name"),          TEXT("string"), true,  TEXT("Variable name to edit"), nullptr, nullptr },
        { TEXT("var_type"),      TEXT("string"), false, TEXT("New variable type"), nullptr, nullptr },
        { TEXT("new_name"),      TEXT("string"), false, TEXT("New name for rename"), nullptr, nullptr },
        { TEXT("default_value"), TEXT("string"), false, TEXT("New default value"), nullptr, nullptr },
        { TEXT("category"),      TEXT("string"), false, TEXT("New category"), nullptr, nullptr },
    };

    static const FMCPParamHelp sAddFuncParams[] = {
        { TEXT("name"),    TEXT("string"),  true,  TEXT("Function name"), nullptr, TEXT("CalculateDamage") },
        { TEXT("inputs"),  TEXT("array"),   false, TEXT("Input pins [{name, type}]"), nullptr, nullptr },
        { TEXT("outputs"), TEXT("array"),   false, TEXT("Output pins [{name, type}]"), nullptr, nullptr },
        { TEXT("pure"),    TEXT("boolean"), false, TEXT("Mark as pure (no exec pins). Default: false"), nullptr, nullptr },
    };

    static const FMCPParamHelp sAddCompParams[] = {
        { TEXT("component_class"), TEXT("string"), true,  TEXT("Component class name"), nullptr, TEXT("StaticMeshComponent") },
        { TEXT("name"),            TEXT("string"), false, TEXT("Component name"), nullptr, nullptr },
        { TEXT("parent"),          TEXT("string"), false, TEXT("Parent component name for hierarchy"), nullptr, nullptr },
        { TEXT("properties"),      TEXT("object"), false, TEXT("Reflection properties {PropName:value}"), nullptr, nullptr },
        { TEXT("components"),      TEXT("array"),  false, TEXT("Batch: array of component objects"), nullptr, nullptr },
    };

    static const FMCPParamHelp sEditCompParams[] = {
        { TEXT("name"),       TEXT("string"), true,  TEXT("Component name to edit"), nullptr, nullptr },
        { TEXT("properties"), TEXT("object"), true,  TEXT("Reflection properties {PropName:value}"), nullptr, nullptr },
    };

    static const FMCPActionHelp sGraphActions[] = {
        { TEXT("add_node"),       TEXT("Add a node to a Blueprint graph or Material expression graph"), sAddNodeParams, UE_ARRAY_COUNT(sAddNodeParams), nullptr },
        { TEXT("edit_node"),      TEXT("Edit properties or pin defaults of an existing node"), sEditNodeParams, UE_ARRAY_COUNT(sEditNodeParams), nullptr },
        { TEXT("connect"),        TEXT("Connect pins between two nodes"), sConnectParams, UE_ARRAY_COUNT(sConnectParams), nullptr },
        { TEXT("disconnect"),     TEXT("Disconnect pins between two nodes"), sConnectParams, UE_ARRAY_COUNT(sConnectParams), nullptr },
        { TEXT("add_variable"),   TEXT("Add a variable to a Blueprint"), sAddVarParams, UE_ARRAY_COUNT(sAddVarParams), nullptr },
        { TEXT("edit_variable"),  TEXT("Rename, retype, or recategorize a Blueprint variable"), sEditVarParams, UE_ARRAY_COUNT(sEditVarParams), nullptr },
        { TEXT("add_function"),   TEXT("Add a function graph to a Blueprint"), sAddFuncParams, UE_ARRAY_COUNT(sAddFuncParams), nullptr },
        { TEXT("add_component"),  TEXT("Add a component to a Blueprint"), sAddCompParams, UE_ARRAY_COUNT(sAddCompParams), nullptr },
        { TEXT("edit_component"), TEXT("Edit properties of a Blueprint component"), sEditCompParams, UE_ARRAY_COUNT(sEditCompParams), nullptr },
        { TEXT("compile"),        TEXT("Compile a Blueprint or recompile a Material"), nullptr, 0, nullptr },
    };

    static const FMCPToolHelpData sGraphHelp = {
        TEXT("graph"),
        TEXT("Edit Blueprint graphs and Material node graphs: add/edit nodes, connect/disconnect pins, manage variables, functions, and components"),
        TEXT("action"),
        sGraphActions, UE_ARRAY_COUNT(sGraphActions),
        sGraphCommonParams, UE_ARRAY_COUNT(sGraphCommonParams)
    };

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    /**
     * Validate a single connection array item: must be a JSON object with "source" and "dest" keys.
     * On failure, appends a descriptive error and returns false.
     */
    static bool ParseConnectionItem(
        int32 ConnIdx,
        const TSharedPtr<FJsonValue>& Item,
        TArray<FString>& Errors,
        TSharedPtr<FJsonObject>& OutConnParams,
        const TSharedPtr<FJsonObject>*& OutSourceObj,
        const TSharedPtr<FJsonObject>*& OutDestObj)
    {
        if (Item->Type != EJson::Object || !(OutConnParams = Item->AsObject()).IsValid())
        {
            Errors.Add(FString::Printf(TEXT("Connection %d: item is not a valid JSON object"), ConnIdx));
            return false;
        }

        bool bHasSource = OutConnParams->TryGetObjectField(TEXT("source"), OutSourceObj);
        bool bHasDest = OutConnParams->TryGetObjectField(TEXT("dest"), OutDestObj);
        if (!bHasSource || !bHasDest)
        {
            TArray<FString> Missing;
            if (!bHasSource) Missing.Add(TEXT("'source'"));
            if (!bHasDest) Missing.Add(TEXT("'dest'"));
            TArray<FString> FoundKeys;
            for (const auto& Pair : OutConnParams->Values) FoundKeys.Add(FString::Printf(TEXT("'%s'"), *Pair.Key));
            Errors.Add(FString::Printf(TEXT("Connection %d: missing required key(s) %s. Found keys: %s. Expected: {\"source\":{\"node\":\"GUID\",\"pin\":\"Name\"}, \"dest\":{\"node\":\"GUID\",\"pin\":\"Name\"}}"),
                ConnIdx, *FString::Join(Missing, TEXT(", ")), *FString::Join(FoundKeys, TEXT(", "))));
            return false;
        }
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> MakePinListJson(UEdGraphNode* Node)
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->bHidden) continue;
            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            PinObj->SetStringField(TEXT("type"), UEdGraphSchema_K2::TypeToText(Pin->PinType).ToString());
            Pins.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        return Pins;
    }

    TSharedPtr<FJsonObject> MakeNodeResult(UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("node_id"), FMCPJsonHelpers::GuidToCompact(Node->NodeGuid));
        Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        Obj->SetStringField(TEXT("name"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        Obj->SetArrayField(TEXT("pins"), MakePinListJson(Node));
        return Obj;
    }

    TSharedPtr<FJsonObject> MakeExpressionResult(UMaterialExpression* Expr)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("node_id"), FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid));
        Obj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
        Obj->SetStringField(TEXT("name"), Expr->GetDescription());

        // Inputs
        TArray<TSharedPtr<FJsonValue>> InputPins;
        const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);
        for (int32 i = 0; i < InputCount; ++i)
        {
            FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, i);
            if (!Input) continue;
            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), Expr->GetInputName(i).ToString());
            PinObj->SetStringField(TEXT("direction"), TEXT("input"));
            InputPins.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        Obj->SetArrayField(TEXT("inputs"), InputPins);

        // Outputs
        TArray<TSharedPtr<FJsonValue>> OutputPins;
        const TArray<FExpressionOutput>& ExprOutputs = Expr->GetOutputs();
        for (const FExpressionOutput& Out : ExprOutputs)
        {
            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), FMCPGraphHelpers::ExprOutputPinName(Out));
            PinObj->SetStringField(TEXT("direction"), TEXT("output"));
            OutputPins.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        Obj->SetArrayField(TEXT("outputs"), OutputPins);

        return Obj;
    }

    bool ParseVarType(const FString& TypeStr, FEdGraphPinType& OutType)
    {
        if (TypeStr.Equals(TEXT("float"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
            OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
            return true;
        }
        if (TypeStr.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Int;
            return true;
        }
        if (TypeStr.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Int64;
            return true;
        }
        if (TypeStr.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
            return true;
        }
        if (TypeStr.Equals(TEXT("string"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_String;
            return true;
        }
        if (TypeStr.Equals(TEXT("name"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Name;
            return true;
        }
        if (TypeStr.Equals(TEXT("text"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Text;
            return true;
        }
        if (TypeStr.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
            return true;
        }
        if (TypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
            return true;
        }
        if (TypeStr.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
            return true;
        }
        if (TypeStr.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
        {
            OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            OutType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
            return true;
        }
        // Object:ClassName pattern
        if (TypeStr.StartsWith(TEXT("Object:"), ESearchCase::IgnoreCase))
        {
            FString ClassName = TypeStr.Mid(7);
            UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
            if (!FoundClass) return false;
            OutType.PinCategory = UEdGraphSchema_K2::PC_Object;
            OutType.PinSubCategoryObject = FoundClass;
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // HandleCompile
    // -------------------------------------------------------------------------

    FMCPToolResult HandleCompile(UBlueprint* Blueprint, UMaterial* Material)
    {
        if (Blueprint)
        {
            FCompilerResultsLog ResultsLog;
            FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave, &ResultsLog);
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("target"), Blueprint->GetPathName());
            if (Blueprint->Status == BS_UpToDate)
            {
                Result->SetStringField(TEXT("status"), TEXT("success"));
            }
            else
            {
                Result->SetStringField(TEXT("status"), TEXT("error"));
                TArray<TSharedPtr<FJsonValue>> Errors;
                for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
                {
                    if (Msg->GetSeverity() == EMessageSeverity::Error)
                    {
                        Errors.Add(MakeShared<FJsonValueString>(Msg->ToText().ToString()));
                    }
                }
                Result->SetArrayField(TEXT("errors"), Errors);
            }
            return FMCPJsonHelpers::SuccessResponse(Result);
        }
        if (Material)
        {
            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);
            UMaterialEditingLibrary::RecompileMaterial(Material);
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("target"), OriginalMaterial->GetPathName());
            Result->SetStringField(TEXT("status"), TEXT("recompiled"));
            return FMCPJsonHelpers::SuccessResponse(Result);
        }
        return FMCPToolResult::Error(TEXT("No Blueprint or Material to compile"));
    }

    // -------------------------------------------------------------------------
    // HandleAddNode
    // -------------------------------------------------------------------------

    UEdGraphNode* CreateBlueprintNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& NodeParams, UBlueprint* Blueprint, FString& OutError)
    {
        FString NodeClass;
        NodeParams->TryGetStringField(TEXT("node_class"), NodeClass);

        double PosXd = 0, PosYd = 0;
        if (!NodeParams->TryGetNumberField(TEXT("pos_x"), PosXd) || !NodeParams->TryGetNumberField(TEXT("pos_y"), PosYd))
        {
            OutError = TEXT("'pos_x' and 'pos_y' are required for add_node");
            return nullptr;
        }
        int32 PosX = (int32)PosXd;
        int32 PosY = (int32)PosYd;

        UEdGraphNode* NewNode = nullptr;

        // Helper: read a top-level string field, falling back to "properties" sub-object
        auto GetField = [&NodeParams](const TCHAR* DirectKey, const TCHAR* PropsKey) -> FString
        {
            FString Value;
            NodeParams->TryGetStringField(DirectKey, Value);
            if (Value.IsEmpty())
            {
                const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
                if (NodeParams->TryGetObjectField(TEXT("properties"), PropsPtr))
                    (*PropsPtr)->TryGetStringField(PropsKey, Value);
            }
            return Value;
        };

        // --- CallFunction ---
        if (NodeClass.Equals(TEXT("CallFunction"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
            UK2Node_CallFunction* FuncNode = Creator.CreateNode();

            FString FuncName  = GetField(TEXT("function"),       TEXT("FunctionName"));
            FString FuncOwner = GetField(TEXT("function_owner"), TEXT("FunctionOwner"));

            if (!FuncName.IsEmpty())
            {
                UClass* OwnerClass = nullptr;
                if (!FuncOwner.IsEmpty())
                {
                    OwnerClass = FindFirstObject<UClass>(*FuncOwner, EFindFirstObjectOptions::EnsureIfAmbiguous);
                }
                if (OwnerClass)
                {
                    UFunction* Func = OwnerClass->FindFunctionByName(FName(*FuncName));
                    if (Func)
                    {
                        FuncNode->SetFromFunction(Func);
                    }
                    else
                    {
                        FuncNode->FunctionReference.SetExternalMember(FName(*FuncName), OwnerClass);
                    }
                }
                else
                {
                    // Try global search for library functions (e.g. PrintString)
                    UFunction* GlobalFunc = FindFirstObject<UFunction>(*FuncName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                    if (GlobalFunc)
                        FuncNode->SetFromFunction(GlobalFunc);
                    else
                        FuncNode->FunctionReference.SetSelfMember(FName(*FuncName));
                }
            }
            FuncNode->NodePosX = PosX;
            FuncNode->NodePosY = PosY;
            FuncNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = FuncNode;
        }
        // --- Event (override) ---
        else if (NodeClass.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
            UK2Node_Event* EventNode = Creator.CreateNode();

            FString EventName;
            NodeParams->TryGetStringField(TEXT("event_name"), EventName);

            // Map common names to internal names
            if (EventName.Equals(TEXT("BeginPlay"), ESearchCase::IgnoreCase))      EventName = TEXT("ReceiveBeginPlay");
            else if (EventName.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))      EventName = TEXT("ReceiveTick");
            else if (EventName.Equals(TEXT("EndPlay"), ESearchCase::IgnoreCase))   EventName = TEXT("ReceiveEndPlay");

            UClass* ParentClass = (Blueprint && Blueprint->ParentClass) ? Blueprint->ParentClass.Get() : AActor::StaticClass();
            EventNode->EventReference.SetExternalMember(FName(*EventName), ParentClass);
            EventNode->bOverrideFunction = true;
            EventNode->NodePosX = PosX;
            EventNode->NodePosY = PosY;
            EventNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = EventNode;
        }
        // --- CustomEvent ---
        else if (NodeClass.Equals(TEXT("CustomEvent"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
            UK2Node_CustomEvent* CustomNode = Creator.CreateNode();

            FString EventName;
            NodeParams->TryGetStringField(TEXT("event_name"), EventName);
            if (!EventName.IsEmpty())
            {
                CustomNode->CustomFunctionName = FName(*EventName);
            }
            CustomNode->NodePosX = PosX;
            CustomNode->NodePosY = PosY;
            CustomNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = CustomNode;
        }
        // --- VariableGet ---
        else if (NodeClass.Equals(TEXT("VariableGet"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
            UK2Node_VariableGet* VarNode = Creator.CreateNode();

            FString VarName = GetField(TEXT("variable_name"), TEXT("VariableName"));
            if (!VarName.IsEmpty())
            {
                VarNode->VariableReference.SetSelfMember(FName(*VarName));
            }
            VarNode->NodePosX = PosX;
            VarNode->NodePosY = PosY;
            VarNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = VarNode;
        }
        // --- VariableSet ---
        else if (NodeClass.Equals(TEXT("VariableSet"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
            UK2Node_VariableSet* VarNode = Creator.CreateNode();

            FString VarName = GetField(TEXT("variable_name"), TEXT("VariableName"));
            if (!VarName.IsEmpty())
            {
                VarNode->VariableReference.SetSelfMember(FName(*VarName));
            }
            VarNode->NodePosX = PosX;
            VarNode->NodePosY = PosY;
            VarNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = VarNode;
        }
        // --- Branch / IfThenElse ---
        else if (NodeClass.Equals(TEXT("Branch"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("IfThenElse"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
            UK2Node_IfThenElse* BranchNode = Creator.CreateNode();
            BranchNode->NodePosX = PosX;
            BranchNode->NodePosY = PosY;
            BranchNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = BranchNode;
        }
        // --- Sequence ---
        else if (NodeClass.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*Graph);
            UK2Node_ExecutionSequence* SeqNode = Creator.CreateNode();
            SeqNode->NodePosX = PosX;
            SeqNode->NodePosY = PosY;
            SeqNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SeqNode;
        }
        // --- Self ---
        else if (NodeClass.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_Self> Creator(*Graph);
            UK2Node_Self* SelfNode = Creator.CreateNode();
            SelfNode->NodePosX = PosX;
            SelfNode->NodePosY = PosY;
            SelfNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SelfNode;
        }
        // --- DynamicCast / CastTo ---
        else if (NodeClass.Equals(TEXT("DynamicCast"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("CastTo"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
            UK2Node_DynamicCast* CastNode = Creator.CreateNode();

            // Use function_owner as the target class to cast to
            FString TargetClassName;
            NodeParams->TryGetStringField(TEXT("function_owner"), TargetClassName);
            if (!TargetClassName.IsEmpty())
            {
                UClass* TargetClass = FindFirstObject<UClass>(*TargetClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                if (TargetClass)
                {
                    CastNode->TargetType = TargetClass;
                }
            }
            CastNode->NodePosX = PosX;
            CastNode->NodePosY = PosY;
            CastNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = CastNode;
        }
        // --- SpawnActor ---
        else if (NodeClass.Equals(TEXT("SpawnActor"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_SpawnActorFromClass> Creator(*Graph);
            UK2Node_SpawnActorFromClass* SpawnNode = Creator.CreateNode();
            SpawnNode->NodePosX = PosX;
            SpawnNode->NodePosY = PosY;
            SpawnNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SpawnNode;
        }
        // --- MakeArray ---
        else if (NodeClass.Equals(TEXT("MakeArray"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_MakeArray> Creator(*Graph);
            UK2Node_MakeArray* ArrayNode = Creator.CreateNode();
            ArrayNode->NodePosX = PosX;
            ArrayNode->NodePosY = PosY;
            ArrayNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = ArrayNode;
        }
        // --- Select ---
        else if (NodeClass.Equals(TEXT("Select"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_Select> Creator(*Graph);
            UK2Node_Select* SelectNode = Creator.CreateNode();
            SelectNode->NodePosX = PosX;
            SelectNode->NodePosY = PosY;
            SelectNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SelectNode;
        }
        // --- SwitchOnInt ---
        else if (NodeClass.Equals(TEXT("SwitchOnInt"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("SwitchInteger"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_SwitchInteger> Creator(*Graph);
            UK2Node_SwitchInteger* SwitchNode = Creator.CreateNode();
            SwitchNode->NodePosX = PosX;
            SwitchNode->NodePosY = PosY;
            SwitchNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SwitchNode;
        }
        // --- SwitchOnString ---
        else if (NodeClass.Equals(TEXT("SwitchOnString"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("SwitchString"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_SwitchString> Creator(*Graph);
            UK2Node_SwitchString* SwitchNode = Creator.CreateNode();
            SwitchNode->NodePosX = PosX;
            SwitchNode->NodePosY = PosY;
            SwitchNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SwitchNode;
        }
        // --- SwitchOnEnum ---
        else if (NodeClass.Equals(TEXT("SwitchOnEnum"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("SwitchEnum"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_SwitchEnum> Creator(*Graph);
            UK2Node_SwitchEnum* SwitchNode = Creator.CreateNode();
            SwitchNode->NodePosX = PosX;
            SwitchNode->NodePosY = PosY;
            SwitchNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = SwitchNode;
        }
        // --- MacroInstance / ForEachLoop ---
        else if (NodeClass.Equals(TEXT("MacroInstance"), ESearchCase::IgnoreCase) ||
                 NodeClass.Equals(TEXT("ForEachLoop"), ESearchCase::IgnoreCase))
        {
            FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
            UK2Node_MacroInstance* MacroNode = Creator.CreateNode();

            FString MacroName;
            NodeParams->TryGetStringField(TEXT("event_name"), MacroName);
            if (MacroName.IsEmpty())
            {
                MacroName = NodeClass.Equals(TEXT("ForEachLoop"), ESearchCase::IgnoreCase)
                    ? TEXT("ForEachLoop") : TEXT("");
            }

            if (!MacroName.IsEmpty())
            {
                UBlueprint* StandardMacros = LoadObject<UBlueprint>(
                    nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
                if (StandardMacros)
                {
                    for (UEdGraph* MacroGraph : StandardMacros->MacroGraphs)
                    {
                        if (MacroGraph && MacroGraph->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
                        {
                            MacroNode->SetMacroGraph(MacroGraph);
                            break;
                        }
                    }
                }
            }

            MacroNode->NodePosX = PosX;
            MacroNode->NodePosY = PosY;
            MacroNode->AllocateDefaultPins();
            Creator.Finalize();
            NewNode = MacroNode;
        }

        return NewNode;
    }

    FMCPToolResult HandleAddNode(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint, UMaterial* Material)
    {
        // Collect items: batch from "nodes" array or single item
        TArray<TSharedPtr<FJsonValue>> NodeItems;
        const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
        if (Params->TryGetArrayField(TEXT("nodes"), NodesArr))
        {
            NodeItems = *NodesArr;
        }
        else
        {
            NodeItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<TSharedPtr<FJsonValue>> Results;

        if (Blueprint)
        {
            FString GraphName;
            if (!Params->TryGetStringField(TEXT("graph"), GraphName))
                Params->TryGetStringField(TEXT("graph_name"), GraphName);
            UEdGraph* Graph = FMCPGraphHelpers::FindGraphByName(Blueprint, GraphName);
            if (!Graph)
            {
                TArray<FString> AvailableGraphs;
                for (UEdGraph* G : Blueprint->UbergraphPages)
                    if (G) AvailableGraphs.Add(G->GetName());
                for (UEdGraph* G : Blueprint->FunctionGraphs)
                    if (G) AvailableGraphs.Add(G->GetName());
                return FMCPToolResult::Error(FString::Printf(
                    TEXT("Graph '%s' not found. Available: %s"),
                    *GraphName, *FString::Join(AvailableGraphs, TEXT(", "))));
            }

            FScopedTransaction Transaction(LOCTEXT("MCPAddNode", "MCP Add Blueprint Node"));
            Blueprint->Modify();

            bool bAnyNodeCreated = false;
            for (const TSharedPtr<FJsonValue>& Item : NodeItems)
            {
                TSharedPtr<FJsonObject> NodeParams = Item->AsObject();
                if (!NodeParams.IsValid()) continue;

                FString NodeError;
                UEdGraphNode* NewNode = CreateBlueprintNode(Graph, NodeParams, Blueprint, NodeError);
                if (NewNode)
                {
                    Results.Add(MakeShared<FJsonValueObject>(MakeNodeResult(NewNode)));
                    bAnyNodeCreated = true;
                }
                else
                {
                    TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
                    if (!NodeError.IsEmpty())
                    {
                        ErrObj->SetStringField(TEXT("error"), NodeError);
                    }
                    else
                    {
                        FString NodeClass;
                        NodeParams->TryGetStringField(TEXT("node_class"), NodeClass);
                        ErrObj->SetStringField(TEXT("error"), FString::Printf(
                            TEXT("Unknown node_class: '%s'. Valid classes: CallFunction, Event, CustomEvent, VariableGet, VariableSet, Branch, Sequence, Self, DynamicCast, SpawnActor, MakeArray, Select, SwitchOnInt, SwitchOnString, SwitchOnEnum, MacroInstance, ForEachLoop"),
                            *NodeClass));
                    }
                    Results.Add(MakeShared<FJsonValueObject>(ErrObj));
                }
            }

            if (bAnyNodeCreated)
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);
            }
        }
        else if (Material)
        {
            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);
            FScopedTransaction Transaction(LOCTEXT("MCPAddExpression", "MCP Add Material Expression"));
            Material->Modify();
            Material->PreEditChange(nullptr);

            for (const TSharedPtr<FJsonValue>& Item : NodeItems)
            {
                TSharedPtr<FJsonObject> NodeParams = Item->AsObject();
                if (!NodeParams.IsValid()) continue;

                FString NodeClass;
                NodeParams->TryGetStringField(TEXT("node_class"), NodeClass);

                double PosXd = 0, PosYd = 0;
                if (!NodeParams->TryGetNumberField(TEXT("pos_x"), PosXd) || !NodeParams->TryGetNumberField(TEXT("pos_y"), PosYd))
                {
                    TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
                    ErrObj->SetStringField(TEXT("error"), TEXT("'pos_x' and 'pos_y' are required for add_node"));
                    Results.Add(MakeShared<FJsonValueObject>(ErrObj));
                    continue;
                }
                int32 PosX = (int32)PosXd;
                int32 PosY = (int32)PosYd;

                // Resolve aliases
                FString ExprClassName = NodeClass;
                if (ExprClassName.Equals(TEXT("Lerp"), ESearchCase::IgnoreCase))
                    ExprClassName = TEXT("LinearInterpolate");
                else if (ExprClassName.Equals(TEXT("TexCoord"), ESearchCase::IgnoreCase))
                    ExprClassName = TEXT("TextureCoordinate");

                // Try prepending MaterialExpression prefix
                FString FullClassName = ExprClassName;
                if (!FullClassName.StartsWith(TEXT("MaterialExpression"), ESearchCase::IgnoreCase))
                {
                    FullClassName = FString::Printf(TEXT("MaterialExpression%s"), *ExprClassName);
                }

                UClass* ExprClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                if (!ExprClass)
                {
                    ExprClass = FindFirstObject<UClass>(*ExprClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                }
                if (!ExprClass || !ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
                {
                    TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
                    ErrObj->SetStringField(TEXT("error"), FString::Printf(
                        TEXT("Unknown expression class: '%s'. Target is a Material - use expression names like Multiply, Add, Constant, ScalarParameter, VectorParameter, TextureCoordinate, Lerp"),
                        *NodeClass));
                    Results.Add(MakeShared<FJsonValueObject>(ErrObj));
                    continue;
                }

                UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, PosX, PosY);
                if (Expr)
                {
                    if (!Expr->MaterialExpressionGuid.IsValid())
                    {
                        Expr->MaterialExpressionGuid = FGuid::NewGuid();
                    }
                    Results.Add(MakeShared<FJsonValueObject>(MakeExpressionResult(Expr)));
                }
                else
                {
                    TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
                    ErrObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to create expression: '%s'"), *NodeClass));
                    Results.Add(MakeShared<FJsonValueObject>(ErrObj));
                }
            }
            Material->PostEditChange();
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);
        }

        if (Results.Num() == 1)
        {
            TSharedPtr<FJsonObject> Single = Results[0]->AsObject();
            if (Single.IsValid() && Single->HasField(TEXT("error")))
            {
                FString ErrMsg;
                Single->TryGetStringField(TEXT("error"), ErrMsg);
                return FMCPToolResult::Error(ErrMsg);
            }
            return FMCPJsonHelpers::SuccessResponse(Single);
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("nodes"), Results);
        Result->SetNumberField(TEXT("count"), Results.Num());
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleEditNode
    // -------------------------------------------------------------------------

    FMCPToolResult HandleEditNode(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint, UMaterial* Material)
    {
        TArray<TSharedPtr<FJsonValue>> EditItems;
        const TArray<TSharedPtr<FJsonValue>>* EditsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("edits"), EditsArr))
        {
            EditItems = *EditsArr;
        }
        else
        {
            EditItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<FString> Modified;
        TArray<FString> Warnings;

        auto GetNodeId = [](const TSharedPtr<FJsonObject>& Obj) -> FString
        {
            FString Id;
            if (!Obj->TryGetStringField(TEXT("node_id"), Id))
                Obj->TryGetStringField(TEXT("node"), Id);
            return Id;
        };

        auto ApplyProperties = [&Warnings](UObject* Target, const TSharedPtr<FJsonObject>& PropsObj)
        {
            for (const auto& Pair : PropsObj->Values)
            {
                FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*Pair.Key));
                if (Prop)
                {
                    FString ValueStr;
                    if (FMCPJsonHelpers::JsonValueToPropertyString(Pair.Value, ValueStr))
                    {
                        Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(Target), Target, PPF_None);
                    }
                }
                else
                {
                    Warnings.Add(FString::Printf(TEXT("Property '%s' not found on %s"), *Pair.Key, *Target->GetClass()->GetName()));
                }
            }
        };

        if (Blueprint)
        {
            FScopedTransaction Transaction(LOCTEXT("MCPEditNode", "MCP Edit Blueprint Node"));
            Blueprint->Modify();

            for (const TSharedPtr<FJsonValue>& Item : EditItems)
            {
                TSharedPtr<FJsonObject> EditParams = Item->AsObject();
                if (!EditParams.IsValid()) continue;

                FString NodeGuid = GetNodeId(EditParams);
                UEdGraphNode* Node = FMCPGraphHelpers::FindNodeByGuid(Blueprint, NodeGuid);
                if (!Node)
                {
                    Warnings.Add(FString::Printf(TEXT("Node not found for GUID: '%s'"), *NodeGuid));
                    continue;
                }
                Node->Modify();

                // Apply pos shorthand
                double PosXd = 0, PosYd = 0;
                if (EditParams->TryGetNumberField(TEXT("pos_x"), PosXd))
                    Node->NodePosX = (int32)PosXd;
                if (EditParams->TryGetNumberField(TEXT("pos_y"), PosYd))
                    Node->NodePosY = (int32)PosYd;

                const TSharedPtr<FJsonObject>* PropsObj = nullptr;
                if (EditParams->TryGetObjectField(TEXT("properties"), PropsObj))
                {
                    ApplyProperties(Node, *PropsObj);
                }

                // Set pin defaults
                const TSharedPtr<FJsonObject>* PinDefaultsObj = nullptr;
                if (EditParams->TryGetObjectField(TEXT("pin_defaults"), PinDefaultsObj))
                {
                    const UEdGraphSchema* Schema = Node->GetSchema();
                    for (const auto& Pair : (*PinDefaultsObj)->Values)
                    {
                        UEdGraphPin* Pin = Node->FindPin(FName(*Pair.Key));
                        if (!Pin) continue;

                        FString Value;
                        Pair.Value->TryGetString(Value);

                        if (Schema)
                        {
                            Schema->TrySetDefaultValue(*Pin, Value);
                        }
                        else
                        {
                            Pin->DefaultValue = Value;
                        }
                    }
                }

                Modified.Add(NodeGuid);
            }

            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);
        }
        else if (Material)
        {
            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);
            FScopedTransaction Transaction(LOCTEXT("MCPEditExpression", "MCP Edit Material Expression"));
            Material->Modify();
            Material->PreEditChange(nullptr);

            for (const TSharedPtr<FJsonValue>& Item : EditItems)
            {
                TSharedPtr<FJsonObject> EditParams = Item->AsObject();
                if (!EditParams.IsValid()) continue;

                FString ExprGuid = GetNodeId(EditParams);
                UMaterialExpression* Expr = FMCPGraphHelpers::FindExpressionByGuid(Material, ExprGuid);
                if (!Expr)
                {
                    Warnings.Add(FString::Printf(TEXT("Expression not found for GUID: '%s'"), *ExprGuid));
                    continue;
                }
                Expr->Modify();

                // Apply pos shorthand
                double PosXd = 0, PosYd = 0;
                if (EditParams->TryGetNumberField(TEXT("pos_x"), PosXd))
                    Expr->MaterialExpressionEditorX = (int32)PosXd;
                if (EditParams->TryGetNumberField(TEXT("pos_y"), PosYd))
                    Expr->MaterialExpressionEditorY = (int32)PosYd;

                const TSharedPtr<FJsonObject>* PropsObj = nullptr;
                if (EditParams->TryGetObjectField(TEXT("properties"), PropsObj))
                {
                    ApplyProperties(Expr, *PropsObj);
                }

                Modified.Add(ExprGuid);
            }

            Material->PostEditChange();
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("modified"), FMCPJsonHelpers::ArrayFromStrings(Modified));
        Result->SetNumberField(TEXT("count"), Modified.Num());
        if (Warnings.Num() > 0)
        {
            Result->SetArrayField(TEXT("warnings"), FMCPJsonHelpers::ArrayFromStrings(Warnings));
        }
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleConnect
    // -------------------------------------------------------------------------

    static int32 FindMatOutputIndex(UMaterialExpression* Expr, const FString& OutputName)
    {
        if (!Expr || Expr->Outputs.Num() == 0) return INDEX_NONE;
        if (OutputName.IsEmpty()) return 0;

        for (int32 i = 0; i < Expr->Outputs.Num(); ++i)
        {
            if (FMCPGraphHelpers::ExprOutputPinName(Expr->Outputs[i]).Equals(OutputName, ESearchCase::IgnoreCase))
                return i;
        }
        return INDEX_NONE;
    }

    static bool MatInputExists(UMaterialExpression* Expr, const FString& InputName)
    {
        const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);
        if (InputName.IsEmpty()) return InputCount > 0;

        const FName NameFName(*InputName);
        for (int32 i = 0; i < InputCount; ++i)
        {
            if (Expr->GetInputName(i) == NameFName) return true;
        }
        return false;
    }

    static FString ListAvailableOutputs(UMaterialExpression* Expr)
    {
        TArray<FString> Names;
        for (const FExpressionOutput& Out : Expr->GetOutputs())
            Names.Add(TEXT("'") + FMCPGraphHelpers::ExprOutputPinName(Out) + TEXT("'"));
        return Names.IsEmpty() ? TEXT("(none)") : FString::Join(Names, TEXT(", "));
    }

    static FString ListAvailableInputs(UMaterialExpression* Expr)
    {
        TArray<FString> Names;
        const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(Expr);
        for (int32 i = 0; i < InputCount; ++i)
            Names.Add(TEXT("'") + Expr->GetInputName(i).ToString() + TEXT("'"));
        return Names.IsEmpty() ? TEXT("(none)") : FString::Join(Names, TEXT(", "));
    }

    // If caller passed empty pin name and index 0 was auto-selected, resolve to the real output name
    // so APIs that look up by name (ConnectMaterialExpressions, ConnectMaterialProperty) can find it.
    static FString ResolveSourcePinName(UMaterialExpression* Expr, const FString& PinName, int32 OutIdx)
    {
        if (PinName.IsEmpty() && OutIdx == 0 && Expr->Outputs.IsValidIndex(0))
        {
            FString ActualName = FMCPGraphHelpers::ExprOutputPinName(Expr->Outputs[0]);
            if (!ActualName.IsEmpty())
                return ActualName;
        }
        return PinName;
    }


    FMCPToolResult HandleConnect(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint, UMaterial* Material)
    {
        TArray<TSharedPtr<FJsonValue>> ConnItems;
        const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("connections"), ConnsArr))
        {
            ConnItems = *ConnsArr;
        }
        else
        {
            ConnItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<FString> Connected;
        TArray<FString> Errors;

        if (Blueprint)
        {
            FScopedTransaction Transaction(LOCTEXT("MCPConnect", "MCP Connect Blueprint Pins"));
            Blueprint->Modify();

            for (int32 ConnIdx = 0; ConnIdx < ConnItems.Num(); ++ConnIdx)
            {
                TSharedPtr<FJsonObject> ConnParams;
                const TSharedPtr<FJsonObject>* SourceObj = nullptr;
                const TSharedPtr<FJsonObject>* DestObj = nullptr;
                if (!ParseConnectionItem(ConnIdx, ConnItems[ConnIdx], Errors, ConnParams, SourceObj, DestObj)) continue;

                FString SourceNodeGuid, SourcePinName, DestNodeGuid, DestPinName;
                (*SourceObj)->TryGetStringField(TEXT("node"), SourceNodeGuid);
                (*SourceObj)->TryGetStringField(TEXT("pin"), SourcePinName);
                (*DestObj)->TryGetStringField(TEXT("node"), DestNodeGuid);
                (*DestObj)->TryGetStringField(TEXT("pin"), DestPinName);

                UEdGraphNode* SourceNode = FMCPGraphHelpers::FindNodeByGuid(Blueprint, SourceNodeGuid);
                UEdGraphNode* DestNode   = FMCPGraphHelpers::FindNodeByGuid(Blueprint, DestNodeGuid);
                if (!SourceNode || !DestNode)
                {
                    if (!SourceNode) Errors.Add(FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeGuid));
                    if (!DestNode)   Errors.Add(FString::Printf(TEXT("Dest node '%s' not found"), *DestNodeGuid));
                    continue;
                }

                UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName), EGPD_Output);
                UEdGraphPin* DestPin   = DestNode->FindPin(FName(*DestPinName), EGPD_Input);

                // Fallback: search without direction
                if (!SourcePin) SourcePin = SourceNode->FindPin(FName(*SourcePinName));
                if (!DestPin)   DestPin   = DestNode->FindPin(FName(*DestPinName));

                if (!SourcePin || !DestPin)
                {
                    if (!SourcePin) Errors.Add(FString::Printf(TEXT("Source pin '%s' not found on node '%s'"), *SourcePinName, *SourceNodeGuid));
                    if (!DestPin)   Errors.Add(FString::Printf(TEXT("Dest pin '%s' not found on node '%s'"), *DestPinName, *DestNodeGuid));
                    continue;
                }

                const UEdGraphSchema* Schema = SourcePin->GetSchema();
                if (Schema && Schema->TryCreateConnection(SourcePin, DestPin))
                {
                    Connected.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
                        *SourceNodeGuid, *SourcePinName, *DestNodeGuid, *DestPinName));
                }
                else
                {
                    FString Reason;
                    if (Schema)
                    {
                        const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, DestPin);
                        Reason = Response.Message.ToString();
                    }
                    if (Reason.IsEmpty())
                    {
                        Errors.Add(FString::Printf(TEXT("Connection rejected: %s.%s -> %s.%s"),
                            *SourceNodeGuid, *SourcePinName, *DestNodeGuid, *DestPinName));
                    }
                    else
                    {
                        Errors.Add(FString::Printf(TEXT("Connection rejected: %s.%s -> %s.%s (%s)"),
                            *SourceNodeGuid, *SourcePinName, *DestNodeGuid, *DestPinName, *Reason));
                    }
                }
            }

            if (!Connected.IsEmpty())
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);
            }
        }
        else if (Material)
        {
            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);
            FScopedTransaction Transaction(LOCTEXT("MCPConnectMat", "MCP Connect Material Expressions"));
            Material->Modify();
            Material->PreEditChange(nullptr);

            // Shared helper: resolve source pin and connect to a material property
            auto ConnectToMatProperty = [&](UMaterialExpression* SrcExpr, const FString& SrcNodeGuid,
                const FString& SrcPinName, EMaterialProperty MatProp, const FString& PropDisplayName)
            {
                int32 SrcOutIdx = FindMatOutputIndex(SrcExpr, SrcPinName);
                if (SrcOutIdx == INDEX_NONE)
                {
                    Errors.Add(FString::Printf(
                        TEXT("Output pin '%s' not found on %s (%s). Available outputs: %s"),
                        *SrcPinName, *SrcNodeGuid, *SrcExpr->GetClass()->GetName(),
                        *ListAvailableOutputs(SrcExpr)));
                    return;
                }
                const FString ResolvedPin = ResolveSourcePinName(SrcExpr, SrcPinName, SrcOutIdx);
                if (UMaterialEditingLibrary::ConnectMaterialProperty(SrcExpr, ResolvedPin, MatProp))
                {
                    Connected.Add(FString::Printf(TEXT("%s -> %s"), *SrcNodeGuid, *PropDisplayName));
                }
                else
                {
                    Errors.Add(FString::Printf(
                        TEXT("Failed to connect %s.'%s' to material property '%s' (property may be inactive for this blend mode). Available outputs: %s"),
                        *SrcNodeGuid, *ResolvedPin, *PropDisplayName,
                        *ListAvailableOutputs(SrcExpr)));
                }
            };

            for (int32 ConnIdx = 0; ConnIdx < ConnItems.Num(); ++ConnIdx)
            {
                TSharedPtr<FJsonObject> ConnParams;
                const TSharedPtr<FJsonObject>* SourceObj = nullptr;
                const TSharedPtr<FJsonObject>* DestObj = nullptr;
                if (!ParseConnectionItem(ConnIdx, ConnItems[ConnIdx], Errors, ConnParams, SourceObj, DestObj)) continue;

                FString SourceNodeGuid, SourcePinName;
                (*SourceObj)->TryGetStringField(TEXT("node"), SourceNodeGuid);
                (*SourceObj)->TryGetStringField(TEXT("pin"), SourcePinName);

                UMaterialExpression* SourceExpr = FMCPGraphHelpers::FindExpressionByGuid(Material, SourceNodeGuid);
                if (!SourceExpr)
                {
                    Errors.Add(FString::Printf(TEXT("Source expression '%s' not found"), *SourceNodeGuid));
                    continue;
                }

                FString MaterialProperty;
                (*DestObj)->TryGetStringField(TEXT("property"), MaterialProperty);

                if (!MaterialProperty.IsEmpty())
                {
                    EMaterialProperty MatProp = MP_BaseColor;
                    if (!FMCPGraphHelpers::MapMaterialProperty(MaterialProperty, MatProp))
                    {
                        Errors.Add(FString::Printf(TEXT("Unknown material property '%s'"), *MaterialProperty));
                        continue;
                    }
                    ConnectToMatProperty(SourceExpr, SourceNodeGuid, SourcePinName, MatProp, MaterialProperty);
                }
                else
                {
                    FString DestNodeGuid, DestPinName;
                    (*DestObj)->TryGetStringField(TEXT("node"), DestNodeGuid);
                    (*DestObj)->TryGetStringField(TEXT("pin"), DestPinName);

                    UMaterialExpression* DestExpr = FMCPGraphHelpers::FindExpressionByGuid(Material, DestNodeGuid);
                    if (!DestExpr)
                    {
                        // Auto-resolve: if dest.node is a known alias for the material output node,
                        // treat dest.pin as a material property
                        if (FMCPGraphHelpers::IsOutputNodeAlias(DestNodeGuid))
                        {
                            EMaterialProperty MatProp;
                            FString AliasError;
                            if (!FMCPGraphHelpers::ResolveAliasToMaterialProperty(DestNodeGuid, DestPinName, MatProp, AliasError))
                            {
                                Errors.Add(AliasError);
                                continue;
                            }
                            ConnectToMatProperty(SourceExpr, SourceNodeGuid, SourcePinName, MatProp, DestPinName);
                            continue;
                        }

                        Errors.Add(FString::Printf(TEXT("Dest expression '%s' not found"), *DestNodeGuid));
                        continue;
                    }

                    int32 SrcOutIdx = FindMatOutputIndex(SourceExpr, SourcePinName);
                    if (SrcOutIdx == INDEX_NONE)
                    {
                        Errors.Add(FString::Printf(
                            TEXT("Output pin '%s' not found on %s (%s). Available outputs: %s"),
                            *SourcePinName, *SourceNodeGuid, *SourceExpr->GetClass()->GetName(),
                            *ListAvailableOutputs(SourceExpr)));
                        continue;
                    }
                    if (!MatInputExists(DestExpr, DestPinName))
                    {
                        Errors.Add(FString::Printf(
                            TEXT("Input pin '%s' not found on %s (%s). Available inputs: %s"),
                            *DestPinName, *DestNodeGuid, *DestExpr->GetClass()->GetName(),
                            *ListAvailableInputs(DestExpr)));
                        continue;
                    }
                    const FString ResolvedSrcPin = ResolveSourcePinName(SourceExpr, SourcePinName, SrcOutIdx);
                    if (UMaterialEditingLibrary::ConnectMaterialExpressions(SourceExpr, ResolvedSrcPin, DestExpr, DestPinName))
                    {
                        Connected.Add(FString::Printf(TEXT("%s -> %s"), *SourceNodeGuid, *DestNodeGuid));
                    }
                    else
                    {
                        const FString SrcAutoNote = SourcePinName.IsEmpty() ? TEXT(" (source pin auto-selected from index 0)") : TEXT("");
                        Errors.Add(FString::Printf(
                            TEXT("Failed to connect %s.'%s' -> %s.'%s'%s. Available outputs: %s. Available inputs: %s"),
                            *SourceNodeGuid, *ResolvedSrcPin, *DestNodeGuid, *DestPinName, *SrcAutoNote,
                            *ListAvailableOutputs(SourceExpr),
                            *ListAvailableInputs(DestExpr)));
                    }
                }
            }

            Material->PostEditChange();
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);
        }

        if (Connected.IsEmpty() && Errors.Num() > 0)
        {
            return FMCPToolResult::Error(FString::Join(Errors, TEXT("; ")));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("connected"), FMCPJsonHelpers::ArrayFromStrings(Connected));
        Result->SetNumberField(TEXT("count"), Connected.Num());
        if (Errors.Num() > 0)
        {
            Result->SetArrayField(TEXT("errors"), FMCPJsonHelpers::ArrayFromStrings(Errors));
        }
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleDisconnect
    // -------------------------------------------------------------------------

    FMCPToolResult HandleDisconnect(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint, UMaterial* Material)
    {
        TArray<TSharedPtr<FJsonValue>> ConnItems;
        const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("connections"), ConnsArr))
        {
            ConnItems = *ConnsArr;
        }
        else
        {
            ConnItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<FString> Disconnected;
        TArray<FString> Errors;

        if (Blueprint)
        {
            FScopedTransaction Transaction(LOCTEXT("MCPDisconnect", "MCP Disconnect Blueprint Pins"));
            Blueprint->Modify();

            for (int32 ConnIdx = 0; ConnIdx < ConnItems.Num(); ++ConnIdx)
            {
                TSharedPtr<FJsonObject> ConnParams;
                const TSharedPtr<FJsonObject>* SourceObj = nullptr;
                const TSharedPtr<FJsonObject>* DestObj = nullptr;
                if (!ParseConnectionItem(ConnIdx, ConnItems[ConnIdx], Errors, ConnParams, SourceObj, DestObj)) continue;

                FString SourceNodeGuid, SourcePinName, DestNodeGuid, DestPinName;
                (*SourceObj)->TryGetStringField(TEXT("node"), SourceNodeGuid);
                (*SourceObj)->TryGetStringField(TEXT("pin"), SourcePinName);
                (*DestObj)->TryGetStringField(TEXT("node"), DestNodeGuid);
                (*DestObj)->TryGetStringField(TEXT("pin"), DestPinName);

                UEdGraphNode* SourceNode = FMCPGraphHelpers::FindNodeByGuid(Blueprint, SourceNodeGuid);
                UEdGraphNode* DestNode   = FMCPGraphHelpers::FindNodeByGuid(Blueprint, DestNodeGuid);
                if (!SourceNode || !DestNode)
                {
                    if (!SourceNode) Errors.Add(FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeGuid));
                    if (!DestNode)   Errors.Add(FString::Printf(TEXT("Dest node '%s' not found"), *DestNodeGuid));
                    continue;
                }

                UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName), EGPD_Output);
                UEdGraphPin* DestPin   = DestNode->FindPin(FName(*DestPinName), EGPD_Input);

                if (!SourcePin) SourcePin = SourceNode->FindPin(FName(*SourcePinName));
                if (!DestPin)   DestPin   = DestNode->FindPin(FName(*DestPinName));

                if (!SourcePin || !DestPin)
                {
                    if (!SourcePin) Errors.Add(FString::Printf(TEXT("Source pin '%s' not found on node '%s'"), *SourcePinName, *SourceNodeGuid));
                    if (!DestPin)   Errors.Add(FString::Printf(TEXT("Dest pin '%s' not found on node '%s'"), *DestPinName, *DestNodeGuid));
                    continue;
                }

                if (!SourcePin->LinkedTo.Contains(DestPin))
                {
                    continue;
                }
                SourceNode->Modify();
                DestNode->Modify();
                SourcePin->BreakLinkTo(DestPin);
                Disconnected.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
                    *SourceNodeGuid, *SourcePinName, *DestNodeGuid, *DestPinName));
            }

            if (!Disconnected.IsEmpty())
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);
            }
        }
        else if (Material)
        {
            UMaterial* OriginalMaterial = Material;
            Material = FMCPGraphHelpers::ResolveMaterialForEditing(Material);
            FScopedTransaction Transaction(LOCTEXT("MCPDisconnectMat", "MCP Disconnect Material Expressions"));
            Material->Modify();
            Material->PreEditChange(nullptr);

            for (int32 ConnIdx = 0; ConnIdx < ConnItems.Num(); ++ConnIdx)
            {
                TSharedPtr<FJsonObject> ConnParams;
                const TSharedPtr<FJsonObject>* SourceObj = nullptr;
                const TSharedPtr<FJsonObject>* DestObj = nullptr;
                if (!ParseConnectionItem(ConnIdx, ConnItems[ConnIdx], Errors, ConnParams, SourceObj, DestObj)) continue;

                FString SourceNodeGuid;
                (*SourceObj)->TryGetStringField(TEXT("node"), SourceNodeGuid);

                UMaterialExpression* SourceExpr = FMCPGraphHelpers::FindExpressionByGuid(Material, SourceNodeGuid);
                if (!SourceExpr)
                {
                    Errors.Add(FString::Printf(TEXT("Source expression '%s' not found"), *SourceNodeGuid));
                    continue;
                }

                FString MaterialProperty;
                (*DestObj)->TryGetStringField(TEXT("property"), MaterialProperty);

                if (!MaterialProperty.IsEmpty())
                {
                    // Disconnect from material root property: check known inputs
                    EMaterialProperty MatProp = MP_BaseColor;
                    if (!FMCPGraphHelpers::MapMaterialProperty(MaterialProperty, MatProp))
                    {
                        Errors.Add(FString::Printf(TEXT("Unknown material property '%s'"), *MaterialProperty));
                        continue;
                    }

                    // Null out root input if it references SourceExpr
                    FExpressionInput* PropInput = Material->GetExpressionInputForProperty(MatProp);
                    bool bCleared = false;
                    if (PropInput && PropInput->Expression == SourceExpr)
                    {
                        SourceExpr->Modify();
                        PropInput->Expression = nullptr;
                        PropInput->OutputIndex = 0;
                        bCleared = true;
                    }
                    if (bCleared)
                    {
                        Disconnected.Add(FString::Printf(TEXT("%s -> %s"), *SourceNodeGuid, *MaterialProperty));
                    }
                }
                else
                {
                    FString DestNodeGuid, DestPinName;
                    (*DestObj)->TryGetStringField(TEXT("node"), DestNodeGuid);
                    (*DestObj)->TryGetStringField(TEXT("pin"), DestPinName);

                    UMaterialExpression* DestExpr = FMCPGraphHelpers::FindExpressionByGuid(Material, DestNodeGuid);
                    if (!DestExpr)
                    {
                        // Auto-resolve: if dest.node is an output alias, treat dest.pin as material property
                        if (FMCPGraphHelpers::IsOutputNodeAlias(DestNodeGuid))
                        {
                            EMaterialProperty MatProp;
                            FString AliasError;
                            if (!FMCPGraphHelpers::ResolveAliasToMaterialProperty(DestNodeGuid, DestPinName, MatProp, AliasError))
                            {
                                Errors.Add(AliasError);
                                continue;
                            }
                            FExpressionInput* PropInput = Material->GetExpressionInputForProperty(MatProp);
                            if (PropInput && PropInput->Expression == SourceExpr)
                            {
                                SourceExpr->Modify();
                                PropInput->Expression = nullptr;
                                PropInput->OutputIndex = 0;
                                Disconnected.Add(FString::Printf(TEXT("%s -> %s"), *SourceNodeGuid, *DestPinName));
                            }
                            continue;
                        }
                        Errors.Add(FString::Printf(TEXT("Dest expression '%s' not found"), *DestNodeGuid));
                        continue;
                    }

                    // Iterate dest expression inputs and null any matching SourceExpr
                    bool bAnyDisconnected = false;
                    DestExpr->Modify();
                    const int32 InputCount = FMCPGraphHelpers::GetExpressionInputCount(DestExpr);
                    for (int32 i = 0; i < InputCount; ++i)
                    {
                        FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(DestExpr, i);
                        if (!Input || Input->Expression != SourceExpr) continue;
                        // If DestPinName is specified, match by input name
                        if (!DestPinName.IsEmpty() && !DestExpr->GetInputName(i).ToString().Equals(DestPinName, ESearchCase::IgnoreCase))
                            continue;
                        Input->Expression = nullptr;
                        Input->OutputIndex = 0;
                        bAnyDisconnected = true;
                    }
                    if (bAnyDisconnected)
                    {
                        Disconnected.Add(FString::Printf(TEXT("%s -> %s"), *SourceNodeGuid, *DestNodeGuid));
                    }
                }
            }

            Material->PostEditChange();
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalMaterial);
        }

        if (Disconnected.IsEmpty() && Errors.Num() > 0)
        {
            return FMCPToolResult::Error(FString::Join(Errors, TEXT("; ")));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("disconnected"), FMCPJsonHelpers::ArrayFromStrings(Disconnected));
        Result->SetNumberField(TEXT("count"), Disconnected.Num());
        if (Errors.Num() > 0)
        {
            Result->SetArrayField(TEXT("errors"), FMCPJsonHelpers::ArrayFromStrings(Errors));
        }
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleAddVariable
    // -------------------------------------------------------------------------

    FMCPToolResult HandleAddVariable(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint)
    {
        if (!Blueprint) return FMCPToolResult::Error(TEXT("add_variable requires a Blueprint target"));

        TArray<TSharedPtr<FJsonValue>> VarItems;
        const TArray<TSharedPtr<FJsonValue>>* VarsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("variables"), VarsArr))
        {
            VarItems = *VarsArr;
        }
        else
        {
            VarItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<FString> Added;
        TArray<FString> Errors;
        FScopedTransaction Transaction(LOCTEXT("MCPAddVariable", "MCP Add Blueprint Variable"));
        Blueprint->Modify();

        for (const TSharedPtr<FJsonValue>& Item : VarItems)
        {
            TSharedPtr<FJsonObject> VarParams = Item->AsObject();
            if (!VarParams.IsValid()) continue;

            FString Name, TypeStr, DefaultValue, Category;
            VarParams->TryGetStringField(TEXT("name"), Name);
            if (Name.IsEmpty()) VarParams->TryGetStringField(TEXT("var_name"), Name);
            VarParams->TryGetStringField(TEXT("var_type"), TypeStr);
            VarParams->TryGetStringField(TEXT("default_value"), DefaultValue);
            VarParams->TryGetStringField(TEXT("category"), Category);

            if (Name.IsEmpty() || TypeStr.IsEmpty())
            {
                Errors.Add(FString::Printf(TEXT("Variable requires 'name' and 'var_type' (got name='%s', var_type='%s')"), *Name, *TypeStr));
                continue;
            }

            FEdGraphPinType PinType;
            if (!ParseVarType(TypeStr, PinType))
            {
                Errors.Add(FString::Printf(TEXT("Invalid var_type '%s' for variable '%s'. Valid: float, int, int64, bool, string, name, text, byte, Vector, Rotator, Transform, Object:ClassName"), *TypeStr, *Name));
                continue;
            }

            if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), PinType, DefaultValue))
            {
                Errors.Add(FString::Printf(TEXT("Failed to add variable '%s' (may already exist)"), *Name));
                continue;
            }

            if (!Category.IsEmpty())
            {
                FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*Name), nullptr, FText::FromString(Category));
            }
            Added.Add(Name);
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("added"), FMCPJsonHelpers::ArrayFromStrings(Added));
        Result->SetNumberField(TEXT("count"), Added.Num());
        if (Errors.Num() > 0)
            Result->SetArrayField(TEXT("errors"), FMCPJsonHelpers::ArrayFromStrings(Errors));
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleEditVariable
    // -------------------------------------------------------------------------

    FMCPToolResult HandleEditVariable(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint)
    {
        if (!Blueprint) return FMCPToolResult::Error(TEXT("edit_variable requires a Blueprint target"));

        FString Name, NewName, TypeStr, DefaultValue, Category;
        Params->TryGetStringField(TEXT("name"), Name);
        Params->TryGetStringField(TEXT("new_name"), NewName);
        Params->TryGetStringField(TEXT("var_type"), TypeStr);
        Params->TryGetStringField(TEXT("default_value"), DefaultValue);
        Params->TryGetStringField(TEXT("category"), Category);

        if (Name.IsEmpty()) return FMCPToolResult::Error(TEXT("'name' is required"));

        FScopedTransaction Transaction(LOCTEXT("MCPEditVariable", "MCP Edit Blueprint Variable"));
        Blueprint->Modify();

        TArray<FString> Changes;

        if (!NewName.IsEmpty())
        {
            FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*Name), FName(*NewName));
            Changes.Add(FString::Printf(TEXT("renamed to %s"), *NewName));
            Name = NewName;
        }

        if (!TypeStr.IsEmpty())
        {
            FEdGraphPinType PinType;
            if (ParseVarType(TypeStr, PinType))
            {
                FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, FName(*Name), PinType);
                Changes.Add(FString::Printf(TEXT("type changed to %s"), *TypeStr));
            }
        }

        if (!DefaultValue.IsEmpty())
        {
            for (FBPVariableDescription& Var : Blueprint->NewVariables)
            {
                if (Var.VarName == FName(*Name))
                {
                    Var.DefaultValue = DefaultValue;
                    Changes.Add(FString::Printf(TEXT("default_value set to %s"), *DefaultValue));
                    break;
                }
            }
        }

        if (!Category.IsEmpty())
        {
            FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(*Name), nullptr, FText::FromString(Category));
            Changes.Add(FString::Printf(TEXT("category set to %s"), *Category));
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("variable"), Name);
        Result->SetArrayField(TEXT("changes"), FMCPJsonHelpers::ArrayFromStrings(Changes));
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleAddFunction
    // -------------------------------------------------------------------------

    FMCPToolResult HandleAddFunction(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint)
    {
        if (!Blueprint) return FMCPToolResult::Error(TEXT("add_function requires a Blueprint target"));

        FString Name;
        Params->TryGetStringField(TEXT("name"), Name);
        if (Name.IsEmpty()) return FMCPToolResult::Error(TEXT("'name' is required"));

        FScopedTransaction Transaction(LOCTEXT("MCPAddFunction", "MCP Add Blueprint Function"));
        Blueprint->Modify();

        UEdGraph* FuncGraph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());

        if (!FuncGraph)
            return FMCPToolResult::Error(TEXT("Failed to create function graph"));

        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FuncGraph, /*bIsUserCreated=*/true, nullptr);

        // Handle pure flag
        bool bPure = false;
        Params->TryGetBoolField(TEXT("pure"), bPure);
        if (bPure)
        {
            TArray<UK2Node_FunctionEntry*> EntryNodes;
            FuncGraph->GetNodesOfClass(EntryNodes);
            if (EntryNodes.Num() > 0)
            {
                EntryNodes[0]->AddExtraFlags(FUNC_BlueprintPure);
            }
        }

        // Add input pins
        const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("inputs"), InputsArr))
        {
            TArray<UK2Node_FunctionEntry*> EntryNodes;
            FuncGraph->GetNodesOfClass(EntryNodes);
            if (EntryNodes.Num() > 0)
            {
                for (const TSharedPtr<FJsonValue>& InputVal : *InputsArr)
                {
                    TSharedPtr<FJsonObject> InputObj = InputVal->AsObject();
                    if (!InputObj.IsValid()) continue;
                    FString PinName, PinType;
                    InputObj->TryGetStringField(TEXT("name"), PinName);
                    InputObj->TryGetStringField(TEXT("type"), PinType);
                    if (PinName.IsEmpty() || PinType.IsEmpty()) continue;
                    FEdGraphPinType Type;
                    if (ParseVarType(PinType, Type))
                    {
                        EntryNodes[0]->CreateUserDefinedPin(FName(*PinName), Type, EGPD_Output);
                    }
                }
            }
        }

        // Add output pins
        const TArray<TSharedPtr<FJsonValue>>* OutputsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("outputs"), OutputsArr))
        {
            TArray<UK2Node_FunctionResult*> ResultNodes;
            FuncGraph->GetNodesOfClass(ResultNodes);
            UK2Node_FunctionResult* ResultNode = nullptr;

            if (ResultNodes.Num() > 0)
            {
                ResultNode = ResultNodes[0];
            }
            else
            {
                FGraphNodeCreator<UK2Node_FunctionResult> Creator(*FuncGraph);
                ResultNode = Creator.CreateNode();
                ResultNode->NodePosX = 400;
                ResultNode->AllocateDefaultPins();
                Creator.Finalize();
            }

            for (const TSharedPtr<FJsonValue>& OutputVal : *OutputsArr)
            {
                TSharedPtr<FJsonObject> OutputObj = OutputVal->AsObject();
                if (!OutputObj.IsValid()) continue;
                FString PinName, PinType;
                OutputObj->TryGetStringField(TEXT("name"), PinName);
                OutputObj->TryGetStringField(TEXT("type"), PinType);
                if (PinName.IsEmpty() || PinType.IsEmpty()) continue;
                FEdGraphPinType Type;
                if (ParseVarType(PinType, Type))
                {
                    ResultNode->CreateUserDefinedPin(FName(*PinName), Type, EGPD_Input);
                }
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), FuncGraph->GetName());
        Result->SetStringField(TEXT("graph"), FuncGraph->GetName());
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleAddComponent
    // -------------------------------------------------------------------------

    FMCPToolResult HandleAddComponent(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint)
    {
        if (!Blueprint) return FMCPToolResult::Error(TEXT("add_component requires a Blueprint target"));
        if (!Blueprint->SimpleConstructionScript)
            return FMCPToolResult::Error(TEXT("Blueprint has no SimpleConstructionScript"));

        TArray<TSharedPtr<FJsonValue>> CompItems;
        const TArray<TSharedPtr<FJsonValue>>* CompsArr = nullptr;
        if (Params->TryGetArrayField(TEXT("components"), CompsArr))
        {
            CompItems = *CompsArr;
        }
        else
        {
            CompItems.Add(MakeShared<FJsonValueObject>(Params));
        }

        TArray<TSharedPtr<FJsonValue>> Results;
        FScopedTransaction Transaction(LOCTEXT("MCPAddComponent", "MCP Add Blueprint Component"));
        Blueprint->Modify();

        USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;

        for (const TSharedPtr<FJsonValue>& Item : CompItems)
        {
            TSharedPtr<FJsonObject> CompParams = Item->AsObject();
            if (!CompParams.IsValid()) continue;

            FString ClassName, CompName, ParentName;
            CompParams->TryGetStringField(TEXT("component_class"), ClassName);
            CompParams->TryGetStringField(TEXT("name"), CompName);
            CompParams->TryGetStringField(TEXT("parent"), ParentName);

            if (ClassName.IsEmpty()) continue;

            UClass* CompClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
            if (!CompClass || !CompClass->IsChildOf(UActorComponent::StaticClass())) continue;

            USCS_Node* NewNode = SCS->CreateNode(CompClass, CompName.IsEmpty() ? NAME_None : FName(*CompName));
            if (!NewNode) continue;

            if (!ParentName.IsEmpty())
            {
                USCS_Node* ParentNode = SCS->FindSCSNode(FName(*ParentName));
                if (ParentNode)
                {
                    ParentNode->AddChildNode(NewNode);
                }
                else
                {
                    SCS->AddNode(NewNode);
                }
            }
            else
            {
                SCS->AddNode(NewNode);
            }

            // Apply properties to ComponentTemplate
            const TSharedPtr<FJsonObject>* PropsObj = nullptr;
            if (CompParams->TryGetObjectField(TEXT("properties"), PropsObj) && NewNode->ComponentTemplate)
            {
                for (const auto& Pair : (*PropsObj)->Values)
                {
                    FProperty* Prop = NewNode->ComponentTemplate->GetClass()->FindPropertyByName(FName(*Pair.Key));
                    if (Prop)
                    {
                        FString ValueStr;
                        if (FMCPJsonHelpers::JsonValueToPropertyString(Pair.Value, ValueStr))
                        {
                            Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(NewNode->ComponentTemplate), NewNode->ComponentTemplate, PPF_None);
                        }
                    }
                }
            }

            TSharedPtr<FJsonObject> CompResult = MakeShared<FJsonObject>();
            CompResult->SetStringField(TEXT("name"), NewNode->GetVariableName().ToString());
            CompResult->SetStringField(TEXT("class"), CompClass->GetName());
            Results.Add(MakeShared<FJsonValueObject>(CompResult));
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("components"), Results);
        Result->SetNumberField(TEXT("count"), Results.Num());
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // -------------------------------------------------------------------------
    // HandleEditComponent
    // -------------------------------------------------------------------------

    FMCPToolResult HandleEditComponent(const TSharedPtr<FJsonObject>& Params, UBlueprint* Blueprint)
    {
        if (!Blueprint) return FMCPToolResult::Error(TEXT("edit_component requires a Blueprint target"));
        if (!Blueprint->SimpleConstructionScript)
            return FMCPToolResult::Error(TEXT("Blueprint has no SimpleConstructionScript"));

        FString Name;
        Params->TryGetStringField(TEXT("name"), Name);
        if (Name.IsEmpty())
        {
            Params->TryGetStringField(TEXT("component_name"), Name);
        }
        if (Name.IsEmpty()) return FMCPToolResult::Error(TEXT("'name' (or 'component_name') is required"));

        USCS_Node* SCSNode = Blueprint->SimpleConstructionScript->FindSCSNode(FName(*Name));
        if (!SCSNode || !SCSNode->ComponentTemplate)
            return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' not found"), *Name));

        const TSharedPtr<FJsonObject>* PropsObj = nullptr;
        if (!Params->TryGetObjectField(TEXT("properties"), PropsObj))
            return FMCPToolResult::Error(TEXT("'properties' is required"));

        FScopedTransaction Transaction(LOCTEXT("MCPEditComponent", "MCP Edit Blueprint Component"));
        Blueprint->Modify();
        SCSNode->ComponentTemplate->Modify();

        TArray<FString> Modified;
        for (const auto& Pair : (*PropsObj)->Values)
        {
            FProperty* Prop = SCSNode->ComponentTemplate->GetClass()->FindPropertyByName(FName(*Pair.Key));
            if (Prop)
            {
                FString ValueStr;
                if (FMCPJsonHelpers::JsonValueToPropertyString(Pair.Value, ValueStr))
                {
                    Prop->ImportText_Direct(*ValueStr, Prop->ContainerPtrToValuePtr<void>(SCSNode->ComponentTemplate), SCSNode->ComponentTemplate, PPF_None);
                    Modified.Add(Pair.Key);
                }
            }
        }

        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FMCPGraphHelpers::RefreshBlueprintEditor(Blueprint);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("component"), Name);
        Result->SetArrayField(TEXT("modified"), FMCPJsonHelpers::ArrayFromStrings(Modified));
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

} // anonymous namespace

// =============================================================================
// FMCPTool_Graph
// =============================================================================

FMCPToolInfo FMCPTool_Graph::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("graph");
    Info.Description = TEXT("Edit Blueprint graphs and Material node graphs: add/edit nodes, connect/disconnect pins, manage variables, functions, and components");

    FString PropNames;
    for (const auto& Entry : FMCPGraphHelpers::KnownMaterialProperties())
    {
        if (PropNames.Len() > 0) PropNames += TEXT(", ");
        PropNames += Entry.Name;
    }
    FString DestDesc = FString::Printf(
        TEXT("[connect|disconnect] Input pin: {\"node\":\"GUID\",\"pin\":\"PinName\"}. Material output (no GUID): {\"property\":\"PropName\"} — or use alias dest.node (Output/Result/Material) with dest.pin as property name. Valid properties: %s"),
        *PropNames);

    Info.Parameters  = {
        { TEXT("action"),          TEXT("Values: add_node|edit_node|connect|disconnect|add_variable|edit_variable|add_function|add_component|edit_component|compile|help"), TEXT("string"), true  },
        { TEXT("target"),          TEXT("Blueprint or Material asset path"),                                                      TEXT("string"), true  },
        { TEXT("graph"),           TEXT("[add_node|edit_node|connect|disconnect] Graph name (BP only). Default: EventGraph. Alias: graph_name"),    TEXT("string"), false },
        { TEXT("node_class"),      TEXT("[add_node] BP node type: CallFunction (requires function param), Event, CustomEvent, VariableGet, VariableSet, Branch, Sequence, Self, DynamicCast, SpawnActor, MakeArray, Select, SwitchOnInt, SwitchOnString, SwitchOnEnum, MacroInstance, ForEachLoop. For Materials: expression class name e.g. Multiply, Add, Lerp, ScalarParameter, VectorParameter, TextureCoordinate, Constant"), TEXT("string"), false },
        { TEXT("function"),        TEXT("[add_node] Function name for CallFunction nodes"),                                        TEXT("string"), false },
        { TEXT("function_owner"),  TEXT("[add_node] Class owning the function (e.g. KismetSystemLibrary); also used as cast target class for DynamicCast"), TEXT("string"), false },
        { TEXT("event_name"),      TEXT("[add_node] Event name for Event/CustomEvent nodes; macro name for MacroInstance"),        TEXT("string"), false },
        { TEXT("variable_name"),   TEXT("[add_node] Variable name for VariableGet/VariableSet nodes"),                            TEXT("string"), false },
        { TEXT("pos_x"),           TEXT("[add_node|edit_node] Node X position"),                                                    TEXT("integer"), false },
        { TEXT("pos_y"),           TEXT("[add_node|edit_node] Node Y position"),                                                    TEXT("integer"), false },
        { TEXT("nodes"),           TEXT("[add_node] Batch: array of node objects. Each: {node_class, function?, function_owner?, event_name?, variable_name?, pos_x?, pos_y?}"), TEXT("array"),  false, TEXT("object") },
        { TEXT("node"),            TEXT("[edit_node] Node GUID to edit"),                                                         TEXT("string"), false },
        { TEXT("properties"),      TEXT("[edit_node|edit_component] Reflection properties. Format: {\"PropName\":value}"),          TEXT("object"), false },
        { TEXT("pin_defaults"),    TEXT("[edit_node] Pin default values. Format: {\"PinName\":\"value\"}"),                          TEXT("object"), false },
        { TEXT("edits"),           TEXT("[edit_node] Batch: array of edit objects. Each: {node (GUID), properties?, pin_defaults?, pos_x?, pos_y?}"), TEXT("array"),  false, TEXT("object") },
        { TEXT("source"),          TEXT("[connect|disconnect] Output pin. Format: {\"node\":\"GUID\",\"pin\":\"PinName\"}. Use inspect(target='Path::GUID',type='pins') for names"), TEXT("object"), false },
        { TEXT("dest"),            DestDesc, TEXT("object"), false },
        { TEXT("connections"),     TEXT("[connect|disconnect] Batch array. Each: {source:{node,pin}, dest:{node,pin}} or {source:{node,pin}, dest:{property:\"PropName\"}} for material output. BP example: [{\"source\":{\"node\":\"AA\",\"pin\":\"ReturnValue\"},\"dest\":{\"node\":\"AQ\",\"pin\":\"A\"}}], Material: [{\"source\":{\"node\":\"AB\",\"pin\":\"\"},\"dest\":{\"property\":\"BaseColor\"}}]"), TEXT("array"),  false, TEXT("object") },
        { TEXT("name"),            TEXT("[add_variable/edit_variable/add_function/add_component/edit_component] Name (alias: var_name for add_variable)"),           TEXT("string"), false },
        { TEXT("var_type"),        TEXT("[add_variable|edit_variable] Values: float|int|bool|string|byte|name|text|Vector|Rotator|Transform|Object:ClassName"), TEXT("string"), false },
        { TEXT("default_value"),   TEXT("[add_variable/edit_variable] Default value as string"),                                  TEXT("string"), false },
        { TEXT("category"),        TEXT("[add_variable/edit_variable] Variable category"),                                        TEXT("string"), false },
        { TEXT("variables"),       TEXT("[add_variable] Batch: array of variable objects. Each: {name, var_type, default_value?, category?}"), TEXT("array"),  false, TEXT("object") },
        { TEXT("new_name"),        TEXT("[edit_variable] New name for rename"),                                                   TEXT("string"), false },
        { TEXT("inputs"),          TEXT("[add_function] Input pins. Format: [{\"name\":\"x\",\"type\":\"float\"}]"),                          TEXT("array"),  false, TEXT("object") },
        { TEXT("outputs"),         TEXT("[add_function] Output pins. Format: [{\"name\":\"result\",\"type\":\"bool\"}]"),                  TEXT("array"),  false, TEXT("object") },
        { TEXT("pure"),            TEXT("[add_function] Mark as pure (no exec pins). Default: false"),                            TEXT("boolean"),false },
        { TEXT("component_class"), TEXT("[add_component] Component class name (e.g. StaticMeshComponent)"),                      TEXT("string"), false },
        { TEXT("parent"),          TEXT("[add_component] Parent component name for hierarchy"),                                   TEXT("string"), false },
        { TEXT("components"),      TEXT("[add_component] Batch: array of component objects. Each: {component_class, name?, parent?, properties?}"), TEXT("array"),  false, TEXT("object") },
        { TEXT("help"),            TEXT("Pass help=true for overview, help='action_name' for detailed parameter info"), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Graph::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sGraphHelp, HelpResult))
        return HelpResult;

    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Action, Target;
        if (!Params->TryGetStringField(TEXT("action"), Action))
            return FMCPToolResult::Error(TEXT("'action' is required"));

        // Backward compat: action=help redirects to the help system
        if (Action.Equals(TEXT("help"), ESearchCase::IgnoreCase))
        {
            return MCPToolHelp::FormatHelp(sGraphHelp, TEXT(""));
        }

        if (!Params->TryGetStringField(TEXT("target"), Target))
            return FMCPToolResult::Error(TEXT("'target' is required"));

        FString ResolveError;
        UObject* Asset = FMCPObjectResolver::ResolveAsset(Target, ResolveError);
        if (!Asset)
            return FMCPToolResult::Error(ResolveError);

        UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
        UMaterial*  Material  = Cast<UMaterial>(Asset);

        if (!Blueprint && !Material)
            return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a Blueprint or Material"), *Target));

        if (Action.Equals(TEXT("add_node"),       ESearchCase::IgnoreCase)) return HandleAddNode(Params, Blueprint, Material);
        if (Action.Equals(TEXT("edit_node"),      ESearchCase::IgnoreCase)) return HandleEditNode(Params, Blueprint, Material);
        if (Action.Equals(TEXT("connect"),        ESearchCase::IgnoreCase)) return HandleConnect(Params, Blueprint, Material);
        if (Action.Equals(TEXT("disconnect"),     ESearchCase::IgnoreCase)) return HandleDisconnect(Params, Blueprint, Material);
        if (Action.Equals(TEXT("add_variable"),   ESearchCase::IgnoreCase)) return HandleAddVariable(Params, Blueprint);
        if (Action.Equals(TEXT("edit_variable"),  ESearchCase::IgnoreCase)) return HandleEditVariable(Params, Blueprint);
        if (Action.Equals(TEXT("add_function"),   ESearchCase::IgnoreCase)) return HandleAddFunction(Params, Blueprint);
        if (Action.Equals(TEXT("add_component"),  ESearchCase::IgnoreCase)) return HandleAddComponent(Params, Blueprint);
        if (Action.Equals(TEXT("edit_component"), ESearchCase::IgnoreCase)) return HandleEditComponent(Params, Blueprint);
        if (Action.Equals(TEXT("compile"),        ESearchCase::IgnoreCase)) return HandleCompile(Blueprint, Material);

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown action: '%s'. Valid actions: %s"),
            *Action, *GetValidActionsString()));
    });
}

#undef LOCTEXT_NAMESPACE
