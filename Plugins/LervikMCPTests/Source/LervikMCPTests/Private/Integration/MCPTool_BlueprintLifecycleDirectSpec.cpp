#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Subsystems/AssetEditorSubsystem.h"

BEGIN_DEFINE_SPEC(FMCPTool_BlueprintLifecycleDirectSpec,
	"Plugins.LervikMCP.Integration.Tools.BlueprintLifecycle.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* CreateTool = nullptr;
	IMCPTool* GraphTool  = nullptr;
	IMCPTool* DeleteTool = nullptr;
	FString   CreatedAssetPath;
END_DEFINE_SPEC(FMCPTool_BlueprintLifecycleDirectSpec)

void FMCPTool_BlueprintLifecycleDirectSpec::Define()
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

	Describe("blueprint editor refresh on tool edit", [this]()
	{
		It("blueprint editor stays open and reflects add_node after tool call", [this]()
		{
			if (!TestNotNull("create tool", CreateTool)) return;
			if (!TestNotNull("graph tool",  GraphTool))  return;
			if (!TestNotNull("delete tool", DeleteTool)) return;

			// Step 1 — Create a real blueprint asset on disk
			FMCPToolResult CreateResult = CreateTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					TEXT(R"({"type":"asset","class":"Blueprint","name":"BPEditorRefresh_Test","path":"/Game/_MCPTest_BPEditorRefresh","parent_class":"Actor"})"))
			);
			TestFalse("create not error", CreateResult.bIsError);
			if (CreateResult.bIsError) return;

			TSharedPtr<FJsonObject> CreateJson = FMCPToolDirectTestHelper::ParseResultJson(CreateResult);
			if (!TestNotNull("create JSON parsed", CreateJson.Get())) return;

			FString AssetName;
			CreateJson->TryGetStringField(TEXT("name"), AssetName);
			CreateJson->TryGetStringField(TEXT("path"), CreatedAssetPath);
			if (!TestFalse("created asset path non-empty", CreatedAssetPath.IsEmpty())) return;

			// Step 2 — Load the UBlueprint so we can open it in the editor
			FString BlueprintObjectPath = FString::Printf(TEXT("%s.%s"), *CreatedAssetPath, *AssetName);
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);
			if (!TestNotNull("UBlueprint loaded", Blueprint)) return;

			// Step 3 — Open in blueprint editor
			UAssetEditorSubsystem* EdSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!TestNotNull("AssetEditorSubsystem", EdSub)) return;
			EdSub->OpenEditorForAsset(Blueprint);

			// Step 4 — Verify editor is open
			IAssetEditorInstance* EditorInstance = EdSub->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);
			if (!TestNotNull("blueprint editor is open", EditorInstance)) return;
			TestEqual("editor name is BlueprintEditor",
				EditorInstance->GetEditorName(), FName(TEXT("BlueprintEditor")));

			// Step 5 — Count initial nodes in EventGraph
			TArray<UEdGraph*> AllGraphs;
			Blueprint->GetAllGraphs(AllGraphs);
			int32 NodesBefore = 0;
			for (UEdGraph* Graph : AllGraphs)
			{
				if (Graph) NodesBefore += Graph->Nodes.Num();
			}

			// Step 6 — Call add_node while the editor is open
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
						*CreatedAssetPath)));
			TestFalse("add_node not error", AddResult.bIsError);

			// Step 7 — Editor must still be open after refresh
			IAssetEditorInstance* EditorAfter = EdSub->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);
			TestNotNull("blueprint editor still open after add_node", EditorAfter);

			// Step 8 — Blueprint must have more nodes than before
			int32 NodesAfter = 0;
			Blueprint->GetAllGraphs(AllGraphs);
			for (UEdGraph* Graph : AllGraphs)
			{
				if (Graph) NodesAfter += Graph->Nodes.Num();
			}
			TestTrue("blueprint has more nodes after add_node with editor open", NodesAfter > NodesBefore);

			// Step 9 — Close editor before AfterEach deletes the asset
			EdSub->CloseAllEditorsForAsset(Blueprint);

			// AfterEach will delete CreatedAssetPath
		});
	});
}
