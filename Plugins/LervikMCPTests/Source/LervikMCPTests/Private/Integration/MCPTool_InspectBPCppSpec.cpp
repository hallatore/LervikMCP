#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"

BEGIN_DEFINE_SPEC(FMCPTool_InspectBPCppSpec, "Plugins.LervikMCP.Integration.Tools.Inspect.BPCpp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* InspectTool = nullptr;
	IMCPTool* GraphTool = nullptr;
END_DEFINE_SPEC(FMCPTool_InspectBPCppSpec)

void FMCPTool_InspectBPCppSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		InspectTool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
		GraphTool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
	});

	AfterEach([this]()
	{
		InspectTool = nullptr;
		GraphTool = nullptr;
		Helper.Cleanup();
	});

	// ── Helper lambdas ──────────────────────────────────────────────────

	auto AddNode = [this](const FString& BPPath, const FString& Graph, const FString& NodeClass,
		int32 PosX, int32 PosY, const FString& ExtraJson = TEXT("")) -> FString
	{
		FString Extra = ExtraJson.IsEmpty() ? TEXT("") : (TEXT(",") + ExtraJson);
		FString Json = FString::Printf(
			TEXT(R"({"action":"add_node","target":"%s","graph":"%s","node_class":"%s","pos_x":%d,"pos_y":%d%s})"),
			*BPPath, *Graph, *NodeClass, PosX, PosY, *Extra);
		FMCPToolResult R = GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(Json));
		if (R.bIsError) return TEXT("");
		TSharedPtr<FJsonObject> J = FMCPToolDirectTestHelper::ParseResultJson(R);
		FString NodeId;
		if (J.IsValid()) J->TryGetStringField(TEXT("node_id"), NodeId);
		return NodeId;
	};

	auto Connect = [this](const FString& BPPath, const FString& SrcNode, const FString& SrcPin,
		const FString& DstNode, const FString& DstPin) -> bool
	{
		FMCPToolResult R = GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
			FString::Printf(TEXT(R"({"action":"connect","target":"%s","source":{"node":"%s","pin":"%s"},"dest":{"node":"%s","pin":"%s"}})"),
				*BPPath, *SrcNode, *SrcPin, *DstNode, *DstPin)));
		return !R.bIsError;
	};

	auto GenerateCpp = [this](const FString& BPPath, const FString& GraphName = TEXT("")) -> FMCPToolResult
	{
		FString Target = GraphName.IsEmpty() ? BPPath : (BPPath + TEXT("::") + GraphName);
		return InspectTool->Execute(
			FMCPToolDirectTestHelper::MakeParams({
				{ TEXT("target"), Target },
				{ TEXT("type"), TEXT("cpp") }
			}));
	};

	auto FindBeginPlayGuid = [](UBlueprint* BP) -> FString
	{
		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Cast<UK2Node_Event>(Node))
					return FMCPJsonHelpers::GuidToCompact(Node->NodeGuid);
			}
		}
		return TEXT("");
	};

	// ── Tests ───────────────────────────────────────────────────────────

	Describe("default graph", [this, GenerateCpp]()
	{
		It("empty graph name defaults to EventGraph", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppDefault"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = GenerateCpp(FMCPToolDirectTestHelper::GetAssetPath(BP));

			TestFalse("not error", Result.bIsError);
			TestTrue("contains Graph: EventGraph",
				Result.Content.Contains(TEXT("// Graph: EventGraph")));
			TestTrue("contains Blueprint: header",
				Result.Content.Contains(TEXT("// Blueprint:")));
		});
	});

	Describe("basic event + function call", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
	{
		It("BeginPlay connected to PrintString produces valid CPP", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppBasic"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);
			FString BeginPlayId = FindBeginPlayGuid(BP);
			if (!TestFalse("BeginPlay found", BeginPlayId.IsEmpty())) return;

			// Add PrintString node
			FString PrintId = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 200, 0,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			if (!TestFalse("PrintString node added", PrintId.IsEmpty())) return;

			// Connect BeginPlay then → PrintString execute
			bool bConnected = Connect(BPPath, BeginPlayId, TEXT("then"), PrintId, TEXT("execute"));
			TestTrue("connected BeginPlay to PrintString", bConnected);

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains void (event signature)",
				Result.Content.Contains(TEXT("void ")));
			TestTrue("contains PrintString call",
				Result.Content.Contains(TEXT("PrintString(")));
			TestTrue("contains trailing comment",
				Result.Content.Contains(TEXT("// [")));
		});
	});

	Describe("branch node", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
	{
		It("branch produces if/else structure", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppBranch"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);
			FString BeginPlayId = FindBeginPlayGuid(BP);
			if (!TestFalse("BeginPlay found", BeginPlayId.IsEmpty())) return;

			// Add Branch node
			FString BranchId = AddNode(BPPath, TEXT("EventGraph"), TEXT("Branch"), 200, 0);
			if (!TestFalse("Branch node added", BranchId.IsEmpty())) return;

			// Add two PrintString nodes for Then and Else
			FString Print1Id = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 400, -100,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			FString Print2Id = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 400, 100,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			if (!TestFalse("PrintString1 added", Print1Id.IsEmpty())) return;
			if (!TestFalse("PrintString2 added", Print2Id.IsEmpty())) return;

			// Connect: BeginPlay → Branch → Then/Else → PrintStrings
			Connect(BPPath, BeginPlayId, TEXT("then"), BranchId, TEXT("execute"));
			Connect(BPPath, BranchId, TEXT("Then"), Print1Id, TEXT("execute"));
			Connect(BPPath, BranchId, TEXT("Else"), Print2Id, TEXT("execute"));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains if (",
				Result.Content.Contains(TEXT("if (")));
			TestTrue("contains else",
				Result.Content.Contains(TEXT("else")));
			TestTrue("contains braces",
				Result.Content.Contains(TEXT("{")));
		});
	});

	Describe("variable get/set", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
	{
		It("variables appear in header and body", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppVarGetSet"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);
			FString BeginPlayId = FindBeginPlayGuid(BP);
			if (!TestFalse("BeginPlay found", BeginPlayId.IsEmpty())) return;

			// Add a float variable
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"Health","var_type":"float"})"), *BPPath)));

			// Add VariableSet node
			FString SetId = AddNode(BPPath, TEXT("EventGraph"), TEXT("VariableSet"), 200, 0,
				TEXT(R"("properties":{"VariableName":"Health"})"));
			if (!TestFalse("VariableSet added", SetId.IsEmpty())) return;

			// Connect BeginPlay → VariableSet
			Connect(BPPath, BeginPlayId, TEXT("then"), SetId, TEXT("execute"));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("variables header contains float",
				Result.Content.Contains(TEXT("float")));
			TestTrue("variables header contains Health",
				Result.Content.Contains(TEXT("Health")));
			TestTrue("body contains assignment",
				Result.Content.Contains(TEXT("Health =")));
		});
	});

	Describe("dangling nodes", [this, AddNode, GenerateCpp]()
	{
		It("unconnected node appears in Dangling section", [this, AddNode, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppDangling"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a PrintString node without connecting it
			FString PrintId = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 500, 500,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			TestFalse("PrintString added", PrintId.IsEmpty());

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains Dangling section",
				Result.Content.Contains(TEXT("Dangling")));
			TestTrue("contains Unconnected marker",
				Result.Content.Contains(TEXT("[Unconnected]")));
		});
	});

	Describe("GUID + position format", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
	{
		It("output lines have trailing [ID] (x,y) comments", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppGuid"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);
			FString BeginPlayId = FindBeginPlayGuid(BP);
			if (!TestFalse("BeginPlay found", BeginPlayId.IsEmpty())) return;

			// Add a connected PrintString
			FString PrintId = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 200, 0,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			if (!TestFalse("PrintString added", PrintId.IsEmpty())) return;
			Connect(BPPath, BeginPlayId, TEXT("then"), PrintId, TEXT("execute"));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);

			// Verify trailing comment pattern: // [XXXX] (x,y)
			TestTrue("has compact GUID pattern // [",
				Result.Content.Contains(TEXT("// [")));

			// Check for position coordinate pattern (x,y) — at least one occurrence
			bool bFoundPosition = false;
			TArray<FString> Lines;
			Result.Content.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				if (Line.Contains(TEXT("// [")) && Line.Contains(TEXT("(")))
				{
					// Look for pattern like (200,0) or similar
					int32 ParenIdx = Line.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (ParenIdx != INDEX_NONE && Line.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, ParenIdx) != INDEX_NONE)
					{
						bFoundPosition = true;
						break;
					}
				}
			}
			TestTrue("at least one line has position (x,y) in trailing comment", bFoundPosition);
		});
	});

	Describe("graph not found", [this, GenerateCpp]()
	{
		It("non-existent graph returns error listing available graphs", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppNotFound"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = GenerateCpp(
				FMCPToolDirectTestHelper::GetAssetPath(BP), TEXT("NonExistentGraph"));

			// The error is embedded in content (not bIsError since it returns the error string)
			TestTrue("content mentions Error",
				Result.Content.Contains(TEXT("Error")));
			TestTrue("content mentions not found",
				Result.Content.Contains(TEXT("not found")));
			TestTrue("content lists EventGraph as available",
				Result.Content.Contains(TEXT("EventGraph")));
		});
	});

	Describe("type correctness", [this, GenerateCpp]()
	{
		It("variables of different types produce correct C++ type names", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppTypes"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add variables of different types
			auto AddVar = [&](const FString& Name, const FString& Type)
			{
				GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"%s","var_type":"%s"})"),
						*BPPath, *Name, *Type)));
			};
			AddVar(TEXT("MyBool"), TEXT("bool"));
			AddVar(TEXT("MyInt"), TEXT("int"));
			AddVar(TEXT("MyFloat"), TEXT("float"));
			AddVar(TEXT("MyString"), TEXT("string"));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains bool type",
				Result.Content.Contains(TEXT("bool ")));
			TestTrue("contains int32 type",
				Result.Content.Contains(TEXT("int32 ")));
			TestTrue("contains float type",
				Result.Content.Contains(TEXT("float ")));
			TestTrue("contains FString type",
				Result.Content.Contains(TEXT("FString ")));
		});
	});

	Describe("sequence node", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
	{
		It("sequence produces labeled sequential blocks", [this, AddNode, Connect, GenerateCpp, FindBeginPlayGuid]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppSequence"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);
			FString BeginPlayId = FindBeginPlayGuid(BP);
			if (!TestFalse("BeginPlay found", BeginPlayId.IsEmpty())) return;

			// Add Sequence node
			FString SeqId = AddNode(BPPath, TEXT("EventGraph"), TEXT("Sequence"), 200, 0);
			if (!TestFalse("Sequence node added", SeqId.IsEmpty())) return;

			// Add two PrintString nodes
			FString Print1Id = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 400, -100,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			FString Print2Id = AddNode(BPPath, TEXT("EventGraph"), TEXT("CallFunction"), 400, 100,
				TEXT(R"("properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"})"));
			if (!TestFalse("Print1 added", Print1Id.IsEmpty())) return;
			if (!TestFalse("Print2 added", Print2Id.IsEmpty())) return;

			// Connect: BeginPlay → Sequence, Sequence.Then_0 → Print1, Sequence.Then_1 → Print2
			Connect(BPPath, BeginPlayId, TEXT("then"), SeqId, TEXT("execute"));
			Connect(BPPath, SeqId, TEXT("then_0"), Print1Id, TEXT("execute"));
			Connect(BPPath, SeqId, TEXT("then_1"), Print2Id, TEXT("execute"));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains Sequence separator",
				Result.Content.Contains(TEXT("Sequence")));
			TestTrue("contains Seq 0 label",
				Result.Content.Contains(TEXT("[Seq 0]")));
			TestTrue("contains Seq 1 label",
				Result.Content.Contains(TEXT("[Seq 1]")));
		});
	});

	Describe("all-graphs mode", [this, GenerateCpp]()
	{
		It("emits all graphs when no graph specified", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppAllGraphs"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a function graph
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_function","target":"%s","name":"TestFunc"})"), *BPPath)));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains EventGraph",
				Result.Content.Contains(TEXT("// Graph: EventGraph")));
			TestTrue("contains TestFunc",
				Result.Content.Contains(TEXT("// Graph: TestFunc")));

			// EventGraph should appear before TestFunc (UbergraphPages first)
			int32 EGIdx = Result.Content.Find(TEXT("// Graph: EventGraph"));
			int32 TFIdx = Result.Content.Find(TEXT("// Graph: TestFunc"));
			TestTrue("EventGraph before TestFunc", EGIdx < TFIdx);
		});
	});

	Describe("local variables", [this, GenerateCpp]()
	{
		It("shows local variables for function graphs", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppLocalVars"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a function graph
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_function","target":"%s","name":"MyFunc"})"), *BPPath)));

			// Programmatically add a local variable to the function entry node
			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == TEXT("MyFunc"))
				{
					FuncGraph = G;
					break;
				}
			}
			if (!TestNotNull("function graph found", FuncGraph)) return;

			for (UEdGraphNode* Node : FuncGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					FBPVariableDescription LocalVar;
					LocalVar.VarName = FName(TEXT("LocalHealth"));
					LocalVar.VarGuid = FGuid::NewGuid();
					LocalVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
					LocalVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
					Entry->LocalVariables.Add(LocalVar);
					break;
				}
			}

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains Local Variables header",
				Result.Content.Contains(TEXT("// Local Variables:")));
			TestTrue("contains LocalHealth",
				Result.Content.Contains(TEXT("LocalHealth")));
			TestTrue("contains float type",
				Result.Content.Contains(TEXT("float LocalHealth")));
		});
	});

	Describe("graph path format", [this, GenerateCpp]()
	{
		It("includes full path in graph header", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppGraphPath"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = GenerateCpp(FMCPToolDirectTestHelper::GetAssetPath(BP));
			TestFalse("not error", Result.bIsError);
			TestTrue("graph header has opening paren after name",
				Result.Content.Contains(TEXT("// Graph: EventGraph (")));
			TestTrue("graph header has path::name with closing paren",
				Result.Content.Contains(TEXT("::EventGraph)")));
		});
	});

	Describe("member variable CDO default", [this, GenerateCpp]()
	{
		It("shows CDO default value for member variable", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppCDODefault"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a float variable
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"Speed","var_type":"float"})"), *BPPath)));

			// Compile so GeneratedClass + CDO exist
			FKismetEditorUtilities::CompileBlueprint(BP);

			// Set CDO value to 42
			if (BP->GeneratedClass)
			{
				UObject* CDO = BP->GeneratedClass->GetDefaultObject(true);
				if (CDO)
				{
					FProperty* Prop = BP->GeneratedClass->FindPropertyByName(FName(TEXT("Speed")));
					if (Prop)
					{
						void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
						FString ImportStr = TEXT("42.0");
						Prop->ImportText_Direct(*ImportStr, ValuePtr, CDO, PPF_None);
					}
				}
			}

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains Speed with default 42",
				Result.Content.Contains(TEXT("Speed = 42")));
		});
	});

	Describe("member variable description default", [this, GenerateCpp]()
	{
		It("shows FBPVariableDescription::DefaultValue for member variable", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppDescDefault"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a float variable with a default value via the description
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"Damage","var_type":"float","default_value":"25"})"), *BPPath)));

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains Damage with default 25",
				Result.Content.Contains(TEXT("Damage = 25")));
		});
	});

	Describe("local variable default", [this, GenerateCpp]()
	{
		It("shows default value for local variable", [this, GenerateCpp]()
		{
			if (!TestNotNull("inspect tool", InspectTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_CppLocalDefault"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a function graph
			GraphTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_function","target":"%s","name":"TestFunc"})"), *BPPath)));

			// Add a local variable with default
			UEdGraph* FuncGraph = nullptr;
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (G && G->GetName() == TEXT("TestFunc"))
				{
					FuncGraph = G;
					break;
				}
			}
			if (!TestNotNull("function graph found", FuncGraph)) return;

			for (UEdGraphNode* Node : FuncGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					FBPVariableDescription LocalVar;
					LocalVar.VarName = FName(TEXT("LocalSpeed"));
					LocalVar.VarGuid = FGuid::NewGuid();
					LocalVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
					LocalVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
					LocalVar.DefaultValue = TEXT("99");
					Entry->LocalVariables.Add(LocalVar);
					break;
				}
			}

			FMCPToolResult Result = GenerateCpp(BPPath);
			TestFalse("not error", Result.bIsError);
			TestTrue("contains LocalSpeed with default 99",
				Result.Content.Contains(TEXT("LocalSpeed = 99")));
		});
	});
}
