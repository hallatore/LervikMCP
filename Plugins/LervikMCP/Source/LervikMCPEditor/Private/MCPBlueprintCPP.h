#pragma once

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Composite.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"

struct FMCPBlueprintCPP
{
	static FString GenerateCPP(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP) return TEXT("// null blueprint");

		FString Out;

		// Blueprint header (always once at top)
		FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("Unknown");
		Out += FString::Printf(TEXT("// Blueprint: %s (Parent: %s)\n"), *BP->GetName(), *ParentName);
		Out += TEXT("// [<ID>] (<pos_x>,<pos_y>) \u2014 each node has a compact ID and position\n");

		// Variables (blueprint-level, before any graph)
		if (BP->NewVariables.Num() > 0)
		{
			Out += TEXT("//\n// Variables:\n");
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{
				FString TypeStr = PinTypeToString(Var.VarType);
				FString DefaultStr;
				if (!Var.DefaultValue.IsEmpty())
					DefaultStr = FString::Printf(TEXT(" = %s"), *FormatDefaultValue(Var.VarType, Var.DefaultValue));
				else
				{
					FString CDOStr = GetCDODefaultString(BP, Var.VarName);
					if (!CDOStr.IsEmpty())
						DefaultStr = FString::Printf(TEXT(" = %s"), *FormatDefaultValue(Var.VarType, CDOStr));
				}
				Out += FString::Printf(TEXT("//   %s %s%s;\n"), *TypeStr, *Var.VarName.ToString(), *DefaultStr);
			}
		}
		Out += TEXT("\n");

		// Collect graphs to render
		TArray<UEdGraph*> Graphs;
		if (!GraphName.IsEmpty())
		{
			// Single-graph mode (backward compat)
			UEdGraph* Graph = FMCPGraphHelpers::FindGraphByName(BP, GraphName);
			if (!Graph)
			{
				FString Available;
				for (UEdGraph* G : BP->UbergraphPages)
					if (G) Available += G->GetName() + TEXT(", ");
				for (UEdGraph* G : BP->FunctionGraphs)
					if (G) Available += G->GetName() + TEXT(", ");
				for (UEdGraph* G : BP->MacroGraphs)
					if (G) Available += G->GetName() + TEXT(", ");
				Available.TrimEndInline();
				if (Available.EndsWith(TEXT(",")))
					Available.LeftChopInline(1);
				return FString::Printf(TEXT("// Error: Graph '%s' not found. Available: %s"), *GraphName, *Available);
			}
			Graphs.Add(Graph);
		}
		else
		{
			// All-graphs mode: UbergraphPages, FunctionGraphs, MacroGraphs
			for (UEdGraph* G : BP->UbergraphPages)
				if (G) Graphs.Add(G);
			for (UEdGraph* G : BP->FunctionGraphs)
				if (G) Graphs.Add(G);
			for (UEdGraph* G : BP->MacroGraphs)
				if (G) Graphs.Add(G);
		}

		// Emit each graph
		FString AssetPath = BP->GetPathName();
		for (UEdGraph* Graph : Graphs)
		{
			Out += FString::Printf(TEXT("// Graph: %s (%s::%s)\n"), *Graph->GetName(), *AssetPath, *Graph->GetName());

			// Emit local variables for function graphs (from UK2Node_FunctionEntry)
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (auto* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
				{
					if (FuncEntry->LocalVariables.Num() > 0)
					{
						Out += TEXT("// Local Variables:\n");
						for (const FBPVariableDescription& Var : FuncEntry->LocalVariables)
						{
							FString TypeStr = PinTypeToString(Var.VarType);
							FString DefaultStr;
							if (!Var.DefaultValue.IsEmpty())
								DefaultStr = FString::Printf(TEXT(" = %s"), *FormatDefaultValue(Var.VarType, Var.DefaultValue));
							Out += FString::Printf(TEXT("//   %s %s%s;\n"), *TypeStr, *Var.VarName.ToString(), *DefaultStr);
						}
					}
					break;
				}
			}

			Out += TEXT("\n");
			Out += GenerateGraphBody(Graph);
		}

		return Out;
	}

