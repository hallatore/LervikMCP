#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/Material.h"
#include "MaterialExpressionIO.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"

BEGIN_DEFINE_SPEC(FMCPTool_InspectDirectSpec, "Plugins.LervikMCP.Integration.Tools.Inspect.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* InspectTool = nullptr;
END_DEFINE_SPEC(FMCPTool_InspectDirectSpec)

void FMCPTool_InspectDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		InspectTool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
	});

	AfterEach([this]()
	{
		InspectTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("inspect tool is registered", [this]()
		{
			TestNotNull("inspect tool found", InspectTool);
		});
	});

	Describe("missing type parameter", [this]()
	{
		It("returns error when type is not provided", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			AActor* Actor = Helper.SpawnTransientActor(AActor::StaticClass());
			if (!TestNotNull("actor spawned", Actor)) return;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), Actor->GetActorLabel() }
				})
			);

			TestTrue("result is an error", Result.bIsError);
			TestTrue("error mentions 'type'",
				Result.Content.Contains(TEXT("type")));
		});
	});

	Describe("type=components for actor", [this]()
	{
		It("returns components array for a spawned actor", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			AActor* Actor = Helper.SpawnTransientActor(AActor::StaticClass());
			if (!TestNotNull("actor spawned", Actor)) return;

			FString ActorLabel = Actor->GetActorLabel();

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), ActorLabel },
					{ TEXT("type"),   TEXT("components") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			TestTrue("result has 'components' array", Json->TryGetArrayField(TEXT("components"), Components));
		});
	});

	Describe("type=pins for material expression", [this]()
	{
		It("returns input and output pins for a UMaterialExpressionAdd node", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_InspectPins"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass());
			if (!TestNotNull("expression added", Expr)) return;

			TestTrue("expression has valid GUID", Expr->MaterialExpressionGuid.IsValid());

			FString Target = FString::Printf(TEXT("%s::%s"),
				*FMCPToolDirectTestHelper::GetAssetPath(Mat),
				*Expr->MaterialExpressionGuid.ToString());

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), Target },
					{ TEXT("type"),   TEXT("pins") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			TestTrue("result has 'pins' array", Json->TryGetArrayField(TEXT("pins"), Pins));
			if (!Pins) return;

			TestTrue("has at least one pin", Pins->Num() > 0);

			bool bFoundInput  = false;
			bool bFoundOutput = false;
			for (const TSharedPtr<FJsonValue>& PinVal : *Pins)
			{
				TSharedPtr<FJsonObject> PinObj = PinVal->AsObject();
				if (!PinObj.IsValid()) continue;

				FString Dir;
				if (PinObj->TryGetStringField(TEXT("direction"), Dir))
				{
					if (Dir == TEXT("input"))  bFoundInput  = true;
					if (Dir == TEXT("output")) bFoundOutput = true;
				}
			}

			TestTrue("pins include at least one input",  bFoundInput);
			TestTrue("pins include at least one output", bFoundOutput);
		});
	});

	Describe("type=expressions / type=nodes response key", [this]()
	{
		It("type=expressions returns key 'expressions' not 'nodes'", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ExpressionsKey"));
			if (!TestNotNull("material created", Mat)) return;

			Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("expressions") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Expressions = nullptr;
			TestTrue("result has 'expressions' array", Json->TryGetArrayField(TEXT("expressions"), Expressions));
			TestFalse("result does NOT have 'nodes' key when type=expressions",
				Json->HasField(TEXT("nodes")));
		});

		It("type=nodes returns key 'nodes'", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_NodesKey"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(BP) },
					{ TEXT("type"),   TEXT("nodes") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			TestTrue("result has 'nodes' array", Json->TryGetArrayField(TEXT("nodes"), Nodes));
		});
	});

	Describe("type=nodes node width and height fields", [this]()
	{
		It("each node entry has width and height fields > 0", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_NodeWidthHeight"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(BP) },
					{ TEXT("type"),   TEXT("nodes") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!TestTrue("result has 'nodes' array", Json->TryGetArrayField(TEXT("nodes"), Nodes))) return;

			for (const TSharedPtr<FJsonValue>& NodeVal : *Nodes)
			{
				TSharedPtr<FJsonObject> NodeObj = NodeVal->AsObject();
				if (!TestTrue("node entry is a JSON object", NodeObj.IsValid())) continue;

				double Width = -1.0, Height = -1.0;
				TestTrue("node has 'width' field",  NodeObj->TryGetNumberField(TEXT("width"),  Width));
				TestTrue("node has 'height' field", NodeObj->TryGetNumberField(TEXT("height"), Height));
				TestTrue("width > 0",  Width  > 0.0);
				TestTrue("height > 0", Height > 0.0);
			}
		});
	});

	Describe("type=expressions expression width and height fields", [this]()
	{
		It("each expression entry has width and height fields > 0", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ExprWidthHeight"));
			if (!TestNotNull("material created", Mat)) return;

			Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("expressions") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Expressions = nullptr;
			if (!TestTrue("result has 'expressions' array", Json->TryGetArrayField(TEXT("expressions"), Expressions))) return;

			for (const TSharedPtr<FJsonValue>& ExprVal : *Expressions)
			{
				TSharedPtr<FJsonObject> ExprObj = ExprVal->AsObject();
				if (!TestTrue("expression entry is a JSON object", ExprObj.IsValid())) continue;

				double Width = -1.0, Height = -1.0;
				TestTrue("expression has 'width' field",  ExprObj->TryGetNumberField(TEXT("width"),  Width));
				TestTrue("expression has 'height' field", ExprObj->TryGetNumberField(TEXT("height"), Height));
				TestTrue("width > 0",  Width  > 0.0);
				TestTrue("height > 0", Height > 0.0);
			}
		});
	});

	Describe("type=expressions with connections", [this]()
	{
		It("Add expression has connections array when Constant is connected to input A", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ExprConnections"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstExprBase = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			UMaterialExpression* AddExprBase   = Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass());
			if (!TestNotNull("constant expression added", ConstExprBase)) return;
			if (!TestNotNull("add expression added",      AddExprBase))   return;

			UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(AddExprBase);
			if (!TestNotNull("add expression cast succeeded", AddExpr)) return;

			// Connect Constant -> Add.A
			AddExpr->A.Expression  = ConstExprBase;
			AddExpr->A.OutputIndex = 0;

			FString ConstGuid = ConstExprBase->MaterialExpressionGuid.ToString();

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("expressions") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Expressions = nullptr;
			if (!TestTrue("result has 'expressions' array", Json->TryGetArrayField(TEXT("expressions"), Expressions))) return;

			bool bFoundAddWithConnection = false;
			for (const TSharedPtr<FJsonValue>& ExprVal : *Expressions)
			{
				TSharedPtr<FJsonObject> ExprObj = ExprVal->AsObject();
				if (!ExprObj.IsValid()) continue;

				FString ClassName;
				ExprObj->TryGetStringField(TEXT("class"), ClassName);
				if (ClassName != TEXT("MaterialExpressionAdd")) continue;

				const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
				if (!ExprObj->TryGetArrayField(TEXT("connections"), Connections)) continue;

				for (const TSharedPtr<FJsonValue>& ConnVal : *Connections)
				{
					TSharedPtr<FJsonObject> ConnObj = ConnVal->AsObject();
					if (!ConnObj.IsValid()) continue;
					FString FromNode;
					ConnObj->TryGetStringField(TEXT("from_node"), FromNode);
					if (FromNode == ConstGuid)
					{
						bFoundAddWithConnection = true;
						break;
					}
				}
				break;
			}
			TestTrue("Add expression has connection from Constant in 'connections' array", bFoundAddWithConnection);
		});
	});

	Describe("type=pins with connected_to", [this]()
	{
		It("Add input pin 'A' has connected_to field when Constant is connected", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_PinsConnectedTo"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstExprBase = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			UMaterialExpression* AddExprBase   = Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass());
			if (!TestNotNull("constant expression added", ConstExprBase)) return;
			if (!TestNotNull("add expression added",      AddExprBase))   return;

			UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(AddExprBase);
			if (!TestNotNull("add expression cast succeeded", AddExpr)) return;

			// Connect Constant -> Add.A
			AddExpr->A.Expression  = ConstExprBase;
			AddExpr->A.OutputIndex = 0;

			FString ConstGuid = ConstExprBase->MaterialExpressionGuid.ToString();

			FString Target = FString::Printf(TEXT("%s::%s"),
				*FMCPToolDirectTestHelper::GetAssetPath(Mat),
				*AddExprBase->MaterialExpressionGuid.ToString());

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), Target },
					{ TEXT("type"),   TEXT("pins") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!TestTrue("result has 'pins' array", Json->TryGetArrayField(TEXT("pins"), Pins))) return;

			bool bFoundConnectedTo = false;
			for (const TSharedPtr<FJsonValue>& PinVal : *Pins)
			{
				TSharedPtr<FJsonObject> PinObj = PinVal->AsObject();
				if (!PinObj.IsValid()) continue;

				FString PinName, Direction;
				PinObj->TryGetStringField(TEXT("name"),      PinName);
				PinObj->TryGetStringField(TEXT("direction"), Direction);

				if (Direction != TEXT("input") || PinName != TEXT("A")) continue;

				const TSharedPtr<FJsonObject>* ConnTo = nullptr;
				if (!PinObj->TryGetObjectField(TEXT("connected_to"), ConnTo)) break;

				FString NodeGuid;
				(*ConnTo)->TryGetStringField(TEXT("from_node"), NodeGuid);
				if (NodeGuid == ConstGuid)
				{
					bFoundConnectedTo = true;
				}
				break;
			}
			TestTrue("Add pin 'A' has connected_to pointing to Constant's GUID", bFoundConnectedTo);
		});
	});

	Describe("type=connections for material expression-to-expression", [this]()
	{
		It("returns edge from Constant to Add expression", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_Connections_E2E"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstExpr    = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			UMaterialExpression* AddExprBase  = Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass());
			if (!TestNotNull("constant expression added", ConstExpr))   return;
			if (!TestNotNull("add expression added",      AddExprBase)) return;

			UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(AddExprBase);
			if (!TestNotNull("add expression cast succeeded", AddExpr)) return;

			AddExpr->A.Expression  = ConstExpr;
			AddExpr->A.OutputIndex = 0;

			FString ConstGuid = ConstExpr->MaterialExpressionGuid.ToString();
			FString AddGuid   = AddExprBase->MaterialExpressionGuid.ToString();

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("connections") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (!TestTrue("result has 'connections' array", Json->TryGetArrayField(TEXT("connections"), Connections))) return;

			bool bFoundEdge = false;
			for (const TSharedPtr<FJsonValue>& ConnVal : *Connections)
			{
				TSharedPtr<FJsonObject> ConnObj = ConnVal->AsObject();
				if (!ConnObj.IsValid()) continue;

				FString FromNode, ToNode, ToPin;
				ConnObj->TryGetStringField(TEXT("from_node"), FromNode);
				ConnObj->TryGetStringField(TEXT("to_node"),   ToNode);
				ConnObj->TryGetStringField(TEXT("to_pin"),    ToPin);

				if (FromNode == ConstGuid && ToNode == AddGuid && ToPin == TEXT("A"))
				{
					bFoundEdge = true;
					break;
				}
			}
			TestTrue("connections contain Constant->Add.A edge", bFoundEdge);
		});
	});

	Describe("type=connections for material expression-to-property", [this]()
	{
		It("returns edge from Constant to BaseColor property", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_Connections_E2P"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstExpr = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			if (!TestNotNull("constant expression added", ConstExpr)) return;

			FExpressionInput* BaseColorInput = Mat->GetExpressionInputForProperty(MP_BaseColor);
			if (!TestNotNull("BaseColor property input found", BaseColorInput)) return;

			BaseColorInput->Expression  = ConstExpr;
			BaseColorInput->OutputIndex = 0;

			FString ConstGuid = ConstExpr->MaterialExpressionGuid.ToString();

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("connections") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (!TestTrue("result has 'connections' array", Json->TryGetArrayField(TEXT("connections"), Connections))) return;

			bool bFoundEdge = false;
			for (const TSharedPtr<FJsonValue>& ConnVal : *Connections)
			{
				TSharedPtr<FJsonObject> ConnObj = ConnVal->AsObject();
				if (!ConnObj.IsValid()) continue;

				FString FromNode, ToProperty;
				ConnObj->TryGetStringField(TEXT("from_node"),   FromNode);
				ConnObj->TryGetStringField(TEXT("to_property"), ToProperty);

				if (FromNode == ConstGuid && ToProperty == TEXT("BaseColor"))
				{
					bFoundEdge = true;
					break;
				}
			}
			TestTrue("connections contain Constant->BaseColor edge", bFoundEdge);
		});
	});

	Describe("type=connections filter for material expression-to-expression", [this]()
	{
		It("filter=A returns only the A input connection", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ConnFilter_E2E"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstA    = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			UMaterialExpression* ConstB    = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			UMaterialExpression* AddExprBase = Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass());
			if (!TestNotNull("ConstA added", ConstA)) return;
			if (!TestNotNull("ConstB added", ConstB)) return;
			if (!TestNotNull("Add added",    AddExprBase)) return;

			UMaterialExpressionAdd* AddExpr = Cast<UMaterialExpressionAdd>(AddExprBase);
			if (!TestNotNull("add cast succeeded", AddExpr)) return;

			AddExpr->A.Expression = ConstA;
			AddExpr->A.OutputIndex = 0;
			AddExpr->B.Expression = ConstB;
			AddExpr->B.OutputIndex = 0;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("connections") },
					{ TEXT("filter"), TEXT("A") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
			if (!TestTrue("result has 'connections' array", Json->TryGetArrayField(TEXT("connections"), Connections))) return;

			TestEqual("filter=A yields exactly 1 connection", Connections->Num(), 1);

			if (Connections->Num() == 1)
			{
				TSharedPtr<FJsonObject> ConnObj = (*Connections)[0]->AsObject();
				FString ToPin;
				ConnObj->TryGetStringField(TEXT("to_pin"), ToPin);
				TestEqual("connection to_pin is A", ToPin, FString(TEXT("A")));
			}
		});
	});

	Describe("type=connections filter for material expression-to-property", [this]()
	{
		It("filter=BaseColor returns 1 connection, filter=NoMatch returns 0", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ConnFilter_E2P"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* ConstExpr = Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass());
			if (!TestNotNull("constant expression added", ConstExpr)) return;

			FExpressionInput* BaseColorInput = Mat->GetExpressionInputForProperty(MP_BaseColor);
			if (!TestNotNull("BaseColor property input found", BaseColorInput)) return;

			BaseColorInput->Expression  = ConstExpr;
			BaseColorInput->OutputIndex = 0;

			// filter=BaseColor → 1 result
			FMCPToolResult Result1 = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("connections") },
					{ TEXT("filter"), TEXT("BaseColor") }
				})
			);
			TestFalse("result1 is not an error", Result1.bIsError);
			TSharedPtr<FJsonObject> Json1 = FMCPToolDirectTestHelper::ParseResultJson(Result1);
			if (!TestNotNull("result1 parses as JSON", Json1.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* Connections1 = nullptr;
			if (!TestTrue("result1 has 'connections' array", Json1->TryGetArrayField(TEXT("connections"), Connections1))) return;
			TestEqual("filter=BaseColor yields 1 connection", Connections1->Num(), 1);

			// filter=NoMatch → 0 results
			FMCPToolResult Result2 = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"),   TEXT("connections") },
					{ TEXT("filter"), TEXT("NoMatch") }
				})
			);
			TestFalse("result2 is not an error", Result2.bIsError);
			TSharedPtr<FJsonObject> Json2 = FMCPToolDirectTestHelper::ParseResultJson(Result2);
			if (!TestNotNull("result2 parses as JSON", Json2.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* Connections2 = nullptr;
			Json2->TryGetArrayField(TEXT("connections"), Connections2);
			int32 Count2 = Connections2 ? Connections2->Num() : 0;
			TestEqual("filter=NoMatch yields 0 connections", Count2, 0);
		});
	});

	Describe("type=components for Blueprint", [this]()
	{
		It("returns SCS components array for a Blueprint with a component", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			IMCPTool* GraphTool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool found", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_InspectComponents"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_component","target":"%s","component_class":"StaticMeshComponent","name":"TestMesh"})"),
						*AssetPath)));
			if (!TestFalse("add_component succeeded", AddResult.bIsError)) return;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), AssetPath },
					{ TEXT("type"),   TEXT("components") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			if (!TestTrue("result has 'components' array", Json->TryGetArrayField(TEXT("components"), Components))) return;

			TestTrue("components array has at least 1 entry", Components->Num() >= 1);

			for (const TSharedPtr<FJsonValue>& CompVal : *Components)
			{
				TSharedPtr<FJsonObject> CompObj = CompVal->AsObject();
				if (!TestTrue("component entry is a JSON object", CompObj.IsValid())) continue;

				FString Name, Class;
				TestTrue("component has 'name' field",  CompObj->TryGetStringField(TEXT("name"),  Name));
				TestTrue("component has 'class' field", CompObj->TryGetStringField(TEXT("class"), Class));
			}
		});

		It("Blueprint with no added components returns components array", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBP_NoComponents"));
			if (!TestNotNull("blueprint created", BP)) return;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(BP) },
					{ TEXT("type"),   TEXT("components") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			TestTrue("result has 'components' array", Json->TryGetArrayField(TEXT("components"), Components));
		});
	});

	Describe("type=hlsl dangling nodes", [this]()
	{
		It("includes unconnected expressions in Dangling section", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_HlslDangling"));
			if (!TestNotNull("material created", Mat)) return;

			// Connected constant -> BaseColor
			auto* Connected = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("connected expr", Connected)) return;
			Connected->R = 0.5f;

			FExpressionInput* BaseColorInput = Mat->GetExpressionInputForProperty(MP_BaseColor);
			if (!TestNotNull("BaseColor input", BaseColorInput)) return;
			BaseColorInput->Expression = Connected;
			BaseColorInput->OutputIndex = 0;

			// Dangling constant (not connected)
			auto* Dangling = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("dangling expr", Dangling)) return;
			Dangling->R = 0.99f;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"), TEXT("hlsl") }
				})
			);

			TestFalse("not error", Result.bIsError);
			TestTrue("contains Dangling section header",
				Result.Content.Contains(TEXT("Dangling")));
			TestTrue("contains dangling constant value 0.99",
				Result.Content.Contains(TEXT("0.99")));
		});

		It("does not emit Dangling section when all nodes are connected", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_HlslAllConnected"));
			if (!TestNotNull("material created", Mat)) return;

			auto* C = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("expr", C)) return;
			C->R = 1.0f;

			FExpressionInput* BaseColorInput = Mat->GetExpressionInputForProperty(MP_BaseColor);
			if (!TestNotNull("BaseColor input", BaseColorInput)) return;
			BaseColorInput->Expression = C;
			BaseColorInput->OutputIndex = 0;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"), TEXT("hlsl") }
				})
			);

			TestFalse("not error", Result.bIsError);
			TestFalse("no Dangling section",
				Result.Content.Contains(TEXT("Dangling")));
		});

		It("dangling node referencing connected node emits connected var name", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_HlslDanglingCrossRef"));
			if (!TestNotNull("material created", Mat)) return;

			// Connected constant -> BaseColor
			auto* ConnectedConst = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("connected const", ConnectedConst)) return;
			ConnectedConst->R = 0.5f;

			FExpressionInput* BaseColorInput = Mat->GetExpressionInputForProperty(MP_BaseColor);
			if (!TestNotNull("BaseColor input", BaseColorInput)) return;
			BaseColorInput->Expression = ConnectedConst;
			BaseColorInput->OutputIndex = 0;

			// Dangling Add node: input A = ConnectedConst, input B = new dangling constant
			auto* DanglingAdd = Cast<UMaterialExpressionAdd>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionAdd::StaticClass()));
			if (!TestNotNull("dangling add", DanglingAdd)) return;

			auto* DanglingConst = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("dangling const", DanglingConst)) return;
			DanglingConst->R = 0.77f;

			// Wire: DanglingAdd.A = ConnectedConst, DanglingAdd.B = DanglingConst
			FExpressionInput* AddInputA = FMCPGraphHelpers::GetExpressionInput(DanglingAdd, 0);
			if (!TestNotNull("add input A", AddInputA)) return;
			AddInputA->Expression = ConnectedConst;
			AddInputA->OutputIndex = 0;

			FExpressionInput* AddInputB = FMCPGraphHelpers::GetExpressionInput(DanglingAdd, 1);
			if (!TestNotNull("add input B", AddInputB)) return;
			AddInputB->Expression = DanglingConst;
			AddInputB->OutputIndex = 0;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"), TEXT("hlsl") }
				})
			);

			TestFalse("not error", Result.bIsError);

			FString ConnectedVarName = FString::Printf(TEXT("Constant_%s"),
				*FMCPJsonHelpers::GuidToCompact(ConnectedConst->MaterialExpressionGuid));

			TestTrue("dangling section references connected var name",
				Result.Content.Contains(ConnectedVarName));

			FString AddVarName = FString::Printf(TEXT("Add_%s"),
				*FMCPJsonHelpers::GuidToCompact(DanglingAdd->MaterialExpressionGuid));
			FString ExpectedAddLine = FString::Printf(TEXT("auto %s = %s + "), *AddVarName, *ConnectedVarName);
			TestTrue("Add expression references connected constant by var name",
				Result.Content.Contains(ExpectedAddLine));
		});

		It("emits dangling nodes even when no nodes are connected to outputs", [this]()
		{
			if (!TestNotNull("inspect tool found", InspectTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_HlslOnlyDangling"));
			if (!TestNotNull("material created", Mat)) return;

			auto* D = Cast<UMaterialExpressionConstant>(
				Helper.AddMaterialExpression(Mat, UMaterialExpressionConstant::StaticClass()));
			if (!TestNotNull("expr", D)) return;
			D->R = 0.42f;

			FMCPToolResult Result = InspectTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat) },
					{ TEXT("type"), TEXT("hlsl") }
				})
			);

			TestFalse("not error", Result.bIsError);
			TestTrue("contains Dangling section",
				Result.Content.Contains(TEXT("Dangling")));
			TestTrue("contains dangling value 0.42",
				Result.Content.Contains(TEXT("0.42")));
		});
	});
}
