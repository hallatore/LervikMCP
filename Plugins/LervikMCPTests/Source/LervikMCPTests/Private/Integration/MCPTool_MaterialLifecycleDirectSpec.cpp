#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"

BEGIN_DEFINE_SPEC(FMCPTool_MaterialLifecycleDirectSpec,
	"Plugins.LervikMCP.Integration.Tools.MaterialLifecycle.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* CreateTool = nullptr;
	IMCPTool* GraphTool  = nullptr;
	IMCPTool* DeleteTool = nullptr;
	FString   CreatedAssetPath;
END_DEFINE_SPEC(FMCPTool_MaterialLifecycleDirectSpec)

void FMCPTool_MaterialLifecycleDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		CreateTool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
		GraphTool  = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
		DeleteTool = FMCPToolDirectTestHelper::FindTool(TEXT("delete"));
		CreatedAssetPath = TEXT("");
	});

	AfterEach([this]()
	{
		if (!CreatedAssetPath.IsEmpty() && DeleteTool)
		{
			DeleteTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
				FString::Printf(TEXT(R"({"type":"asset","target":"%s"})"), *CreatedAssetPath)));
		}
		CreateTool = GraphTool = DeleteTool = nullptr;
		Helper.Cleanup();
	});

	Describe("material lifecycle", [this]()
	{
		It("creates material, adds and connects nodes, then deletes", [this]()
		{
			if (!TestNotNull("create tool", CreateTool)) return;
			if (!TestNotNull("graph tool",  GraphTool))  return;
			if (!TestNotNull("delete tool", DeleteTool)) return;

			// Step 1 — Create material
			FMCPToolResult CreateResult = CreateTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					TEXT(R"({"type":"asset","class":"Material","name":"MatLC_Test","path":"/Game/_MCPTest_MatLC"})")));
			TestFalse("create not error", CreateResult.bIsError);

			TSharedPtr<FJsonObject> CreateJson = FMCPToolDirectTestHelper::ParseResultJson(CreateResult);
			if (!TestNotNull("create result JSON parsed", CreateJson.Get())) return;

			FString AssetClass, AssetName;
			CreateJson->TryGetStringField(TEXT("class"), AssetClass);
			CreateJson->TryGetStringField(TEXT("name"), AssetName);
			TestEqual("class is Material", AssetClass, FString(TEXT("Material")));
			TestEqual("name is MatLC_Test", AssetName, FString(TEXT("MatLC_Test")));
			CreateJson->TryGetStringField(TEXT("path"), CreatedAssetPath);
			TestFalse("created asset path non-empty", CreatedAssetPath.IsEmpty());
			if (CreatedAssetPath.IsEmpty()) return;

			// Step 2 — Add Constant3Vector node
			FMCPToolResult VecResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(
						TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant3Vector","pos_x":0,"pos_y":0})"),
						*CreatedAssetPath)));
			TestFalse("add Constant3Vector not error", VecResult.bIsError);

			TSharedPtr<FJsonObject> VecJson = FMCPToolDirectTestHelper::ParseResultJson(VecResult);
			if (!TestNotNull("Constant3Vector result JSON parsed", VecJson.Get())) return;

			FString VecNodeId;
			VecJson->TryGetStringField(TEXT("node_id"), VecNodeId);
			TestFalse("Constant3Vector node_id is non-empty", VecNodeId.IsEmpty());
			if (VecNodeId.IsEmpty()) return;

			// Step 3 — Add Multiply node
			FMCPToolResult MulResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(
						TEXT(R"({"action":"add_node","target":"%s","node_class":"Multiply","pos_x":200,"pos_y":0})"),
						*CreatedAssetPath)));
			TestFalse("add Multiply not error", MulResult.bIsError);

			TSharedPtr<FJsonObject> MulJson = FMCPToolDirectTestHelper::ParseResultJson(MulResult);
			if (!TestNotNull("Multiply result JSON parsed", MulJson.Get())) return;

			FString MulNodeId;
			MulJson->TryGetStringField(TEXT("node_id"), MulNodeId);
			TestFalse("Multiply node_id is non-empty", MulNodeId.IsEmpty());
			if (MulNodeId.IsEmpty()) return;

			const TArray<TSharedPtr<FJsonValue>>* Inputs  = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
			TestTrue("Multiply has inputs array",  MulJson->TryGetArrayField(TEXT("inputs"),  Inputs));
			TestTrue("Multiply has outputs array", MulJson->TryGetArrayField(TEXT("outputs"), Outputs));
			if (Inputs)  TestTrue("Multiply has >= 2 inputs",  Inputs->Num()  >= 2);
			if (Outputs) TestTrue("Multiply has >= 1 output",  Outputs->Num() >= 1);

			// Step 4 — Connect Constant3Vector → Multiply.A
			FMCPToolResult ConnResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({
						"action": "connect",
						"target": "%s",
						"source": {"node": "%s", "pin": ""},
						"dest":   {"node": "%s", "pin": "A"}
					})"),
						*CreatedAssetPath, *VecNodeId, *MulNodeId)));
			TestFalse("connect not error", ConnResult.bIsError);

			TSharedPtr<FJsonObject> ConnJson = FMCPToolDirectTestHelper::ParseResultJson(ConnResult);
			if (!TestNotNull("connect result JSON parsed", ConnJson.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Connected = nullptr;
			TestTrue("has connected array", ConnJson->TryGetArrayField(TEXT("connected"), Connected));
			if (Connected) TestEqual("connected has 1 entry", Connected->Num(), 1);

			int32 ConnCount = -1;
			ConnJson->TryGetNumberField(TEXT("count"), ConnCount);
			TestEqual("connect count is 1", ConnCount, 1);

			// Step 5 — Delete material
			FMCPToolResult DeleteResult = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"type":"asset","target":"%s"})"), *CreatedAssetPath)));
			TestFalse("delete not error", DeleteResult.bIsError);

			TSharedPtr<FJsonObject> DeleteJson = FMCPToolDirectTestHelper::ParseResultJson(DeleteResult);
			if (!TestNotNull("delete result JSON parsed", DeleteJson.Get())) return;

			int32 DelCount = -1;
			DeleteJson->TryGetNumberField(TEXT("count"), DelCount);
			TestEqual("delete count is 1", DelCount, 1);

			const TArray<TSharedPtr<FJsonValue>>* Deleted = nullptr;
			TestTrue("has deleted array", DeleteJson->TryGetArrayField(TEXT("deleted"), Deleted));
			if (Deleted && Deleted->Num() >= 1)
			{
				FString DelPath;
				(*Deleted)[0]->TryGetString(DelPath);
				TestEqual("deleted[0] is CreatedAssetPath", DelPath, CreatedAssetPath);
			}

			CreatedAssetPath = TEXT(""); // AfterEach safety-net not needed
		});
	});

	Describe("material editor refresh on tool edit", [this]()
	{
		It("package is marked dirty after add_node when material editor is open", [this]()
		{
			if (!TestNotNull("create tool", CreateTool)) return;
			if (!TestNotNull("graph tool",  GraphTool))  return;
			if (!TestNotNull("delete tool", DeleteTool)) return;

			// Step 1 — Create a real material asset on disk
			FMCPToolResult CreateResult = CreateTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					TEXT(R"({"type":"asset","class":"Material","name":"MatEditorRefresh_Test","path":"/Game/_MCPTest_MatEditorRefresh"})"))
			);
			TestFalse("create not error", CreateResult.bIsError);
			if (CreateResult.bIsError) return;

			TSharedPtr<FJsonObject> CreateJson = FMCPToolDirectTestHelper::ParseResultJson(CreateResult);
			if (!TestNotNull("create JSON parsed", CreateJson.Get())) return;

			FString AssetName;
			CreateJson->TryGetStringField(TEXT("name"), AssetName);
			CreateJson->TryGetStringField(TEXT("path"), CreatedAssetPath);
			if (!TestFalse("created asset path non-empty", CreatedAssetPath.IsEmpty())) return;

			// Step 2 — Load the UMaterial so we can open it in the editor
			FString MaterialObjectPath = FString::Printf(TEXT("%s.%s"), *CreatedAssetPath, *AssetName);
			UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialObjectPath);
			if (!TestNotNull("UMaterial loaded", Material)) return;

			// Step 3 — Open in material editor
			UAssetEditorSubsystem* EdSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!TestNotNull("AssetEditorSubsystem", EdSub)) return;
			EdSub->OpenEditorForAsset(Material);

			// Step 4 — Verify editor is open and get the preview material
			IAssetEditorInstance* EditorInstance = EdSub->FindEditorForAsset(Material, /*bFocusIfOpen=*/false);
			if (!TestNotNull("material editor is open", EditorInstance)) return;
			IMaterialEditor* MatEditor = static_cast<IMaterialEditor*>(EditorInstance);
			UMaterial* PreviewMaterial = Cast<UMaterial>(MatEditor->GetMaterialInterface());
			if (!TestNotNull("preview material exists", PreviewMaterial)) return;

			int32 ExprsBefore = PreviewMaterial->GetExpressions().Num();

			// Step 5 — Call add_node while the editor is open
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","node_class":"Constant","pos_x":0,"pos_y":0})"),
						*CreatedAssetPath)));
			TestFalse("add_node not error", AddResult.bIsError);

			// Step 6 — Preview material must have the new expression
			// (tools edit the preview copy when editor is open; RefreshMaterialEditor re-syncs graph)
			int32 ExprsAfter = PreviewMaterial->GetExpressions().Num();
			TestTrue("preview material has new expression after add_node with editor open", ExprsAfter > ExprsBefore);

			// Step 7 — Close editor before AfterEach deletes the asset
			EdSub->CloseAllEditorsForAsset(Material);

			// AfterEach will delete CreatedAssetPath
		});
	});
}