private:
	// ── per-graph body ──────────────────────────────────────────────────

	static FString GenerateGraphBody(UEdGraph* Graph)
	{
		FString Out;

		// Find entry nodes and walk exec chains
		TArray<UEdGraphNode*> EntryNodes = FindEntryNodes(Graph);
		TSet<UEdGraphNode*> AllVisited;

		struct FExecChain { UEdGraphNode* Entry; TArray<UEdGraphNode*> Nodes; };
		TArray<FExecChain> Chains;

		for (UEdGraphNode* Entry : EntryNodes)
		{
			FExecChain Chain;
			Chain.Entry = Entry;
			WalkExecChain(Entry, Chain.Nodes, AllVisited);
			Chains.Add(MoveTemp(Chain));
		}

		// Collect pure data dependencies for all exec-chain nodes
		TSet<UEdGraphNode*> AllDataNodes;
		for (UEdGraphNode* Node : AllVisited)
			CollectDataDependencies(Node, AllDataNodes);

		// Build var name map for all nodes
		TArray<UEdGraphNode*> AllNodes;
		for (auto& Chain : Chains)
			AllNodes.Append(Chain.Nodes);
		for (UEdGraphNode* DataNode : AllDataNodes)
		{
			if (!AllVisited.Contains(DataNode))
				AllNodes.Add(DataNode);
		}
		TMap<UEdGraphNode*, FString> VarNames = BuildVarNameMap(AllNodes);

		// Merge data nodes into visited for dangling detection
		TSet<UEdGraphNode*> AllReachable = AllVisited;
		AllReachable.Append(AllDataNodes);

		// Emit each entry point with structured code
		TSet<UEdGraphNode*> EmitVisited;
		TSet<UEdGraphNode*> EmittedPure;

		for (UEdGraphNode* Entry : EntryNodes)
		{
			Out += EmitExecFrom(Entry, VarNames, 0, EmitVisited, EmittedPure);
		}

		// Dangling nodes
		TArray<UEdGraphNode*> Dangling = CollectDanglingNodes(Graph, AllReachable);
		if (Dangling.Num() > 0)
		{
			Out += TEXT("// --- Dangling Nodes ---\n");
			for (UEdGraphNode* Node : Dangling)
			{
				Out += FString::Printf(TEXT("// [Unconnected] %s %s\n"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
					*TrailingComment(Node));
			}
		}

		return Out;
	}

	// ── helpers ──────────────────────────────────────────────────────────

	static bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	static bool HasExecPins(const UEdGraphNode* Node)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin)) return true;
		}
		return false;
	}

	static FString IndentStr(int32 Level)
	{
		FString S;
		for (int32 i = 0; i < Level; ++i)
			S += TEXT("    ");
		return S;
	}

	// ── type resolution ─────────────────────────────────────────────────

	static FString ResolveInnerTypeName(FName Category, FName SubCategory, UObject* SubCategoryObject)
	{
		if (Category == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
		if (Category == UEdGraphSchema_K2::PC_Byte) return TEXT("uint8");
		if (Category == UEdGraphSchema_K2::PC_Int) return TEXT("int32");
		if (Category == UEdGraphSchema_K2::PC_Int64) return TEXT("int64");
		if (Category == UEdGraphSchema_K2::PC_Real)
			return SubCategory == UEdGraphSchema_K2::PC_Double ? TEXT("double") : TEXT("float");
		if (Category == UEdGraphSchema_K2::PC_Float) return TEXT("float");
		if (Category == UEdGraphSchema_K2::PC_Double) return TEXT("double");
		if (Category == UEdGraphSchema_K2::PC_Name) return TEXT("FName");
		if (Category == UEdGraphSchema_K2::PC_String) return TEXT("FString");
		if (Category == UEdGraphSchema_K2::PC_Text) return TEXT("FText");
		if (Category == UEdGraphSchema_K2::PC_Object)
		{
			if (UClass* Cls = Cast<UClass>(SubCategoryObject))
				return Cls->GetPrefixCPP() + Cls->GetName() + TEXT("*");
			return TEXT("UObject*");
		}
		if (Category == UEdGraphSchema_K2::PC_Class)
		{
			FString Inner = SubCategoryObject ? SubCategoryObject->GetName() : TEXT("UObject");
			return FString::Printf(TEXT("TSubclassOf<%s>"), *Inner);
		}
		if (Category == UEdGraphSchema_K2::PC_SoftObject)
		{
			FString Inner = SubCategoryObject ? SubCategoryObject->GetName() : TEXT("UObject");
			return FString::Printf(TEXT("TSoftObjectPtr<%s>"), *Inner);
		}
		if (Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			FString Inner = SubCategoryObject ? SubCategoryObject->GetName() : TEXT("UObject");
			return FString::Printf(TEXT("TSoftClassPtr<%s>"), *Inner);
		}
		if (Category == UEdGraphSchema_K2::PC_Interface)
		{
			FString Inner = SubCategoryObject ? SubCategoryObject->GetName() : TEXT("Interface");
			return FString::Printf(TEXT("TScriptInterface<I%s>"), *Inner);
		}
		if (Category == UEdGraphSchema_K2::PC_Struct)
			return SubCategoryObject ? SubCategoryObject->GetName() : TEXT("auto");
		if (Category == UEdGraphSchema_K2::PC_Enum)
			return SubCategoryObject ? SubCategoryObject->GetName() : TEXT("auto");
		if (Category == UEdGraphSchema_K2::PC_Delegate) return TEXT("FDelegate");
		if (Category == UEdGraphSchema_K2::PC_MCDelegate) return TEXT("FMulticastDelegate");
		if (Category == UEdGraphSchema_K2::PC_Exec) return TEXT("");
		if (Category == UEdGraphSchema_K2::PC_Wildcard) return TEXT("auto");
		if (Category == UEdGraphSchema_K2::PC_FieldPath) return TEXT("TFieldPath<FProperty>");
		return TEXT("auto");
	}

	static FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		FString Inner = ResolveInnerTypeName(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
		if (Inner.IsEmpty()) return TEXT("");

		// Container wrapping
		if (PinType.IsArray())
			Inner = FString::Printf(TEXT("TArray<%s>"), *Inner);
		else if (PinType.IsSet())
			Inner = FString::Printf(TEXT("TSet<%s>"), *Inner);
		else if (PinType.IsMap())
		{
			const FEdGraphTerminalType& VT = PinType.PinValueType;
			FString ValType = ResolveInnerTypeName(VT.TerminalCategory, VT.TerminalSubCategory, VT.TerminalSubCategoryObject.Get());
			Inner = FString::Printf(TEXT("TMap<%s, %s>"), *Inner, *ValType);
		}

		if (PinType.bIsConst)
			Inner = TEXT("const ") + Inner;
		if (PinType.bIsReference)
			Inner += TEXT("&");

		return Inner;
	}

	// ── default value formatting ────────────────────────────────────────

	static FString ParseStructField(const FString& Src, const FString& Key)
	{
		int32 Idx = Src.Find(Key + TEXT("="));
		if (Idx == INDEX_NONE) return TEXT("0");
		int32 Start = Idx + Key.Len() + 1; // skip "Key="
		int32 End = Start;
		while (End < Src.Len() && Src[End] != TEXT(' ') && Src[End] != TEXT(',') && Src[End] != TEXT(')')) ++End;
		return Src.Mid(Start, End - Start);
	}

	static FString FormatDefaultValue(const FEdGraphPinType& PinType, const FString& Default)
	{
		if (Default.IsEmpty()) return TEXT("/*unset*/");

		// Container types checked first — containers still carry the inner PinCategory
		if (PinType.IsArray() || PinType.IsSet())
		{
			FString Inner = Default;
			if (Inner.StartsWith(TEXT("(")) && Inner.EndsWith(TEXT(")")))
				Inner = Inner.Mid(1, Inner.Len() - 2);
			if (Inner.IsEmpty()) return TEXT("{}");
			return FString::Printf(TEXT("{ %s }"), *Inner);
		}
		if (PinType.IsMap())
		{
			if (Default.IsEmpty() || Default == TEXT("()")) return TEXT("{}");
			return FString::Printf(TEXT("{ %s }"), *Default);
		}

		FName Cat = PinType.PinCategory;
		if (Cat == UEdGraphSchema_K2::PC_Boolean)
			return Default.Equals(TEXT("true"), ESearchCase::IgnoreCase) ? TEXT("true") : TEXT("false");
		if (Cat == UEdGraphSchema_K2::PC_String)
			return FString::Printf(TEXT("TEXT(\"%s\")"), *Default);
		if (Cat == UEdGraphSchema_K2::PC_Text)
			return FString::Printf(TEXT("INVTEXT(\"%s\")"), *Default);
		if (Cat == UEdGraphSchema_K2::PC_Name)
			return FString::Printf(TEXT("FName(TEXT(\"%s\"))"), *Default);
		if (Cat == UEdGraphSchema_K2::PC_Int || Cat == UEdGraphSchema_K2::PC_Int64
			|| Cat == UEdGraphSchema_K2::PC_Float || Cat == UEdGraphSchema_K2::PC_Double
			|| Cat == UEdGraphSchema_K2::PC_Real || Cat == UEdGraphSchema_K2::PC_Byte)
			return Default;
		if (Cat == UEdGraphSchema_K2::PC_Object || Cat == UEdGraphSchema_K2::PC_Class
			|| Cat == UEdGraphSchema_K2::PC_SoftObject || Cat == UEdGraphSchema_K2::PC_SoftClass)
			return TEXT("nullptr");
		if (Cat == UEdGraphSchema_K2::PC_Enum)
		{
			FString EnumName = PinType.PinSubCategoryObject.IsValid() ? PinType.PinSubCategoryObject->GetName() : TEXT("");
			if (!EnumName.IsEmpty() && !Default.Contains(TEXT("::")))
				return FString::Printf(TEXT("%s::%s"), *EnumName, *Default);
			return Default;
		}
		if (Cat == UEdGraphSchema_K2::PC_Struct && PinType.PinSubCategoryObject.IsValid())
		{
			FString StructName = PinType.PinSubCategoryObject->GetName();
			if (StructName == TEXT("Vector") || StructName == TEXT("Vector_NetQuantize"))
				return FString::Printf(TEXT("FVector(%s, %s, %s)"),
					*ParseStructField(Default, TEXT("X")),
					*ParseStructField(Default, TEXT("Y")),
					*ParseStructField(Default, TEXT("Z")));
			if (StructName == TEXT("Rotator"))
				return FString::Printf(TEXT("FRotator(%s, %s, %s)"),
					*ParseStructField(Default, TEXT("P")),
					*ParseStructField(Default, TEXT("Y")),
					*ParseStructField(Default, TEXT("R")));
			if (StructName == TEXT("LinearColor"))
				return FString::Printf(TEXT("FLinearColor(%s, %s, %s, %s)"),
					*ParseStructField(Default, TEXT("R")),
					*ParseStructField(Default, TEXT("G")),
					*ParseStructField(Default, TEXT("B")),
					*ParseStructField(Default, TEXT("A")));
			if (StructName == TEXT("Vector2D"))
				return FString::Printf(TEXT("FVector2D(%s, %s)"),
					*ParseStructField(Default, TEXT("X")),
					*ParseStructField(Default, TEXT("Y")));
			if (StructName == TEXT("Transform"))
				return Default == TEXT("0,0,0|0,0,0|1,1,1") ? TEXT("FTransform::Identity") : FString::Printf(TEXT("FTransform(/*%s*/)"), *Default);
		}
		return Default.IsEmpty() ? TEXT("{}") : Default;
	}

public:
	static FString GetCDODefaultString(UBlueprint* BP, FName VarName)
	{
		if (!BP || !BP->GeneratedClass) return FString();
		UObject* CDO = BP->GeneratedClass->GetDefaultObject(false);
		if (!CDO) return FString();

		FProperty* Prop = BP->GeneratedClass->FindPropertyByName(VarName);
		if (!Prop) return FString();

		// Compare against parent CDO — only show if value differs from parent default
		UObject* ParentCDO = BP->ParentClass ? BP->ParentClass->GetDefaultObject(false) : nullptr;
		if (ParentCDO)
		{
			FProperty* ParentProp = BP->ParentClass->FindPropertyByName(VarName);
			if (ParentProp)
			{
				const void* CDOVal = Prop->ContainerPtrToValuePtr<void>(CDO);
				const void* ParentVal = ParentProp->ContainerPtrToValuePtr<void>(ParentCDO);
				if (Prop->Identical(CDOVal, ParentVal))
					return FString();
			}
		}

		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, CDO, PPF_None);
		return ValueStr;
	}

