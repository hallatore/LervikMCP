#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

BEGIN_DEFINE_SPEC(FMCPTool_GraphDirectSpec, "Plugins.LervikMCP.Integration.Tools.Graph.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* GraphTool = nullptr;
END_DEFINE_SPEC(FMCPTool_GraphDirectSpec)

void FMCPTool_GraphDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		GraphTool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
	});

	AfterEach([this]()
	{
		GraphTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("graph tool is registered", [this]()
		{
			TestNotNull("graph tool found", GraphTool);
		});
	});

	Describe("material add_node", [this]()
	{
		It("returns inputs and outputs arrays for Multiply expression", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMultiplyPins"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Multiply","pos_x":0,"pos_y":0})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
			TestTrue("has inputs array", Json->TryGetArrayField(TEXT("inputs"), Inputs));
			TestTrue("has outputs array", Json->TryGetArrayField(TEXT("outputs"), Outputs));

			if (Inputs) TestEqual("Multiply has 2 inputs", Inputs->Num(), 2);
			if (Outputs) TestTrue("Multiply has at least 1 output", Outputs->Num() >= 1);

			if (Inputs && Inputs->Num() >= 2)
			{
				FString NameA, NameB;
				(*Inputs)[0]->AsObject()->TryGetStringField(TEXT("name"), NameA);
				(*Inputs)[1]->AsObject()->TryGetStringField(TEXT("name"), NameB);
				TestEqual("first input is A", NameA, FString(TEXT("A")));
				TestEqual("second input is B", NameB, FString(TEXT("B")));

				FString Dir;
				(*Inputs)[0]->AsObject()->TryGetStringField(TEXT("direction"), Dir);
				TestEqual("input direction", Dir, FString(TEXT("input")));
			}

			if (Outputs && Outputs->Num() >= 1)
			{
				FString OutName;
				(*Outputs)[0]->AsObject()->TryGetStringField(TEXT("name"), OutName);
				TestEqual("first output name is empty string (unnamed default output)", OutName, FString(TEXT("")));
			}
		});
	});

	Describe("connect error reporting", [this]()
	{
		It("invalid source node GUID produces an errors array entry", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectErrors"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "BADGUID-0000-0000-0000-000000000000", "pin": "execute"},
					"dest":   {"node": "BADGUID-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when all connections fail", Result.bIsError);
			TestTrue("error text mentions source node",
				Result.Content.Contains(TEXT("Source"), ESearchCase::IgnoreCase) ||
				Result.Content.Contains(TEXT("not found"), ESearchCase::IgnoreCase));
		});
	});

	Describe("disconnect error reporting", [this]()
	{
		It("invalid source/dest node GUIDs produce an errors array entry", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestDisconnectErrors"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "BADGUID-0000-0000-0000-000000000000", "pin": "execute"},
					"dest":   {"node": "BADGUID-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when all disconnections fail", Result.bIsError);
			TestTrue("error text mentions node not found",
				Result.Content.Contains(TEXT("not found"), ESearchCase::IgnoreCase));
		});
	});

	Describe("connect/disconnect missing keys error reporting", [this]()
	{
		It("connect with missing source key reports error with found keys", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectMissingSource"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"from": {"node": "00000000-0000-0000-0000-000000000000", "pin": "exec"},
					"dest":  {"node": "00000000-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("mentions missing 'source'", Result.Content.Contains(TEXT("'source'"), ESearchCase::IgnoreCase));
			TestTrue("mentions found key 'from'", Result.Content.Contains(TEXT("'from'"), ESearchCase::IgnoreCase));
		});

		It("connect with missing dest key reports error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectMissingDest"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "00000000-0000-0000-0000-000000000000", "pin": "exec"},
					"to":     {"node": "00000000-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("mentions missing 'dest'", Result.Content.Contains(TEXT("'dest'"), ESearchCase::IgnoreCase));
		});

		It("connect with non-object connection item reports error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectNonObject"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"connections": ["not_an_object"]
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("mentions not a valid JSON object", Result.Content.Contains(TEXT("not a valid JSON object"), ESearchCase::IgnoreCase));
		});

		It("disconnect with missing source key reports error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestDisconnectMissingSource"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"from": {"node": "00000000-0000-0000-0000-000000000000", "pin": "exec"},
					"dest":  {"node": "00000000-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("mentions missing 'source'", Result.Content.Contains(TEXT("'source'"), ESearchCase::IgnoreCase));
		});
	});

	Describe("invalid action error message", [this]()
	{
		It("lists valid actions in the error message", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestInvalidAction"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"not_a_real_action","target":"%s"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is an error", Result.bIsError);

			FString Content = Result.Content;
			TestTrue("error lists 'add_node'", Content.Contains(TEXT("add_node")));
			TestTrue("error lists 'connect'",  Content.Contains(TEXT("connect")));
		});
	});

	Describe("material edit_node", [this]()
	{
		It("edits a material expression property", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeScalar"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionScalarParameter::StaticClass());
			if (!TestNotNull("expression created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"ParameterName":"TestParam"}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Modified = nullptr;
			TestTrue("has modified array", Json->TryGetArrayField(TEXT("modified"), Modified));
			if (Modified && Modified->Num() >= 1)
			{
				FString FirstMod;
				(*Modified)[0]->TryGetString(FirstMod);
				TestEqual("modified contains expression GUID", FirstMod, GuidStr);
			}
		});

		It("edits a material expression using node_id key (from add_node response)", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeById"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionScalarParameter::StaticClass());
			if (!TestNotNull("expression created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node_id":"%s","properties":{"ParameterName":"ByIdParam"}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Modified = nullptr;
			TestTrue("has modified array", Json->TryGetArrayField(TEXT("modified"), Modified));
			if (Modified && Modified->Num() >= 1)
			{
				FString FirstMod;
				(*Modified)[0]->TryGetString(FirstMod);
				TestEqual("modified contains expression GUID", FirstMod, GuidStr);
			}
		});

		It("edit_node with pos sets MaterialExpressionEditorX and Y", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodePos"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			if (!TestNotNull("expression created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","pos_x":200,"pos_y":300})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TestEqual("MaterialExpressionEditorX is 200", Expr->MaterialExpressionEditorX, 200);
			TestEqual("MaterialExpressionEditorY is 300", Expr->MaterialExpressionEditorY, 300);
		});

		It("reports unknown expression property in warnings", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeUnknownProp"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionScalarParameter::StaticClass());
			if (!TestNotNull("expression created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"NonExistentProp":"value"}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not a hard error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("response has warnings for unknown property",
				Json->TryGetArrayField(TEXT("warnings"), Warnings));
		});
	});

	Describe("connect happy path", [this]()
	{
		It("connects two material expression pins", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestConnectHappy"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid      = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"node": "%s", "pin": "A"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *MultiplyGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connected = nullptr;
			TestTrue("has connected array", Json->TryGetArrayField(TEXT("connected"), Connected));
			if (Connected) TestEqual("connected has 1 entry", Connected->Num(), 1);

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 1", Count, 1);
		});

		It("connects Constant3Vector to SubsurfaceColor material property", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestConnectSubsurfaceColor"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Vec = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionConstant3Vector::StaticClass(), 0, 0);
			if (!TestNotNull("Constant3Vector expression", Vec)) return;

			FString VecGuid = Vec->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"property": "SubsurfaceColor"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *VecGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connected = nullptr;
			TestTrue("has connected array", Json->TryGetArrayField(TEXT("connected"), Connected));
			if (Connected) TestEqual("connected has 1 entry", Connected->Num(), 1);
		});
	});

	Describe("disconnect happy path", [this]()
	{
		It("disconnects two material expression pins", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectHappy"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid      = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AssetPath    = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			// First connect them
			TSharedPtr<FJsonObject> ConnParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"node": "%s", "pin": "A"}
				})"),
					*AssetPath, *MultiplyGuid, *AddGuid));
			FMCPToolResult ConnResult = GraphTool->Execute(ConnParams);
			TestFalse("connect setup succeeded", ConnResult.bIsError);

			// Now disconnect
			TSharedPtr<FJsonObject> DisconParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"node": "%s", "pin": "A"}
				})"),
					*AssetPath, *MultiplyGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(DisconParams);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Disconnected = nullptr;
			TestTrue("has disconnected array", Json->TryGetArrayField(TEXT("disconnected"), Disconnected));
			if (Disconnected) TestEqual("disconnected has 1 entry", Disconnected->Num(), 1);

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 1", Count, 1);
		});
	});

	Describe("blueprint add_node CallFunction", [this]()
	{
		It("CallFunction with FunctionName in properties creates node with pins", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestCallFunctionProps"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "add_node",
					"target": "%s",
					"graph": "EventGraph",
					"node_class": "CallFunction",
					"pos_x": 0, "pos_y": 0,
					"properties": {"FunctionName": "PrintString", "FunctionOwner": "KismetSystemLibrary"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			FString Title;
			Json->TryGetStringField(TEXT("name"), Title);
			TestFalse("node title is not 'None'", Title.Equals(TEXT("None")));

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			TestTrue("node has pins array", Json->TryGetArrayField(TEXT("pins"), Pins));
			if (Pins) TestTrue("node has at least one pin", Pins->Num() > 0);
		});
	});

	Describe("blueprint add_node VariableGet", [this]()
	{
		It("VariableGet with VariableName in properties creates node with pins", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestVarGetProps"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a variable first so the reference resolves
			TSharedPtr<FJsonObject> AddVarParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"TestHealth","var_type":"float"})"),
					*AssetPath));
			FMCPToolResult VarResult = GraphTool->Execute(AddVarParams);
			TestFalse("add_variable succeeded", VarResult.bIsError);

			// Add VariableGet using properties sub-object
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "add_node",
					"target": "%s",
					"graph": "EventGraph",
					"node_class": "VariableGet",
					"pos_x": 0, "pos_y": 0,
					"properties": {"VariableName": "TestHealth"}
				})"),
					*AssetPath));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			FString Title;
			Json->TryGetStringField(TEXT("name"), Title);
			TestFalse("node title is not 'None'", Title.Equals(TEXT("None")));

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			TestTrue("node has pins array", Json->TryGetArrayField(TEXT("pins"), Pins));
			if (Pins) TestTrue("node has at least one pin", Pins->Num() > 0);
		});
	});

	Describe("blueprint add_node SwitchOnInt hidden pins", [this]()
	{
		It("pins list excludes hidden internal pins (no NotEqual comparison pin)", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestSwitchOnIntHiddenPins"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "add_node",
					"target": "%s",
					"graph": "EventGraph",
					"node_class": "SwitchOnInt",
					"pos_x": 0, "pos_y": 0
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			if (!TestFalse("add_node SwitchOnInt succeeded", Result.bIsError)) return;

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (!TestTrue("node has pins array", Json->TryGetArrayField(TEXT("pins"), Pins))) return;
			if (!Pins) return;

			for (const TSharedPtr<FJsonValue>& PinVal : *Pins)
			{
				TSharedPtr<FJsonObject> PinObj = PinVal->AsObject();
				if (!PinObj.IsValid()) continue;

				FString PinName;
				PinObj->TryGetStringField(TEXT("name"), PinName);
				TestFalse(
					FString::Printf(TEXT("hidden pin '%s' must not appear in pin list"), *PinName),
					PinName.Contains(TEXT("NotEqual"), ESearchCase::IgnoreCase));
			}
		});
	});

	Describe("help action", [this]()
	{
		It("returns list of valid actions with descriptions", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				TEXT(R"({"action":"help"})"));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
			TestTrue("response has actions array", Json->TryGetArrayField(TEXT("actions"), Actions));
			if (!Actions) return;

			TestTrue("has at least 5 actions", Actions->Num() >= 5);

			bool bFoundAddNode = false;
			bool bFoundConnect = false;
			for (const TSharedPtr<FJsonValue>& ActionVal : *Actions)
			{
				TSharedPtr<FJsonObject> ActionObj = ActionVal->AsObject();
				if (!ActionObj.IsValid()) continue;

				FString Name;
				ActionObj->TryGetStringField(TEXT("name"), Name);
				if (Name == TEXT("add_node")) bFoundAddNode = true;
				if (Name == TEXT("connect"))  bFoundConnect = true;
			}

			TestTrue("contains add_node action", bFoundAddNode);
			TestTrue("contains connect action",  bFoundConnect);
		});
	});

	Describe("material edit_node numeric property", [this]()
	{
		It("edit_node with numeric ConstA sets Multiply expression float property", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeNumeric"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass());
			if (!TestNotNull("Multiply expression created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Pass ConstA as a JSON number (not a string) — this triggers the coercion bug
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"ConstA":3.14}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			// Should report the expression as modified (modified count > 0)
			const TArray<TSharedPtr<FJsonValue>>* Modified = nullptr;
			TestTrue("has modified array", Json->TryGetArrayField(TEXT("modified"), Modified));
			if (Modified)
			{
				TestTrue("modified count > 0", Modified->Num() > 0);
			}

			// Verify the property actually changed on the expression
			UMaterialExpressionMultiply* Multiply = Cast<UMaterialExpressionMultiply>(Expr);
			if (TestNotNull("cast to Multiply", Multiply))
			{
				TestTrue("ConstA was set to ~3.14", FMath::IsNearlyEqual(Multiply->ConstA, 3.14f, 0.001f));
			}
		});
	});

	Describe("blueprint add_variable var_name alias", [this]()
	{
		It("creates variable when var_name is used instead of name", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestVarNameAlias"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","var_name":"MyAliasVar","var_type":"int"})"),
					*AssetPath));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("one variable added", Count, 1);
		});
	});

	Describe("error message quality - add_node Blueprint class on Material", [this]()
	{
		It("error mentions 'Material' and suggests valid expression names", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestAddNodeBPClassOnMat"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"PrintString","pos_x":0,"pos_y":0})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError for unknown class", Result.bIsError);

			TestTrue("error mentions 'Material'", Result.Content.Contains(TEXT("Material")));
			TestTrue("error suggests a valid expression name",
				Result.Content.Contains(TEXT("Multiply")) ||
				Result.Content.Contains(TEXT("Add"))      ||
				Result.Content.Contains(TEXT("Constant")));
		});
	});

	Describe("error message quality - add_node unknown class on Blueprint", [this]()
	{
		It("returns bIsError and error text mentions valid classes", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestAddNodeUnknownClass"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"NonExistentNodeClass","pos_x":0,"pos_y":0})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError for unknown class", Result.bIsError);
			TestTrue("error mentions valid node classes",
				Result.Content.Contains(TEXT("CallFunction")) ||
				Result.Content.Contains(TEXT("Branch")));
		});
	});

	Describe("error message quality - connect type mismatch reason", [this]()
	{
		It("error includes CanCreateConnection reason, not just node GUIDs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectTypeMismatch"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add first Branch node
			TSharedPtr<FJsonObject> AddParams1 = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
					*AssetPath));
			FMCPToolResult AddResult1 = GraphTool->Execute(AddParams1);
			if (!TestFalse("first Branch node added without error", AddResult1.bIsError)) return;

			TSharedPtr<FJsonObject> AddJson1 = FMCPToolDirectTestHelper::ParseResultJson(AddResult1);
			if (!TestNotNull("first add_node result parsed", AddJson1.Get())) return;

			FString Node1Id;
			AddJson1->TryGetStringField(TEXT("node_id"), Node1Id);
			if (!TestFalse("first node_id is not empty", Node1Id.IsEmpty())) return;

			// Add second Branch node
			TSharedPtr<FJsonObject> AddParams2 = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":400,"pos_y":0})"),
					*AssetPath));
			FMCPToolResult AddResult2 = GraphTool->Execute(AddParams2);
			if (!TestFalse("second Branch node added without error", AddResult2.bIsError)) return;

			TSharedPtr<FJsonObject> AddJson2 = FMCPToolDirectTestHelper::ParseResultJson(AddResult2);
			if (!TestNotNull("second add_node result parsed", AddJson2.Get())) return;

			FString Node2Id;
			AddJson2->TryGetStringField(TEXT("node_id"), Node2Id);
			if (!TestFalse("second node_id is not empty", Node2Id.IsEmpty())) return;

			// Attempt incompatible connection: Exec "then" -> Bool "Condition"
			TSharedPtr<FJsonObject> ConnParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": "then"},
					"dest":   {"node": "%s", "pin": "Condition"}
				})"),
					*AssetPath, *Node1Id, *Node2Id));

			FMCPToolResult ConnResult = GraphTool->Execute(ConnParams);
			TestTrue("result is bIsError when connection fails", ConnResult.bIsError);
			// Error text includes the CanCreateConnection reason
			TestTrue("error contains reason beyond node identifiers",
				ConnResult.Content.Contains(TEXT("(")) ||
				ConnResult.Content.Contains(TEXT("type"), ESearchCase::IgnoreCase) ||
				ConnResult.Content.Contains(TEXT("rejected"), ESearchCase::IgnoreCase));
		});
	});

	Describe("edit_component component_name alias", [this]()
	{
		It("accepts component_name as alias for name", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestEditCompAlias"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a component
			TSharedPtr<FJsonObject> AddParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_component","target":"%s","component_class":"StaticMeshComponent","name":"TestMesh"})"),
					*AssetPath));
			FMCPToolResult AddResult = GraphTool->Execute(AddParams);
			TestFalse("add_component succeeded", AddResult.bIsError);

			// Edit using component_name alias
			TSharedPtr<FJsonObject> EditParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "edit_component",
					"target": "%s",
					"component_name": "TestMesh",
					"properties": {}
				})"),
					*AssetPath));

			FMCPToolResult Result = GraphTool->Execute(EditParams);
			TestFalse("edit_component with component_name alias is not an error", Result.bIsError);
		});
	});

	Describe("connect all-fail is bIsError", [this]()
	{
		It("returns bIsError when all connections fail due to bad GUIDs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestConnectAllFail"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "BADGUID00-0000-0000-0000-000000000000", "pin": "execute"},
					"dest":   {"node": "BADGUID01-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when all connections fail", Result.bIsError);
		});
	});

	Describe("disconnect all-fail is bIsError", [this]()
	{
		It("returns bIsError when all disconnections fail due to bad GUIDs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestDisconnectAllFail"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "BADGUID00-0000-0000-0000-000000000000", "pin": "execute"},
					"dest":   {"node": "BADGUID01-0000-0000-0000-000000000001", "pin": "then"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when all disconnections fail", Result.bIsError);
		});
	});

	Describe("disconnect idempotency", [this]()
	{
		It("blueprint disconnect when pins are not linked is silent no-op", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestDisconnectNoLink"));
			if (!TestNotNull("blueprint created", BP)) return;
			FString BPPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add two PrintString nodes (not connected)
			auto AddNode = [&](const FString& Name) -> FString
			{
				TSharedPtr<FJsonObject> P = FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"CallFunction","pos_x":0,"pos_y":0,"properties":{"FunctionName":"PrintString","FunctionOwner":"KismetSystemLibrary"}})"), *BPPath));
				FMCPToolResult R = GraphTool->Execute(P);
				TSharedPtr<FJsonObject> J = FMCPToolDirectTestHelper::ParseResultJson(R);
				FString Guid; if (J.IsValid()) J->TryGetStringField(TEXT("node_id"), Guid);
				return Guid;
			};
			FString Guid1 = AddNode(TEXT("Node1"));
			FString Guid2 = AddNode(TEXT("Node2"));
			if (!TestFalse("guid1 empty", Guid1.IsEmpty())) return;
			if (!TestFalse("guid2 empty", Guid2.IsEmpty())) return;

			// Try disconnecting unlinked pins — should succeed silently
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "%s", "pin": "then"},
					"dest":   {"node": "%s", "pin": "execute"}
				})"), *BPPath, *Guid1, *Guid2));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);
		});

		It("material disconnect when property is not connected to source is silent no-op", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectPropNoLink"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Vec = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionConstant3Vector::StaticClass(), 0, 0);
			if (!TestNotNull("vector expression", Vec)) return;

			FString VecGuid = Vec->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Disconnect without ever connecting — should succeed silently
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "%s"},
					"dest":   {"property": "BaseColor"}
				})"), *FMCPToolDirectTestHelper::GetAssetPath(Mat), *VecGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);
		});

		It("material disconnect when expressions are not connected is silent no-op", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectExprNoLink"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Disconnect without connecting — should succeed silently
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "disconnect",
					"target": "%s",
					"source": {"node": "%s"},
					"dest":   {"node": "%s"}
				})"), *FMCPToolDirectTestHelper::GetAssetPath(Mat), *MultiplyGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);
		});
	});

	Describe("connect diagnostics", [this]()
	{
		It("bad output pin name reports available outputs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDiagBadOutputPin"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid      = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": "BADPIN"},
					"dest":   {"node": "%s", "pin": "A"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *MultiplyGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError", Result.bIsError);
			TestTrue("error mentions 'Output pin'", Result.Content.Contains(TEXT("Output pin"), ESearchCase::IgnoreCase));
			TestTrue("error mentions 'not found'", Result.Content.Contains(TEXT("not found"), ESearchCase::IgnoreCase));
			TestTrue("error lists available outputs", Result.Content.Contains(TEXT("Available outputs"), ESearchCase::IgnoreCase));
		});

		It("bad input pin name reports available inputs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDiagBadInputPin"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid      = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"node": "%s", "pin": "BADPIN"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *MultiplyGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError", Result.bIsError);
			TestTrue("error mentions 'Input pin'", Result.Content.Contains(TEXT("Input pin"), ESearchCase::IgnoreCase));
			TestTrue("error mentions 'not found'", Result.Content.Contains(TEXT("not found"), ESearchCase::IgnoreCase));
			TestTrue("error lists available inputs", Result.Content.Contains(TEXT("Available inputs"), ESearchCase::IgnoreCase));
			TestTrue("error names the 'A' input", Result.Content.Contains(TEXT("'A'"), ESearchCase::CaseSensitive));
		});

		It("bad output pin on property connect reports available outputs", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDiagBadOutputOnProp"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Multiply = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0);
			if (!TestNotNull("Multiply expression", Multiply)) return;

			FString MultiplyGuid = Multiply->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": "BADPIN"},
					"dest":   {"property": "BaseColor"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *MultiplyGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError", Result.bIsError);
			TestTrue("error mentions 'Output pin'", Result.Content.Contains(TEXT("Output pin"), ESearchCase::IgnoreCase));
			TestTrue("error mentions 'not found'", Result.Content.Contains(TEXT("not found"), ESearchCase::IgnoreCase));
			TestTrue("error lists available outputs", Result.Content.Contains(TEXT("Available outputs"), ESearchCase::IgnoreCase));
		});

		It("Sine expression with empty source pin connects to another expression (regression)", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestSineEmptyPinExpr"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Sine = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionSine::StaticClass(), 0, 0);
			UMaterialExpression* Add = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionAdd::StaticClass(), 200, 0);
			if (!TestNotNull("Sine expression", Sine)) return;
			if (!TestNotNull("Add expression", Add)) return;

			FString SineGuid = Sine->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString AddGuid  = Add->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"node": "%s", "pin": "A"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *SineGuid, *AddGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("Sine->Add connection succeeds with empty source pin", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 1", Count, 1);
		});

		It("Sine expression connects to material property with empty source pin (regression)", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestSineEmptyPinProp"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Sine = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionSine::StaticClass(), 0, 0);
			if (!TestNotNull("Sine expression", Sine)) return;

			FString SineGuid = Sine->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "connect",
					"target": "%s",
					"source": {"node": "%s", "pin": ""},
					"dest":   {"property": "EmissiveColor"}
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *SineGuid));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("Sine->EmissiveColor connection succeeds with empty source pin", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 1", Count, 1);
		});
	});

	Describe("material output alias resolution", [this]()
	{
		It("resolves Output alias with valid property to ConnectMaterialProperty", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestOutputAlias"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			// Add a Constant3Vector node
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add node not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add result JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			// Connect with alias "Output" + pin "BaseColor"
			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "BaseColor"}
					})"), *MatPath, *NodeId)));
			TestFalse("connect not error", ConnResult.bIsError);

			TSharedPtr<FJsonObject> ConnJson = FMCPToolDirectTestHelper::ParseResultJson(ConnResult);
			if (!TestNotNull("connect JSON", ConnJson.Get())) return;
			int32 Count = 0;
			ConnJson->TryGetNumberField(TEXT("count"), Count);
			TestEqual("connected count is 1", Count, 1);
		});

		It("resolves Result alias case-insensitively", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestResultAlias"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"ScalarParameter","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "result", "pin": "Metallic"}
					})"), *MatPath, *NodeId)));
			TestFalse("connect not error", ConnResult.bIsError);
		});

		It("alias with invalid property name returns error listing valid properties", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestAliasBadProp"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "FakeProperty"}
					})"), *MatPath, *NodeId)));
			TestTrue("connect is error", ConnResult.bIsError);
			TestTrue("error mentions valid properties", ConnResult.Content.Contains(TEXT("BaseColor")));
			TestTrue("error mentions recognized alias", ConnResult.Content.Contains(TEXT("material output node")));
		});

		It("non-alias invalid dest GUID still returns expression not found", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestNonAlias"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "SomeRandomNode", "pin": "A"}
					})"), *MatPath, *NodeId)));
			TestTrue("connect is error", ConnResult.bIsError);
			TestTrue("error mentions not found", ConnResult.Content.Contains(TEXT("not found")));
			TestFalse("error does NOT mention material output node", ConnResult.Content.Contains(TEXT("material output node")));
		});
	});

	Describe("disconnect material output alias resolution", [this]()
	{
		It("disconnects via Output alias after connect via alias", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectOutputAlias"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			// Add a Constant3Vector node
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add node not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add result JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			// Connect via alias
			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "BaseColor"}
					})"), *MatPath, *NodeId)));
			TestFalse("connect not error", ConnResult.bIsError);

			// Disconnect via alias
			FMCPToolResult DiscResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "disconnect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "BaseColor"}
					})"), *MatPath, *NodeId)));
			TestFalse("disconnect not error", DiscResult.bIsError);

			TSharedPtr<FJsonObject> DiscJson = FMCPToolDirectTestHelper::ParseResultJson(DiscResult);
			if (!TestNotNull("disconnect JSON", DiscJson.Get())) return;
			int32 Count = 0;
			DiscJson->TryGetNumberField(TEXT("count"), Count);
			TestEqual("disconnected count is 1", Count, 1);
		});

		It("disconnect via alias when not connected is silent no-op", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectAliasNoop"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			// Add a node but don't connect
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			// Disconnect via alias (nothing connected)
			FMCPToolResult DiscResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "disconnect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "BaseColor"}
					})"), *MatPath, *NodeId)));
			TestFalse("disconnect not error", DiscResult.bIsError);

			TSharedPtr<FJsonObject> DiscJson = FMCPToolDirectTestHelper::ParseResultJson(DiscResult);
			if (!TestNotNull("disconnect JSON", DiscJson.Get())) return;
			int32 Count = 0;
			DiscJson->TryGetNumberField(TEXT("count"), Count);
			TestEqual("disconnected count is 0", Count, 0);
		});

		It("disconnect via alias with invalid property returns error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectAliasBadProp"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			FMCPToolResult DiscResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "disconnect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "Output", "pin": "FakeProperty"}
					})"), *MatPath, *NodeId)));
			TestTrue("disconnect is error", DiscResult.bIsError);
			TestTrue("error mentions valid properties", DiscResult.Content.Contains(TEXT("BaseColor")));
			TestTrue("error mentions recognized alias", DiscResult.Content.Contains(TEXT("material output node")));
		});

		It("disconnect via Result alias works", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestDisconnectResultAlias"));
			if (!TestNotNull("material created", Mat)) return;
			FString MatPath = FMCPToolDirectTestHelper::GetAssetPath(Mat);

			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"ScalarParameter","pos_x":0,"pos_y":0})"), *MatPath)));
			TestFalse("add not error", AddResult.bIsError);
			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add JSON", AddJson.Get())) return;
			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id non-empty", NodeId.IsEmpty())) return;

			// Connect via result alias
			GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "result", "pin": "Metallic"}
					})"), *MatPath, *NodeId)));

			// Disconnect via result alias
			FMCPToolResult DiscResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "disconnect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "result", "pin": "Metallic"}
					})"), *MatPath, *NodeId)));
			TestFalse("disconnect not error", DiscResult.bIsError);

			TSharedPtr<FJsonObject> DiscJson = FMCPToolDirectTestHelper::ParseResultJson(DiscResult);
			if (!TestNotNull("disconnect JSON", DiscJson.Get())) return;
			int32 Count = 0;
			DiscJson->TryGetNumberField(TEXT("count"), Count);
			TestEqual("disconnected count is 1", Count, 1);
		});
	});

	Describe("blueprint edit_node", [this]()
	{
		It("edit_node with pos sets NodePosX and NodePosY on a Blueprint node", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBPEditNodePos"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a Branch node
			TSharedPtr<FJsonObject> AddParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
					*AssetPath));
			FMCPToolResult AddResult = GraphTool->Execute(AddParams);
			if (!TestFalse("add_node Branch succeeded", AddResult.bIsError)) return;

			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add_node result parsed", AddJson.Get())) return;

			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id is not empty", NodeId.IsEmpty())) return;

			// Call edit_node with pos
			TSharedPtr<FJsonObject> EditParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","pos_x":500,"pos_y":600})"),
					*AssetPath, *NodeId));

			FMCPToolResult Result = GraphTool->Execute(EditParams);
			TestFalse("result is not error", Result.bIsError);

			// Find the node in BP graphs and verify position
			FGuid TargetGuid;
			FGuid::Parse(NodeId, TargetGuid);
			UEdGraphNode* FoundNode = nullptr;
			TArray<UEdGraph*> AllGraphs;
			BP->GetAllGraphs(AllGraphs);
			for (UEdGraph* Graph : AllGraphs)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (Node && Node->NodeGuid == TargetGuid)
					{
						FoundNode = Node;
						break;
					}
				}
				if (FoundNode) break;
			}

			if (TestNotNull("found the edited node", FoundNode))
			{
				TestEqual("NodePosX is 500", FoundNode->NodePosX, 500);
				TestEqual("NodePosY is 600", FoundNode->NodePosY, 600);
			}
		});
	});

	Describe("add_node missing pos validation", [this]()
	{
		It("Blueprint add_node without pos returns error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestAddNodeNoPosBP"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when pos is missing", Result.bIsError);
			TestTrue("error mentions pos", Result.Content.Contains(TEXT("pos"), ESearchCase::IgnoreCase));
		});

		It("Material add_node without pos returns error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestAddNodeNoPosMat"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Multiply"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is bIsError when pos is missing", Result.bIsError);
			TestTrue("error mentions pos", Result.Content.Contains(TEXT("pos"), ESearchCase::IgnoreCase));
		});

		It("Batch add_node with one missing pos only fails that node", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestAddNodeBatchPartialPos"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({
					"action": "add_node",
					"target": "%s",
					"nodes": [
						{"node_class": "Multiply", "pos_x": 0, "pos_y": 0},
						{"node_class": "Add"}
					]
				})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("batch result is not a hard error (partial success)", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			TestTrue("has nodes array", Json->TryGetArrayField(TEXT("nodes"), Nodes));
			if (Nodes)
			{
				TestEqual("nodes array has 2 entries", Nodes->Num(), 2);
				// Second entry should be an error
				if (Nodes->Num() >= 2)
				{
					TSharedPtr<FJsonObject> SecondNode = (*Nodes)[1]->AsObject();
					TestTrue("second node has error field", SecondNode.IsValid() && SecondNode->HasField(TEXT("error")));
				}
			}
		});
	});

	Describe("add_node graph_name alias", [this]()
	{
		It("add_node with graph_name targets a function graph", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestGraphNameAlias"));
			if (!TestNotNull("blueprint created", BP)) return;

			// First create a function graph
			TSharedPtr<FJsonObject> FuncParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_function","target":"%s","name":"MyTestFunc"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));
			FMCPToolResult FuncResult = GraphTool->Execute(FuncParams);
			TestFalse("add_function succeeded", FuncResult.bIsError);

			// Now add a node using graph_name to target the function graph
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph_name":"MyTestFunc","node_class":"CallFunction","function":"PrintString","function_owner":"KismetSystemLibrary","pos_x":100,"pos_y":50})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("add_node result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			// Single node returns flat object with node_id
			FString NodeId;
			TestTrue("has node_id", Json->TryGetStringField(TEXT("node_id"), NodeId));
			TestFalse("node_id is not empty", NodeId.IsEmpty());
		});

		It("add_node with invalid graph_name lists available graphs in error", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestGraphNameNotFound"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph_name":"NonExistentGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("error mentions available graphs", Result.Content.Contains(TEXT("Available"), ESearchCase::IgnoreCase));
			TestTrue("error mentions EventGraph", Result.Content.Contains(TEXT("EventGraph")));
		});
	});

	Describe("add_variable error reporting", [this]()
	{
		It("returns error when var_type is invalid", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestAddVarInvalidType"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"MyVar","var_type":"NotARealType"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not a hard error (returns success with errors array)", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
			TestTrue("has errors array", Json->TryGetArrayField(TEXT("errors"), Errors));
			if (Errors) TestTrue("errors array is not empty", Errors->Num() > 0);

			int32 Count = -1;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 0", Count, 0);
		});

		It("returns error when name is missing", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestAddVarMissingName"));
			if (!TestNotNull("blueprint created", BP)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","var_type":"float"})"),
					*FMCPToolDirectTestHelper::GetAssetPath(BP)));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not a hard error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
			TestTrue("has errors array", Json->TryGetArrayField(TEXT("errors"), Errors));
			if (Errors) TestTrue("errors array is not empty", Errors->Num() > 0);
		});
	});

	Describe("material edit_node struct DefaultValue", [this]()
	{
		It("sets FLinearColor DefaultValue on VectorParameter via JSON object", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeStructColor"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionVectorParameter::StaticClass());
			if (!TestNotNull("VectorParameter created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Pass DefaultValue as a JSON object {R, G, B, A} — this was the original bug
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"DefaultValue":{"R":1.0,"G":0.5,"B":0.25,"A":1.0}}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Modified = nullptr;
			TestTrue("has modified array", Json->TryGetArrayField(TEXT("modified"), Modified));
			if (Modified) TestTrue("modified count > 0", Modified->Num() > 0);

			// Verify the actual struct value was applied
			UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr);
			if (TestNotNull("cast to VectorParameter", VecParam))
			{
				TestTrue("R is ~1.0", FMath::IsNearlyEqual(VecParam->DefaultValue.R, 1.0f, 0.001f));
				TestTrue("G is ~0.5", FMath::IsNearlyEqual(VecParam->DefaultValue.G, 0.5f, 0.001f));
				TestTrue("B is ~0.25", FMath::IsNearlyEqual(VecParam->DefaultValue.B, 0.25f, 0.001f));
				TestTrue("A is ~1.0", FMath::IsNearlyEqual(VecParam->DefaultValue.A, 1.0f, 0.001f));
			}

			// Verify no warnings in response
			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			bool bHasWarnings = Json->TryGetArrayField(TEXT("warnings"), Warnings);
			if (bHasWarnings && Warnings) TestEqual("no warnings", Warnings->Num(), 0);
		});

		It("reports warning when struct DefaultValue has invalid format", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeStructInvalid"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionVectorParameter::StaticClass());
			if (!TestNotNull("VectorParameter created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Pass DefaultValue as a plain string that cannot be parsed as FLinearColor
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"DefaultValue":"not_a_color"}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not a hard error (soft warning expected)", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
			if (Warnings && TestTrue("at least one warning", Warnings->Num() > 0))
			{
				FString FirstWarning = (*Warnings)[0]->AsString();
				TestTrue("warning mentions property name", FirstWarning.Contains(TEXT("DefaultValue")));
				TestTrue("warning mentions failure", FirstWarning.Contains(TEXT("Failed to set")));
			}
		});

		It("also sets ParameterName alongside struct DefaultValue", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodeStructMultiProp"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionVectorParameter::StaticClass());
			if (!TestNotNull("VectorParameter created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Set both ParameterName (string) and DefaultValue (struct) in one call
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"ParameterName":"MyColor","DefaultValue":{"R":0.0,"G":1.0,"B":0.0,"A":1.0}}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr);
			if (TestNotNull("cast to VectorParameter", VecParam))
			{
				TestEqual("ParameterName is MyColor", VecParam->ParameterName.ToString(), FString(TEXT("MyColor")));
				TestTrue("G is ~1.0", FMath::IsNearlyEqual(VecParam->DefaultValue.G, 1.0f, 0.001f));
			}
		});

		It("sets partial FLinearColor keys, preserves unspecified", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestEditNodePartialColor"));
			if (!TestNotNull("material created", Mat)) return;

			UMaterialExpression* Expr = Helper.AddMaterialExpression(
				Mat, UMaterialExpressionVectorParameter::StaticClass());
			if (!TestNotNull("VectorParameter created", Expr)) return;

			FString GuidStr = Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);

			// Pre-set to non-zero so we can verify partial import preserves unspecified keys
			UMaterialExpressionVectorParameter* PreVec = CastChecked<UMaterialExpressionVectorParameter>(Expr);
			PreVec->DefaultValue = FLinearColor(0.8f, 0.8f, 0.8f, 0.8f);

			// Only specify R — omit G, B, A (common LLM caller edge case)
			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"DefaultValue":{"R":1.0}}})"),
					*FMCPToolDirectTestHelper::GetAssetPath(Mat), *GuidStr));

			FMCPToolResult Result = GraphTool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			// Partial struct import preserves unspecified keys
			UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr);
			if (TestNotNull("cast to VectorParameter", VecParam))
			{
				TestTrue("R is ~1.0", FMath::IsNearlyEqual(VecParam->DefaultValue.R, 1.0f, 0.001f));
				TestTrue("G preserved (0.8)", FMath::IsNearlyEqual(VecParam->DefaultValue.G, 0.8f, 0.001f));
				TestTrue("B preserved (0.8)", FMath::IsNearlyEqual(VecParam->DefaultValue.B, 0.8f, 0.001f));
				TestTrue("A preserved (0.8)", FMath::IsNearlyEqual(VecParam->DefaultValue.A, 0.8f, 0.001f));
			}

			// No warnings expected
			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			bool bHasWarnings = Json->TryGetArrayField(TEXT("warnings"), Warnings);
			if (bHasWarnings && Warnings) TestEqual("no warnings", Warnings->Num(), 0);
		});
	});

	Describe("blueprint edit_node struct property", [this]()
	{
		It("edit_node with struct JSON object on blueprint node property", [this]()
		{
			if (!TestNotNull("graph tool", GraphTool)) return;

			UBlueprint* BP = Helper.CreateTransientBlueprint(TEXT("TestBPEditNodeStruct"));
			if (!TestNotNull("blueprint created", BP)) return;

			FString AssetPath = FMCPToolDirectTestHelper::GetAssetPath(BP);

			// Add a node
			TSharedPtr<FJsonObject> AddParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
					*AssetPath));
			FMCPToolResult AddResult = GraphTool->Execute(AddParams);
			if (!TestFalse("add_node succeeded", AddResult.bIsError)) return;

			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add_node result parsed", AddJson.Get())) return;

			FString NodeId;
			AddJson->TryGetStringField(TEXT("node_id"), NodeId);
			if (!TestFalse("node_id not empty", NodeId.IsEmpty())) return;

			// Try to set a non-existent struct property -> should produce warning
			TSharedPtr<FJsonObject> EditParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(
					TEXT(R"({"action":"edit_node","target":"%s","node":"%s","properties":{"FakeStructProp":{"X":1,"Y":2,"Z":3}}})"),
					*AssetPath, *NodeId));

			FMCPToolResult Result = GraphTool->Execute(EditParams);
			TestFalse("result is not a hard error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("has warnings for unknown property", Json->TryGetArrayField(TEXT("warnings"), Warnings));
			if (Warnings) TestTrue("at least one warning", Warnings->Num() > 0);
		});
	});
}