private:
	static UEdGraphPin* FollowKnots(UEdGraphPin* Pin)
	{
		TSet<UEdGraphNode*> Visited;
		while (Pin)
		{
			UEdGraphNode* Node = Pin->GetOwningNode();
			if (!Node || !Cast<UK2Node_Knot>(Node)) break;
			if (Visited.Contains(Node)) break;
			Visited.Add(Node);
			UEdGraphPin* InputPin = nullptr;
			for (UEdGraphPin* P : Node->Pins)
			{
				if (P->Direction == EGPD_Input) { InputPin = P; break; }
			}
			if (!InputPin || InputPin->LinkedTo.Num() == 0) break;
			Pin = InputPin->LinkedTo[0];
		}
		return Pin;
	}

	static UEdGraphNode* GetLinkedNode(UEdGraphPin* Pin)
	{
		if (Pin && Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
			return Pin->LinkedTo[0]->GetOwningNode();
		return nullptr;
	}

	static FString InputRef(UEdGraphPin* Pin, const TMap<UEdGraphNode*, FString>& VarNames)
	{
		if (!Pin) return TEXT("???");

		if (Pin->LinkedTo.Num() > 0)
		{
			UEdGraphPin* SourcePin = FollowKnots(Pin->LinkedTo[0]);
			if (!SourcePin) return TEXT("???");
			UEdGraphNode* SourceNode = SourcePin->GetOwningNode();
			const FString* VN = VarNames.Find(SourceNode);
			if (!VN) return TEXT("???");

			// Multi-output pin disambiguation
			FString PinName = SourcePin->PinName.ToString();
			if (PinName != TEXT("ReturnValue") && !IsExecPin(SourcePin))
			{
				int32 DataOutputCount = 0;
				for (UEdGraphPin* P : SourceNode->Pins)
					if (P->Direction == EGPD_Output && !IsExecPin(P)) DataOutputCount++;
				if (DataOutputCount > 1)
					return FString::Printf(TEXT("%s.%s"), **VN, *SanitizeName(PinName));
			}
			return *VN;
		}

		// Unconnected pin — format default value
		FString Default = Pin->DefaultValue;
		if (Default.IsEmpty())
			Default = Pin->AutogeneratedDefaultValue;

		FString Formatted = FormatDefaultValue(Pin->PinType, Default);

		// Pin name annotation for non-trivial defaults
		FString PinLabel = Pin->PinName.ToString();
		if (Formatted != TEXT("/*unset*/") && !PinLabel.IsEmpty()
			&& PinLabel != Formatted && !PinLabel.IsNumeric())
		{
			Formatted = FString::Printf(TEXT("%s /*%s*/"), *Formatted, *PinLabel);
		}
		return Formatted;
	}

	// ── node emission ──────────────────────────────────────────────────

	static FString EmitCallFunction(UK2Node_CallFunction* Call,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(Call);
		FString FuncName = Call->FunctionReference.GetMemberName().ToString();
		FString Comment = TrailingComment(Call);

		FString Args;
		for (UEdGraphPin* Pin : Call->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (IsExecPin(Pin)) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;
			if (!Args.IsEmpty()) Args += TEXT(", ");
			Args += InputRef(Pin, VarNames);
		}

		FString Target;
		UEdGraphPin* SelfPin = Call->FindPin(UEdGraphSchema_K2::PN_Self);
		if (SelfPin && SelfPin->LinkedTo.Num() > 0)
			Target = InputRef(SelfPin, VarNames) + TEXT("->");

		bool bHasOutputData = false;
		for (UEdGraphPin* Pin : Call->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{ bHasOutputData = true; break; }
		}

		FString Line;
		if (bHasOutputData)
		{
			// Find first non-exec output pin for type
			FString TypeStr = TEXT("auto");
			for (UEdGraphPin* Pin : Call->Pins)
			{
				if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
				{ TypeStr = PinTypeToString(Pin->PinType); break; }
			}
			Line = FString::Printf(TEXT("%s %s = %s%s(%s);"), *TypeStr, *VN, *Target, *FuncName, *Args);
		}
		else
			Line = FString::Printf(TEXT("%s%s(%s);"), *Target, *FuncName, *Args);

		return IndentStr(Indent) + Line + TEXT(" ") + Comment + TEXT("\n");
	}

	static FString EmitVariableGet(UK2Node_VariableGet* VarGet,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(VarGet);
		FString VarName = VarGet->GetVarName().ToString();
		FString Comment = TrailingComment(VarGet);
		FString TypeStr = TEXT("auto");
		for (UEdGraphPin* Pin : VarGet->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{ TypeStr = PinTypeToString(Pin->PinType); break; }
		}
		return IndentStr(Indent) + FString::Printf(TEXT("%s %s = %s; %s\n"), *TypeStr, *VN, *VarName, *Comment);
	}

	static FString EmitVariableSet(UK2Node_VariableSet* VarSet,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VarName = VarSet->GetVarName().ToString();
		FString Comment = TrailingComment(VarSet);

		FString Value = TEXT("/*unset*/");
		for (UEdGraphPin* Pin : VarSet->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (IsExecPin(Pin)) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;
			Value = InputRef(Pin, VarNames);
			break;
		}

		return IndentStr(Indent) + FString::Printf(TEXT("%s = %s; %s\n"), *VarName, *Value, *Comment);
	}

	static FString EmitSelf(UEdGraphNode* Node,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(Node);
		FString Comment = TrailingComment(Node);
		return IndentStr(Indent) + FString::Printf(TEXT("auto %s = this; %s\n"), *VN, *Comment);
	}

	static FString EmitFallback(UEdGraphNode* Node,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(Node);
		FString Title = SanitizeName(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		FString Comment = TrailingComment(Node);
		FString ClassName = Node->GetClass()->GetName();

		FString Args;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;
			if (!Args.IsEmpty()) Args += TEXT(", ");
			Args += InputRef(Pin, VarNames);
		}

		bool bHasOutputData = false;
		FString TypeStr = TEXT("auto");
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{ bHasOutputData = true; TypeStr = PinTypeToString(Pin->PinType); break; }
		}

		// Add class hint for unknown node types
		FString ClassHint = FString::Printf(TEXT("/* %s */ "), *ClassName);
		if (bHasOutputData)
			return IndentStr(Indent) + FString::Printf(TEXT("%s %s = %s%s(%s); %s\n"), *TypeStr, *VN, *ClassHint, *Title, *Args, *Comment);
		return IndentStr(Indent) + FString::Printf(TEXT("%s%s(%s); %s\n"), *ClassHint, *Title, *Args, *Comment);
	}

	static FString EmitNode(UEdGraphNode* Node, const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		if (!Node) return TEXT("");
		if (Cast<UK2Node_Knot>(Node)) return TEXT("");

		if (auto* Call = Cast<UK2Node_CallFunction>(Node))
			return EmitCallFunction(Call, VarNames, Indent);
		if (auto* VarGet = Cast<UK2Node_VariableGet>(Node))
			return EmitVariableGet(VarGet, VarNames, Indent);
		if (auto* VarSet = Cast<UK2Node_VariableSet>(Node))
			return EmitVariableSet(VarSet, VarNames, Indent);
		if (Cast<UK2Node_Self>(Node))
			return EmitSelf(Node, VarNames, Indent);
		if (auto* SpawnNode = Cast<UK2Node_SpawnActorFromClass>(Node))
			return EmitSpawnActor(SpawnNode, VarNames, Indent);
		if (auto* MakeArr = Cast<UK2Node_MakeArray>(Node))
			return EmitMakeArray(MakeArr, VarNames, Indent);
		if (auto* SelectNode = Cast<UK2Node_Select>(Node))
			return EmitSelect(SelectNode, VarNames, Indent);
		if (auto* CastNode = Cast<UK2Node_DynamicCast>(Node))
		{
			if (CastNode->IsNodePure())
				return EmitPureCast(CastNode, VarNames, Indent);
		}

		return EmitFallback(Node, VarNames, Indent);
	}

	// ── pure data dependency emission ──────────────────────────────────

	static void CollectPureDepsOrdered(UEdGraphNode* Node,
		TArray<UEdGraphNode*>& OutDeps,
		const TSet<UEdGraphNode*>& AlreadyEmitted,
		TSet<UEdGraphNode*>& InProgress)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				UEdGraphPin* SourcePin = FollowKnots(LinkedPin);
				if (!SourcePin) continue;
				UEdGraphNode* SourceNode = SourcePin->GetOwningNode();
				if (!SourceNode || AlreadyEmitted.Contains(SourceNode)) continue;
				if (HasExecPins(SourceNode)) continue;
				if (OutDeps.Contains(SourceNode) || InProgress.Contains(SourceNode)) continue;

				InProgress.Add(SourceNode);
				CollectPureDepsOrdered(SourceNode, OutDeps, AlreadyEmitted, InProgress);

				if (!OutDeps.Contains(SourceNode))
					OutDeps.Add(SourceNode);
			}
		}
	}

	static FString EmitPureDeps(UEdGraphNode* Node,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& AlreadyEmitted)
	{
		TArray<UEdGraphNode*> PureDeps;
		TSet<UEdGraphNode*> InProgress;
		CollectPureDepsOrdered(Node, PureDeps, AlreadyEmitted, InProgress);

		FString Out;
		for (UEdGraphNode* PureNode : PureDeps)
		{
			AlreadyEmitted.Add(PureNode);
			Out += EmitNode(PureNode, VarNames, Indent);
		}
		return Out;
	}

	// ── exec chain walking ─────────────────────────────────────────────

	static UEdGraphNode* FollowExecOutput(UEdGraphNode* Node)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
			if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
				return Pin->LinkedTo[0]->GetOwningNode();
		}
		return nullptr;
	}

	static void CollectExecReachable(UEdGraphNode* Node, TSet<UEdGraphNode*>& OutSet)
	{
		if (!Node || OutSet.Contains(Node)) return;
		OutSet.Add(Node);
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
					CollectExecReachable(Linked->GetOwningNode(), OutSet);
			}
		}
	}

	static UEdGraphNode* FindConvergencePoint(UEdGraphNode* ThenStart, UEdGraphNode* ElseStart)
	{
		if (!ThenStart || !ElseStart) return nullptr;

		TSet<UEdGraphNode*> ThenReachable;
		CollectExecReachable(ThenStart, ThenReachable);

		TArray<UEdGraphNode*> Queue;
		TSet<UEdGraphNode*> ElseVisited;
		Queue.Add(ElseStart);
		while (Queue.Num() > 0)
		{
			UEdGraphNode* Node = Queue[0];
			Queue.RemoveAt(0);
			if (!Node || ElseVisited.Contains(Node)) continue;
			ElseVisited.Add(Node);

			if (ThenReachable.Contains(Node))
				return Node;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsExecPin(Pin) && Pin->Direction == EGPD_Output)
				{
					for (UEdGraphPin* Linked : Pin->LinkedTo)
						if (Linked) Queue.Add(Linked->GetOwningNode());
				}
			}
		}
		return nullptr;
	}

	static UEdGraphNode* FindMultiBranchConvergence(const TArray<UEdGraphNode*>& BranchStarts)
	{
		if (BranchStarts.Num() < 2) return nullptr;

		// Collect reachable sets for each branch
		TArray<TSet<UEdGraphNode*>> ReachableSets;
		for (UEdGraphNode* Start : BranchStarts)
		{
			TSet<UEdGraphNode*> Reachable;
			if (Start) CollectExecReachable(Start, Reachable);
			ReachableSets.Add(MoveTemp(Reachable));
		}

		// BFS from first branch, find first node reachable from ALL branches
		TArray<UEdGraphNode*> Queue;
		TSet<UEdGraphNode*> BFSVisited;
		if (BranchStarts[0]) Queue.Add(BranchStarts[0]);
		while (Queue.Num() > 0)
		{
			UEdGraphNode* Node = Queue[0];
			Queue.RemoveAt(0);
			if (!Node || BFSVisited.Contains(Node)) continue;
			BFSVisited.Add(Node);

			bool bInAll = true;
			for (int32 i = 1; i < ReachableSets.Num(); ++i)
			{
				if (!ReachableSets[i].Contains(Node)) { bInAll = false; break; }
			}
			if (bInAll) return Node;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (IsExecPin(Pin) && Pin->Direction == EGPD_Output)
				{
					for (UEdGraphPin* Linked : Pin->LinkedTo)
						if (Linked) Queue.Add(Linked->GetOwningNode());
				}
			}
		}
		return nullptr;
	}

	static FString EmitEvent(UK2Node_Event* Event,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString EventName = Event->GetNodeTitle(ENodeTitleType::ListView).ToString();
		EventName = SanitizeName(EventName);

		FString Params;
		for (UEdGraphPin* Pin : Event->Pins)
		{
			if (Pin->Direction != EGPD_Output) continue;
			if (IsExecPin(Pin)) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate) continue;
			if (!Params.IsEmpty()) Params += TEXT(", ");
			FString TypeStr = PinTypeToString(Pin->PinType);
			Params += FString::Printf(TEXT("%s %s"), *TypeStr, *SanitizeName(Pin->PinName.ToString()));
		}

		FString Comment = TrailingComment(Event);
		return IndentStr(Indent) + FString::Printf(TEXT("void %s(%s) %s\n"), *EventName, *Params, *Comment);
	}

	static FString EmitBranch(UK2Node_IfThenElse* Branch,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure)
	{
		FString Comment = TrailingComment(Branch);
		FString Cond = InputRef(Branch->GetConditionPin(), VarNames);

		UEdGraphNode* ThenStart = GetLinkedNode(Branch->GetThenPin());
		UEdGraphNode* ElseStart = GetLinkedNode(Branch->GetElsePin());
		UEdGraphNode* Convergence = FindConvergencePoint(ThenStart, ElseStart);

		FString Out;
		Out += IndentStr(Indent) + FString::Printf(TEXT("if (%s) %s\n"), *Cond, *Comment);
		Out += IndentStr(Indent) + TEXT("{\n");
		if (ThenStart)
			Out += EmitExecFrom(ThenStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
		Out += IndentStr(Indent) + TEXT("}\n");

		if (ElseStart)
		{
			Out += IndentStr(Indent) + TEXT("else\n");
			Out += IndentStr(Indent) + TEXT("{\n");
			Out += EmitExecFrom(ElseStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent) + TEXT("}\n");
		}

		return Out;
	}

	// ── sequence ────────────────────────────────────────────────────────

	static FString EmitSequence(UK2Node_ExecutionSequence* SeqNode,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure,
		UEdGraphNode*& OutContinuation)
	{
		OutContinuation = nullptr;
		FString Comment = TrailingComment(SeqNode);

		// Collect all branch starts
		TArray<UEdGraphNode*> BranchStarts;
		TArray<UEdGraphPin*> ThenPins;
		for (int32 i = 0; ; ++i)
		{
			UEdGraphPin* ThenPin = SeqNode->GetThenPinGivenIndex(i);
			if (!ThenPin) break;
			ThenPins.Add(ThenPin);
			UEdGraphNode* Target = GetLinkedNode(ThenPin);
			if (Target) BranchStarts.Add(Target);
		}

		UEdGraphNode* Convergence = FindMultiBranchConvergence(BranchStarts);
		OutContinuation = Convergence;

		FString Out;
		Out += IndentStr(Indent) + FString::Printf(TEXT("// --- Sequence --- %s\n"), *Comment);

		for (int32 i = 0; i < ThenPins.Num(); ++i)
		{
			UEdGraphNode* Target = GetLinkedNode(ThenPins[i]);
			if (!Target) continue;
			Out += IndentStr(Indent) + FString::Printf(TEXT("// [Seq %d]\n"), i);
			Out += EmitExecFrom(Target, VarNames, Indent, Visited, EmittedPure, Convergence);
		}

		return Out;
	}

	// ── switch ──────────────────────────────────────────────────────────

	static FString EmitSwitch(UK2Node_Switch* SwitchNode,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure,
		UEdGraphNode*& OutContinuation)
	{
		OutContinuation = nullptr;
		FString Comment = TrailingComment(SwitchNode);

		// Emit pure deps for selection input
		FString Out;
		Out += EmitPureDeps(SwitchNode, VarNames, Indent, EmittedPure);

		FString Selection = InputRef(SwitchNode->GetSelectionPin(), VarNames);

		// Collect case pins and their targets
		TArray<TPair<FString, UEdGraphNode*>> Cases;
		TArray<UEdGraphNode*> BranchStarts;
		UEdGraphPin* DefaultPin = SwitchNode->GetDefaultPin();

		for (UEdGraphPin* Pin : SwitchNode->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
			if (Pin == DefaultPin) continue;
			FString CaseLabel = SwitchNode->GetExportTextForPin(Pin);
			if (CaseLabel.IsEmpty()) CaseLabel = Pin->PinName.ToString();
			UEdGraphNode* Target = GetLinkedNode(Pin);
			Cases.Add(TPair<FString, UEdGraphNode*>(CaseLabel, Target));
			if (Target) BranchStarts.Add(Target);
		}

		UEdGraphNode* DefaultTarget = DefaultPin ? GetLinkedNode(DefaultPin) : nullptr;
		if (DefaultTarget) BranchStarts.Add(DefaultTarget);

		UEdGraphNode* Convergence = FindMultiBranchConvergence(BranchStarts);
		OutContinuation = Convergence;

		// Determine if string switch for quoting
		bool bIsString = Cast<UK2Node_SwitchString>(SwitchNode) != nullptr;

		Out += IndentStr(Indent) + FString::Printf(TEXT("switch (%s) %s\n"), *Selection, *Comment);
		Out += IndentStr(Indent) + TEXT("{\n");

		for (auto& CasePair : Cases)
		{
			FString Label = bIsString
				? FString::Printf(TEXT("TEXT(\"%s\")"), *CasePair.Key)
				: CasePair.Key;
			Out += IndentStr(Indent) + FString::Printf(TEXT("case %s:\n"), *Label);
			Out += IndentStr(Indent) + TEXT("{\n");
			if (CasePair.Value)
				Out += EmitExecFrom(CasePair.Value, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent + 1) + TEXT("break;\n");
			Out += IndentStr(Indent) + TEXT("}\n");
		}

		if (DefaultTarget)
		{
			Out += IndentStr(Indent) + TEXT("default:\n");
			Out += IndentStr(Indent) + TEXT("{\n");
			Out += EmitExecFrom(DefaultTarget, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent + 1) + TEXT("break;\n");
			Out += IndentStr(Indent) + TEXT("}\n");
		}

		Out += IndentStr(Indent) + TEXT("}\n");
		return Out;
	}

	// ── macro instance ──────────────────────────────────────────────────

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Dir)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->PinName.ToString() == Name && Pin->Direction == Dir)
				return Pin;
		}
		return nullptr;
	}

	static FString EmitMacroInstance(UK2Node_MacroInstance* Macro,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure,
		UEdGraphNode*& OutContinuation)
	{
		OutContinuation = nullptr;
		UEdGraph* MacroGraph = Macro->GetMacroGraph();
		FString MacroName = MacroGraph ? MacroGraph->GetName() : TEXT("UnknownMacro");
		FString Comment = TrailingComment(Macro);
		FString Out;

		Out += EmitPureDeps(Macro, VarNames, Indent, EmittedPure);

		// Completed pin → continuation after the macro
		UEdGraphPin* CompletedPin = FindPinByName(Macro, TEXT("Completed"), EGPD_Output);
		UEdGraphNode* CompletedTarget = CompletedPin ? GetLinkedNode(CompletedPin) : nullptr;

		// --- ForEachLoop / ForEachLoopWithBreak ---
		if (MacroName == TEXT("ForEachLoop") || MacroName == TEXT("ForEachLoopWithBreak"))
		{
			FString Array = InputRef(FindPinByName(Macro, TEXT("Array"), EGPD_Input), VarNames);
			Out += IndentStr(Indent) + FString::Printf(TEXT("for (auto& Element : %s) %s\n"), *Array, *Comment);
			Out += IndentStr(Indent) + TEXT("{\n");
			UEdGraphNode* LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("Loop Body"), EGPD_Output));
			if (!LoopBody) LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("LoopBody"), EGPD_Output));
			if (LoopBody)
				Out += EmitExecFrom(LoopBody, VarNames, Indent + 1, Visited, EmittedPure);
			Out += IndentStr(Indent) + TEXT("}\n");
			OutContinuation = CompletedTarget;
			return Out;
		}

		// --- ForLoop / ForLoopWithBreak ---
		if (MacroName == TEXT("ForLoop") || MacroName == TEXT("ForLoopWithBreak"))
		{
			FString First = InputRef(FindPinByName(Macro, TEXT("FirstIndex"), EGPD_Input), VarNames);
			FString Last = InputRef(FindPinByName(Macro, TEXT("LastIndex"), EGPD_Input), VarNames);
			Out += IndentStr(Indent) + FString::Printf(TEXT("for (int32 Index = %s; Index <= %s; ++Index) %s\n"), *First, *Last, *Comment);
			Out += IndentStr(Indent) + TEXT("{\n");
			UEdGraphNode* LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("Loop Body"), EGPD_Output));
			if (!LoopBody) LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("LoopBody"), EGPD_Output));
			if (LoopBody)
				Out += EmitExecFrom(LoopBody, VarNames, Indent + 1, Visited, EmittedPure);
			Out += IndentStr(Indent) + TEXT("}\n");
			OutContinuation = CompletedTarget;
			return Out;
		}

		// --- WhileLoop ---
		if (MacroName == TEXT("WhileLoop"))
		{
			FString Cond = InputRef(FindPinByName(Macro, TEXT("Condition"), EGPD_Input), VarNames);
			Out += IndentStr(Indent) + FString::Printf(TEXT("while (%s) %s\n"), *Cond, *Comment);
			Out += IndentStr(Indent) + TEXT("{\n");
			UEdGraphNode* LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("Loop Body"), EGPD_Output));
			if (!LoopBody) LoopBody = GetLinkedNode(FindPinByName(Macro, TEXT("LoopBody"), EGPD_Output));
			if (LoopBody)
				Out += EmitExecFrom(LoopBody, VarNames, Indent + 1, Visited, EmittedPure);
			Out += IndentStr(Indent) + TEXT("}\n");
			OutContinuation = CompletedTarget;
			return Out;
		}

		// --- IsValid ---
		if (MacroName == TEXT("IsValid"))
		{
			FString Obj = InputRef(FindPinByName(Macro, TEXT("InputObject"), EGPD_Input), VarNames);
			UEdGraphNode* ValidStart = GetLinkedNode(FindPinByName(Macro, TEXT("Is Valid"), EGPD_Output));
			UEdGraphNode* NotValidStart = GetLinkedNode(FindPinByName(Macro, TEXT("Is Not Valid"), EGPD_Output));

			TArray<UEdGraphNode*> Branches;
			if (ValidStart) Branches.Add(ValidStart);
			if (NotValidStart) Branches.Add(NotValidStart);
			UEdGraphNode* Convergence = FindMultiBranchConvergence(Branches);

			Out += IndentStr(Indent) + FString::Printf(TEXT("if (IsValid(%s)) %s\n"), *Obj, *Comment);
			Out += IndentStr(Indent) + TEXT("{\n");
			if (ValidStart)
				Out += EmitExecFrom(ValidStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent) + TEXT("}\n");
			if (NotValidStart)
			{
				Out += IndentStr(Indent) + TEXT("else\n");
				Out += IndentStr(Indent) + TEXT("{\n");
				Out += EmitExecFrom(NotValidStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
				Out += IndentStr(Indent) + TEXT("}\n");
			}
			OutContinuation = Convergence;
			return Out;
		}

		// --- FlipFlop ---
		if (MacroName == TEXT("FlipFlop"))
		{
			UEdGraphNode* AStart = GetLinkedNode(FindPinByName(Macro, TEXT("A"), EGPD_Output));
			UEdGraphNode* BStart = GetLinkedNode(FindPinByName(Macro, TEXT("B"), EGPD_Output));

			TArray<UEdGraphNode*> Branches;
			if (AStart) Branches.Add(AStart);
			if (BStart) Branches.Add(BStart);
			UEdGraphNode* Convergence = FindMultiBranchConvergence(Branches);

			Out += IndentStr(Indent) + FString::Printf(TEXT("// FlipFlop %s\n"), *Comment);
			Out += IndentStr(Indent) + TEXT("if (/*FlipFlop A*/)\n");
			Out += IndentStr(Indent) + TEXT("{\n");
			if (AStart)
				Out += EmitExecFrom(AStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent) + TEXT("}\n");
			Out += IndentStr(Indent) + TEXT("else // FlipFlop B\n");
			Out += IndentStr(Indent) + TEXT("{\n");
			if (BStart)
				Out += EmitExecFrom(BStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent) + TEXT("}\n");
			OutContinuation = Convergence;
			return Out;
		}

		// --- Gate ---
		if (MacroName == TEXT("Gate"))
		{
			UEdGraphNode* ExitTarget = GetLinkedNode(FindPinByName(Macro, TEXT("Exit"), EGPD_Output));
			Out += IndentStr(Indent) + FString::Printf(TEXT("// Gate %s\n"), *Comment);
			if (ExitTarget)
				OutContinuation = ExitTarget;
			return Out;
		}

		// --- DoOnce ---
		if (MacroName == TEXT("DoOnce"))
		{
			Out += IndentStr(Indent) + FString::Printf(TEXT("// DoOnce %s\n"), *Comment);
			OutContinuation = CompletedTarget;
			return Out;
		}

		// --- Generic unknown macro ---
		{
			FString Args;
			for (UEdGraphPin* Pin : Macro->Pins)
			{
				if (Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
				if (!Args.IsEmpty()) Args += TEXT(", ");
				Args += InputRef(Pin, VarNames);
			}

			// Collect exec outputs (non-Completed)
			TArray<TPair<FString, UEdGraphNode*>> ExecOuts;
			for (UEdGraphPin* Pin : Macro->Pins)
			{
				if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
				if (Pin == CompletedPin) continue;
				UEdGraphNode* Target = GetLinkedNode(Pin);
				ExecOuts.Add(TPair<FString, UEdGraphNode*>(Pin->PinName.ToString(), Target));
			}

			Out += IndentStr(Indent) + FString::Printf(TEXT("%s(%s); %s\n"), *MacroName, *Args, *Comment);

			if (ExecOuts.Num() == 1 && ExecOuts[0].Value)
			{
				// Single exec output — inline
				Out += EmitExecFrom(ExecOuts[0].Value, VarNames, Indent, Visited, EmittedPure);
			}
			else if (ExecOuts.Num() > 1)
			{
				TArray<UEdGraphNode*> Branches;
				for (auto& Pair : ExecOuts)
					if (Pair.Value) Branches.Add(Pair.Value);
				UEdGraphNode* Convergence = FindMultiBranchConvergence(Branches);

				for (auto& Pair : ExecOuts)
				{
					Out += IndentStr(Indent) + FString::Printf(TEXT("// [%s]\n"), *Pair.Key);
					Out += IndentStr(Indent) + TEXT("{\n");
					if (Pair.Value)
						Out += EmitExecFrom(Pair.Value, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
					Out += IndentStr(Indent) + TEXT("}\n");
				}
				OutContinuation = Convergence;
				return Out;
			}

			OutContinuation = CompletedTarget;
			return Out;
		}
	}

	// ── dynamic cast ────────────────────────────────────────────────────

	static FString EmitDynamicCast(UK2Node_DynamicCast* CastNode,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure,
		UEdGraphNode*& OutContinuation)
	{
		OutContinuation = nullptr;
		FString VN = VarNames.FindRef(CastNode);
		FString TargetName = CastNode->TargetType ? CastNode->TargetType->GetName() : TEXT("Unknown");
		FString ObjRef = InputRef(CastNode->GetCastSourcePin(), VarNames);
		FString Comment = TrailingComment(CastNode);

		FString Out;
		Out += EmitPureDeps(CastNode, VarNames, Indent, EmittedPure);
		Out += IndentStr(Indent) + FString::Printf(TEXT("%s* %s = Cast<%s>(%s); %s\n"),
			*TargetName, *VN, *TargetName, *ObjRef, *Comment);

		UEdGraphNode* SuccessStart = GetLinkedNode(CastNode->GetValidCastPin());
		UEdGraphNode* FailStart = GetLinkedNode(CastNode->GetInvalidCastPin());

		TArray<UEdGraphNode*> Branches;
		if (SuccessStart) Branches.Add(SuccessStart);
		if (FailStart) Branches.Add(FailStart);
		UEdGraphNode* Convergence = FindMultiBranchConvergence(Branches);
		OutContinuation = Convergence;

		Out += IndentStr(Indent) + FString::Printf(TEXT("if (%s)\n"), *VN);
		Out += IndentStr(Indent) + TEXT("{\n");
		if (SuccessStart)
			Out += EmitExecFrom(SuccessStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
		Out += IndentStr(Indent) + TEXT("}\n");

		if (FailStart)
		{
			Out += IndentStr(Indent) + TEXT("else\n");
			Out += IndentStr(Indent) + TEXT("{\n");
			Out += EmitExecFrom(FailStart, VarNames, Indent + 1, Visited, EmittedPure, Convergence);
			Out += IndentStr(Indent) + TEXT("}\n");
		}

		return Out;
	}

	// ── spawn actor ─────────────────────────────────────────────────────

	static FString EmitSpawnActor(UK2Node_SpawnActorFromClass* SpawnNode,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(SpawnNode);
		FString Comment = TrailingComment(SpawnNode);

		UClass* ClassToSpawn = SpawnNode->GetClassToSpawn();
		FString ClassName = ClassToSpawn ? ClassToSpawn->GetName() : TEXT("AActor");

		FString Transform = InputRef(SpawnNode->FindPin(TEXT("SpawnTransform")), VarNames);

		FString Out;
		Out += IndentStr(Indent) + FString::Printf(TEXT("%s* %s = GetWorld()->SpawnActor<%s>(%s); %s\n"),
			*ClassName, *VN, *ClassName, *Transform, *Comment);

		// Emit spawn var pin assignments
		for (UEdGraphPin* Pin : SpawnNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (IsExecPin(Pin)) continue;
			if (SpawnNode->IsSpawnVarPin(Pin))
			{
				FString Value = InputRef(Pin, VarNames);
				Out += IndentStr(Indent) + FString::Printf(TEXT("%s->%s = %s;\n"),
					*VN, *SanitizeName(Pin->PinName.ToString()), *Value);
			}
		}

		return Out;
	}

	// ── make array ──────────────────────────────────────────────────────

	static FString EmitMakeArray(UK2Node_MakeArray* ArrayNode,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(ArrayNode);
		FString Comment = TrailingComment(ArrayNode);

		FString Elements;
		for (UEdGraphPin* Pin : ArrayNode->Pins)
		{
			if (Pin->Direction != EGPD_Input) continue;
			if (!Elements.IsEmpty()) Elements += TEXT(", ");
			Elements += InputRef(Pin, VarNames);
		}

		// Resolve element type from output pin
		FString ElemType = TEXT("auto");
		for (UEdGraphPin* Pin : ArrayNode->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{
				// Output is TArray<T>, get T from inner pin type category
				if (Pin->PinType.IsArray())
				{
					FEdGraphPinType Inner = Pin->PinType;
					Inner.ContainerType = EPinContainerType::None;
					ElemType = PinTypeToString(Inner);
				}
				else
					ElemType = PinTypeToString(Pin->PinType);
				break;
			}
		}
		return IndentStr(Indent) + FString::Printf(TEXT("TArray<%s> %s = { %s }; %s\n"),
			*ElemType, *VN, *Elements, *Comment);
	}

	// ── select ──────────────────────────────────────────────────────────

	static FString EmitSelect(UK2Node_Select* SelectNode,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(SelectNode);
		FString Comment = TrailingComment(SelectNode);
		FString IndexRef = InputRef(SelectNode->GetIndexPin(), VarNames);

		TArray<UEdGraphPin*> OptionPins;
		SelectNode->GetOptionPins(OptionPins);

		// Resolve output type
		FString TypeStr = TEXT("auto");
		for (UEdGraphPin* Pin : SelectNode->Pins)
		{
			if (Pin->Direction == EGPD_Output && !IsExecPin(Pin))
			{ TypeStr = PinTypeToString(Pin->PinType); break; }
		}

		if (OptionPins.Num() == 2)
		{
			FString Opt0 = InputRef(OptionPins[0], VarNames);
			FString Opt1 = InputRef(OptionPins[1], VarNames);
			return IndentStr(Indent) + FString::Printf(TEXT("%s %s = (%s) ? %s : %s; %s\n"),
				*TypeStr, *VN, *IndexRef, *Opt0, *Opt1, *Comment);
		}

		FString Options;
		for (UEdGraphPin* Pin : OptionPins)
		{
			if (!Options.IsEmpty()) Options += TEXT(", ");
			Options += InputRef(Pin, VarNames);
		}
		return IndentStr(Indent) + FString::Printf(TEXT("%s %s = Select(%s, %s); %s\n"),
			*TypeStr, *VN, *IndexRef, *Options, *Comment);
	}

	// ── function result ─────────────────────────────────────────────────

	static FString EmitFunctionResult(UK2Node_FunctionResult* ResultNode,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent,
		TSet<UEdGraphNode*>& EmittedPure)
	{
		FString Comment = TrailingComment(ResultNode);
		FString Out;

		Out += EmitPureDeps(ResultNode, VarNames, Indent, EmittedPure);

		// Collect non-exec input pins (return values)
		TArray<UEdGraphPin*> ReturnPins;
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
			ReturnPins.Add(Pin);
		}

		if (ReturnPins.Num() == 1)
		{
			FString Value = InputRef(ReturnPins[0], VarNames);
			Out += IndentStr(Indent) + FString::Printf(TEXT("return %s; %s\n"), *Value, *Comment);
		}
		else if (ReturnPins.Num() > 1)
		{
			for (UEdGraphPin* Pin : ReturnPins)
			{
				FString Value = InputRef(Pin, VarNames);
				Out += IndentStr(Indent) + FString::Printf(TEXT("%s = %s;\n"),
					*SanitizeName(Pin->PinName.ToString()), *Value);
			}
			Out += IndentStr(Indent) + FString::Printf(TEXT("return; %s\n"), *Comment);
		}
		else
		{
			Out += IndentStr(Indent) + FString::Printf(TEXT("return; %s\n"), *Comment);
		}

		return Out;
	}

	// ── pure cast (no exec pins) ────────────────────────────────────────

	static FString EmitPureCast(UK2Node_DynamicCast* CastNode,
		const TMap<UEdGraphNode*, FString>& VarNames, int32 Indent)
	{
		FString VN = VarNames.FindRef(CastNode);
		FString TargetName = CastNode->TargetType ? CastNode->TargetType->GetName() : TEXT("Unknown");
		FString ObjRef = InputRef(CastNode->GetCastSourcePin(), VarNames);
		FString Comment = TrailingComment(CastNode);
		return IndentStr(Indent) + FString::Printf(TEXT("%s* %s = Cast<%s>(%s); %s\n"),
			*TargetName, *VN, *TargetName, *ObjRef, *Comment);
	}

	static FString EmitExecFrom(UEdGraphNode* StartNode,
		const TMap<UEdGraphNode*, FString>& VarNames,
		int32 Indent,
		TSet<UEdGraphNode*>& Visited,
		TSet<UEdGraphNode*>& EmittedPure,
		UEdGraphNode* StopBefore = nullptr)
	{
		FString Out;
		UEdGraphNode* Node = StartNode;

		while (Node && !Visited.Contains(Node))
		{
			if (Node == StopBefore) break;

			Visited.Add(Node);

			if (Cast<UK2Node_Knot>(Node))
			{
				Node = FollowExecOutput(Node);
				continue;
			}

			// Event entry — emit signature + body
			if (auto* Event = Cast<UK2Node_Event>(Node))
			{
				Out += EmitEvent(Event, VarNames, Indent);
				Out += IndentStr(Indent) + TEXT("{\n");
				UEdGraphNode* BodyStart = FollowExecOutput(Event);
				Out += EmitExecFrom(BodyStart, VarNames, Indent + 1, Visited, EmittedPure);
				Out += IndentStr(Indent) + TEXT("}\n\n");
				break;
			}

			// Function entry
			if (auto* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				FString FuncName = SanitizeName(FuncEntry->GetGraph()->GetName());
				FString Comment = TrailingComment(FuncEntry);

				// Emit parameters from output data pins
				FString Params;
				for (UEdGraphPin* Pin : FuncEntry->Pins)
				{
					if (Pin->Direction != EGPD_Output) continue;
					if (IsExecPin(Pin)) continue;
					if (!Params.IsEmpty()) Params += TEXT(", ");
					FString TypeStr = PinTypeToString(Pin->PinType);
					Params += FString::Printf(TEXT("%s %s"), *TypeStr, *SanitizeName(Pin->PinName.ToString()));
				}

				Out += IndentStr(Indent) + FString::Printf(TEXT("void %s(%s) %s\n"), *FuncName, *Params, *Comment);
				Out += IndentStr(Indent) + TEXT("{\n");
				UEdGraphNode* BodyStart = FollowExecOutput(FuncEntry);
				Out += EmitExecFrom(BodyStart, VarNames, Indent + 1, Visited, EmittedPure);
				Out += IndentStr(Indent) + TEXT("}\n\n");
				break;
			}

			// Macro entry tunnel
			if (auto* Tunnel = Cast<UK2Node_Tunnel>(Node))
			{
				if (Tunnel->bCanHaveOutputs && !Cast<UK2Node_MacroInstance>(Node) && !Cast<UK2Node_Composite>(Node))
				{
					FString MacroName = SanitizeName(Tunnel->GetGraph()->GetName());
					FString Comment = TrailingComment(Tunnel);

					FString Params;
					for (UEdGraphPin* Pin : Tunnel->Pins)
					{
						if (Pin->Direction != EGPD_Output) continue;
						if (IsExecPin(Pin)) continue;
						if (!Params.IsEmpty()) Params += TEXT(", ");
						FString TypeStr = PinTypeToString(Pin->PinType);
						Params += FString::Printf(TEXT("%s %s"), *TypeStr, *SanitizeName(Pin->PinName.ToString()));
					}

					Out += IndentStr(Indent) + FString::Printf(TEXT("/* Macro */ %s(%s) %s\n"), *MacroName, *Params, *Comment);
					Out += IndentStr(Indent) + TEXT("{\n");
					UEdGraphNode* BodyStart = FollowExecOutput(Tunnel);
					Out += EmitExecFrom(BodyStart, VarNames, Indent + 1, Visited, EmittedPure);
					Out += IndentStr(Indent) + TEXT("}\n\n");
					break;
				}
			}

			// Emit pure data dependencies
			Out += EmitPureDeps(Node, VarNames, Indent, EmittedPure);

			// Branch — if/else
			if (auto* Branch = Cast<UK2Node_IfThenElse>(Node))
			{
				Out += EmitBranch(Branch, VarNames, Indent, Visited, EmittedPure);

				UEdGraphNode* ThenStart = GetLinkedNode(Branch->GetThenPin());
				UEdGraphNode* ElseStart = GetLinkedNode(Branch->GetElsePin());
				UEdGraphNode* Convergence = FindConvergencePoint(ThenStart, ElseStart);
				if (Convergence && !Visited.Contains(Convergence))
				{
					Node = Convergence;
					continue;
				}
				break;
			}

			// Sequence
			if (auto* SeqNode = Cast<UK2Node_ExecutionSequence>(Node))
			{
				UEdGraphNode* SeqContinuation = nullptr;
				Out += EmitSequence(SeqNode, VarNames, Indent, Visited, EmittedPure, SeqContinuation);
				if (SeqContinuation && !Visited.Contains(SeqContinuation))
				{
					Node = SeqContinuation;
					continue;
				}
				break;
			}

			// Switch
			if (auto* SwitchNode = Cast<UK2Node_Switch>(Node))
			{
				UEdGraphNode* SwitchContinuation = nullptr;
				Out += EmitSwitch(SwitchNode, VarNames, Indent, Visited, EmittedPure, SwitchContinuation);
				if (SwitchContinuation && !Visited.Contains(SwitchContinuation))
				{
					Node = SwitchContinuation;
					continue;
				}
				break;
			}

			// Macro Instance
			if (auto* MacroNode = Cast<UK2Node_MacroInstance>(Node))
			{
				UEdGraphNode* MacroContinuation = nullptr;
				Out += EmitMacroInstance(MacroNode, VarNames, Indent, Visited, EmittedPure, MacroContinuation);
				if (MacroContinuation && !Visited.Contains(MacroContinuation))
				{
					Node = MacroContinuation;
					continue;
				}
				break;
			}

			// Dynamic Cast (impure)
			if (auto* CastNode = Cast<UK2Node_DynamicCast>(Node))
			{
				if (!CastNode->IsNodePure())
				{
					UEdGraphNode* CastContinuation = nullptr;
					Out += EmitDynamicCast(CastNode, VarNames, Indent, Visited, EmittedPure, CastContinuation);
					if (CastContinuation && !Visited.Contains(CastContinuation))
					{
						Node = CastContinuation;
						continue;
					}
					break;
				}
			}

			// Function Result
			if (auto* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				Out += EmitFunctionResult(ResultNode, VarNames, Indent, EmittedPure);
				break;
			}

			// Macro exit tunnel
			if (auto* Tunnel = Cast<UK2Node_Tunnel>(Node))
			{
				if (Tunnel->bCanHaveInputs && !Cast<UK2Node_MacroInstance>(Node) && !Cast<UK2Node_Composite>(Node))
				{
					FString Comment = TrailingComment(Tunnel);
					Out += EmitPureDeps(Tunnel, VarNames, Indent, EmittedPure);
					for (UEdGraphPin* Pin : Tunnel->Pins)
					{
						if (Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
						FString Value = InputRef(Pin, VarNames);
						Out += IndentStr(Indent) + FString::Printf(TEXT("%s = %s; // macro output\n"),
							*SanitizeName(Pin->PinName.ToString()), *Value);
					}
					Out += IndentStr(Indent) + FString::Printf(TEXT("// macro exit %s\n"), *Comment);
					break;
				}
			}

			// Normal exec node
			Out += EmitNode(Node, VarNames, Indent);

			Node = FollowExecOutput(Node);
		}

		return Out;
	}

	// ── graph traversal ─────────────────────────────────────────────────

	static TArray<UEdGraphNode*> FindEntryNodes(UEdGraph* Graph)
	{
		TArray<UEdGraphNode*> Entries;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (Cast<UK2Node_Event>(Node) || Cast<UK2Node_CustomEvent>(Node) || Cast<UK2Node_FunctionEntry>(Node))
				Entries.Add(Node);
			else if (auto* Tunnel = Cast<UK2Node_Tunnel>(Node))
			{
				if (!Cast<UK2Node_MacroInstance>(Node) && !Cast<UK2Node_Composite>(Node)
					&& Tunnel->bCanHaveOutputs)
				{
					Entries.Add(Node);
				}
			}
		}
		Entries.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.NodePosY < B.NodePosY; });
		return Entries;
	}

	static void WalkExecChain(UEdGraphNode* Node, TArray<UEdGraphNode*>& OutNodes, TSet<UEdGraphNode*>& Visited)
	{
		if (!Node || Visited.Contains(Node)) return;
		Visited.Add(Node);
		OutNodes.Add(Node);

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
					WalkExecChain(LinkedPin->GetOwningNode(), OutNodes, Visited);
			}
		}
	}

	static void CollectDataDependencies(UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || IsExecPin(Pin)) continue;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				UEdGraphNode* SourceNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (!SourceNode || Visited.Contains(SourceNode)) continue;
				if (!HasExecPins(SourceNode))
				{
					Visited.Add(SourceNode);
					CollectDataDependencies(SourceNode, Visited);
				}
			}
		}
	}

	static TArray<UEdGraphNode*> CollectDanglingNodes(UEdGraph* Graph, const TSet<UEdGraphNode*>& Visited)
	{
		TArray<UEdGraphNode*> Dangling;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Visited.Contains(Node)) continue;
			if (Node->IsA<UEdGraphNode_Comment>()) continue;
			Dangling.Add(Node);
		}
		Dangling.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			return A.NodePosY != B.NodePosY ? A.NodePosY < B.NodePosY : A.NodePosX < B.NodePosX;
		});
		return Dangling;
	}

	// ── naming ──────────────────────────────────────────────────────────

	static FString SanitizeName(const FString& Name)
	{
		FString Result;
		Result.Reserve(Name.Len());
		bool bLastWasUnderscore = true;
		for (TCHAR Ch : Name)
		{
			if (FChar::IsAlnum(Ch))
			{
				Result.AppendChar(Ch);
				bLastWasUnderscore = false;
			}
			else if (!bLastWasUnderscore)
			{
				Result.AppendChar(TEXT('_'));
				bLastWasUnderscore = true;
			}
		}
		if (Result.Len() > 0 && Result[Result.Len() - 1] == TEXT('_'))
			Result.LeftChopInline(1);
		if (Result.IsEmpty()) Result = TEXT("Unnamed");
		return Result;
	}

	static FString TrailingComment(UEdGraphNode* Node)
	{
		FString GUID = FMCPJsonHelpers::GuidToCompact(Node->NodeGuid);
		int32 X = Node->NodePosX;
		int32 Y = Node->NodePosY;
		FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

		if (Title.IsEmpty())
			return FString::Printf(TEXT("// [%s] (%d,%d)"), *GUID, X, Y);
		return FString::Printf(TEXT("// [%s] (%d,%d) \"%s\""), *GUID, X, Y, *Title);
	}

	static FString BuildVarName(UEdGraphNode* Node)
	{
		if (auto* CallFunc = Cast<UK2Node_CallFunction>(Node))
		{
			FName FuncName = CallFunc->FunctionReference.GetMemberName();
			return FString::Printf(TEXT("Local_%s"), *SanitizeName(FuncName.ToString()));
		}
		if (auto* VarGet = Cast<UK2Node_VariableGet>(Node))
		{
			FName VarName = VarGet->GetVarName();
			return FString::Printf(TEXT("Local_%s"), *SanitizeName(VarName.ToString()));
		}
		if (auto* VarSet = Cast<UK2Node_VariableSet>(Node))
		{
			FName VarName = VarSet->GetVarName();
			return FString::Printf(TEXT("Set_%s"), *SanitizeName(VarName.ToString()));
		}
		if (auto* Event = Cast<UK2Node_Event>(Node))
		{
			FString EventName = Event->GetNodeTitle(ENodeTitleType::ListView).ToString();
			return FString::Printf(TEXT("Event_%s"), *SanitizeName(EventName));
		}
		if (auto* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
			return FString::Printf(TEXT("Event_%s"), *SanitizeName(CustomEvent->CustomFunctionName.ToString()));
		if (Cast<UK2Node_FunctionEntry>(Node))
			return TEXT("FunctionEntry");
		if (Cast<UK2Node_IfThenElse>(Node))
			return TEXT("Branch");
		if (Cast<UK2Node_ExecutionSequence>(Node))
			return TEXT("Sequence");
		if (Cast<UK2Node_Switch>(Node))
			return TEXT("Switch");
		if (auto* MacroNode = Cast<UK2Node_MacroInstance>(Node))
		{
			UEdGraph* MG = MacroNode->GetMacroGraph();
			return FString::Printf(TEXT("Local_%s"), *SanitizeName(MG ? MG->GetName() : TEXT("Macro")));
		}
		if (auto* Tunnel = Cast<UK2Node_Tunnel>(Node))
		{
			if (!Cast<UK2Node_MacroInstance>(Node) && !Cast<UK2Node_Composite>(Node))
			{
				FString GraphName = Tunnel->GetGraph() ? SanitizeName(Tunnel->GetGraph()->GetName()) : TEXT("Macro");
				if (Tunnel->bCanHaveOutputs)
					return FString::Printf(TEXT("MacroEntry_%s"), *GraphName);
				return FString::Printf(TEXT("MacroExit_%s"), *GraphName);
			}
		}
		if (auto* CastNode = Cast<UK2Node_DynamicCast>(Node))
		{
			FString TargetName = CastNode->TargetType ? CastNode->TargetType->GetName() : TEXT("Unknown");
			return FString::Printf(TEXT("Local_CastTo_%s"), *SanitizeName(TargetName));
		}
		if (auto* SpawnNode = Cast<UK2Node_SpawnActorFromClass>(Node))
		{
			UClass* ClassToSpawn = SpawnNode->GetClassToSpawn();
			FString CN = ClassToSpawn ? ClassToSpawn->GetName() : TEXT("Actor");
			return FString::Printf(TEXT("Local_Spawn_%s"), *SanitizeName(CN));
		}
		if (Cast<UK2Node_MakeArray>(Node))
			return TEXT("Local_MakeArray");
		if (Cast<UK2Node_Select>(Node))
			return TEXT("Local_Select");
		if (Cast<UK2Node_FunctionResult>(Node))
			return TEXT("FunctionResult");

		FString ClassName = Node->GetClass()->GetName();
		ClassName.ReplaceInline(TEXT("K2Node_"), TEXT(""));
		FString CompactGuid = FMCPJsonHelpers::GuidToCompact(Node->NodeGuid);
		return FString::Printf(TEXT("Local_%s_%s"), *SanitizeName(ClassName), *CompactGuid);
	}

	static TMap<UEdGraphNode*, FString> BuildVarNameMap(const TArray<UEdGraphNode*>& Nodes)
	{
		TMap<UEdGraphNode*, FString> VarNames;
		TMap<FString, TArray<UEdGraphNode*>> NameToNodes;

		for (UEdGraphNode* Node : Nodes)
		{
			FString Name = BuildVarName(Node);
			VarNames.Add(Node, Name);
			NameToNodes.FindOrAdd(Name).Add(Node);
		}

		// Resolve collisions with guid suffix
		for (auto& Pair : NameToNodes)
		{
			if (Pair.Value.Num() > 1)
			{
				for (UEdGraphNode* Node : Pair.Value)
				{
					FString CompactGuid = FMCPJsonHelpers::GuidToCompact(Node->NodeGuid);
					VarNames[Node] = FString::Printf(TEXT("%s_%s"), *Pair.Key, *CompactGuid);
				}
			}
		}

		// Second pass: if still collisions, use numeric suffixes
		TMap<FString, TArray<UEdGraphNode*>> NameToNodes2;
		for (auto& Pair : VarNames)
			NameToNodes2.FindOrAdd(Pair.Value).Add(Pair.Key);
		for (auto& Pair : NameToNodes2)
		{
			if (Pair.Value.Num() > 1)
			{
				Pair.Value.Sort([&Nodes](const UEdGraphNode& A, const UEdGraphNode& B)
				{
					return Nodes.IndexOfByKey(&A) < Nodes.IndexOfByKey(&B);
				});
				int32 Suffix = 0;
				for (UEdGraphNode* Node : Pair.Value)
					VarNames[Node] = FString::Printf(TEXT("%s_%d"), *Pair.Key, Suffix++);
			}
		}

		return VarNames;
	}
};
