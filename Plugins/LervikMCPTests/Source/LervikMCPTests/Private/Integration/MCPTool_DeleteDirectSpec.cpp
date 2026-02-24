#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

BEGIN_DEFINE_SPEC(FMCPTool_DeleteDirectSpec, "Plugins.LervikMCP.Integration.Tools.Delete.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* DeleteTool = nullptr;
	IMCPTool* CreateTool = nullptr;
	IMCPTool* GraphTool  = nullptr;
	FString TestFolderPath;
	FString CreatedAssetPath;
END_DEFINE_SPEC(FMCPTool_DeleteDirectSpec)

void FMCPTool_DeleteDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		DeleteTool = FMCPToolDirectTestHelper::FindTool(TEXT("delete"));
	});

	AfterEach([this]()
	{
		DeleteTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("delete tool is registered", [this]()
		{
			TestNotNull("delete tool found", DeleteTool);
		});
	});

	Describe("silent failure warnings", [this]()
	{
		It("type=actor with non-existent target populates warnings array", [this]()
		{
			if (!TestNotNull("delete tool found", DeleteTool)) return;

			FMCPToolResult Result = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),   TEXT("actor") },
					{ TEXT("target"), TEXT("NonExistentActor_ZZZ_MCP_9999") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			double Count = -1.0;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestEqual("count is 0", Count, 0.0);

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("result has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
		});
	});

	Describe("type=folder", [this]()
	{
		BeforeEach([this]()
		{
			TestFolderPath = FString::Printf(TEXT("/Game/MCPTest_DeleteFolder_%d"), FMath::RandRange(10000, 99999));
			if (GEditor)
			{
				UEditorAssetSubsystem* AssetSub = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
				if (AssetSub)
				{
					AssetSub->MakeDirectory(TestFolderPath);
				}
			}
		});

		AfterEach([this]()
		{
			if (!TestFolderPath.IsEmpty())
			{
				FString RelPath = TestFolderPath.RightChop(FString(TEXT("/Game/")).Len());
				FString DiskPath = FPaths::ProjectContentDir() / RelPath;
				IFileManager::Get().DeleteDirectory(*DiskPath, false, true);
				TestFolderPath.Empty();
			}
		});

		It("deletes an empty content folder and returns count > 0", [this]()
		{
			if (!TestNotNull("delete tool found", DeleteTool)) return;
			if (!TestFalse("test folder path was set", TestFolderPath.IsEmpty())) return;

			FMCPToolResult Result = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),   TEXT("folder") },
					{ TEXT("target"), TestFolderPath }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			double Count = 0.0;
			Json->TryGetNumberField(TEXT("count"), Count);
			TestTrue("count > 0 (folder was deleted)", Count > 0.0);

			FString RelPath = TestFolderPath.RightChop(FString(TEXT("/Game/")).Len());
			FString DiskPath = FPaths::ProjectContentDir() / RelPath;
			TestFalse("folder no longer exists on disk", IFileManager::Get().DirectoryExists(*DiskPath));
		});

		It("returns error for a non-existent folder path", [this]()
		{
			if (!TestNotNull("delete tool found", DeleteTool)) return;

			FMCPToolResult Result = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),   TEXT("folder") },
					{ TEXT("target"), TEXT("/Game/NonExistentMCPFolder_ZZZ_99999") }
				})
			);

			if (Result.bIsError)
			{
				TestTrue("hard error is acceptable", Result.bIsError);
			}
			else
			{
				TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
				if (!TestNotNull("result parses as JSON", Json.Get())) return;

				double Count = -1.0;
				Json->TryGetNumberField(TEXT("count"), Count);
				TestEqual("count is 0 for non-existent folder", Count, 0.0);
			}
		});

		It("returns bIsError when targeting a non-empty folder", [this]()
		{
			if (!TestNotNull("delete tool found", DeleteTool)) return;

			FMCPToolResult Result = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),   TEXT("folder") },
					{ TEXT("target"), TEXT("/Game/ThirdPerson") }
				})
			);

			TestTrue("deleting non-empty folder returns bIsError", Result.bIsError);
		});

		It("rejects path traversal in folder target", [this]()
		{
			if (!TestNotNull("delete tool found", DeleteTool)) return;

			FMCPToolResult Result = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),   TEXT("folder") },
					{ TEXT("target"), TEXT("/Game/../../SomeDir") }
				})
			);

			TestTrue("path traversal returns hard error", Result.bIsError);
		});
	});

	Describe("Blueprint element deletion with open editor", [this]()
	{
		BeforeEach([this]()
		{
			CreateTool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
			GraphTool  = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			CreatedAssetPath = TEXT("");
		});

		AfterEach([this]()
		{
			if (!CreatedAssetPath.IsEmpty() && DeleteTool)
			{
				DeleteTool->Execute(FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"type":"asset","target":"%s"})"), *CreatedAssetPath)));
			}
			CreateTool = GraphTool = nullptr;
			CreatedAssetPath = TEXT("");
		});

		It("deletes a node while editor is open and editor remains open", [this]()
		{
			if (!TestNotNull("create tool", CreateTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;
			if (!TestNotNull("delete tool", DeleteTool)) return;

			// Create blueprint
			FMCPToolResult CreateResult = CreateTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					TEXT(R"({"type":"asset","class":"Blueprint","name":"BPDeleteNode_Test","path":"/Game/_MCPTest_BPDeleteNode","parent_class":"Actor"})")));
			TestFalse("create not error", CreateResult.bIsError);
			if (CreateResult.bIsError) return;

			TSharedPtr<FJsonObject> CreateJson = FMCPToolDirectTestHelper::ParseResultJson(CreateResult);
			if (!TestNotNull("create JSON", CreateJson.Get())) return;

			FString AssetName;
			CreateJson->TryGetStringField(TEXT("name"), AssetName);
			CreateJson->TryGetStringField(TEXT("path"), CreatedAssetPath);
			if (!TestFalse("created path non-empty", CreatedAssetPath.IsEmpty())) return;

			// Load and open in editor
			FString BPObjPath = FString::Printf(TEXT("%s.%s"), *CreatedAssetPath, *AssetName);
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BPObjPath);
			if (!TestNotNull("UBlueprint loaded", Blueprint)) return;

			UAssetEditorSubsystem* EdSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!TestNotNull("AssetEditorSubsystem", EdSub)) return;
			EdSub->OpenEditorForAsset(Blueprint);

			if (!TestNotNull("editor open", EdSub->FindEditorForAsset(Blueprint, false))) return;

			// Count nodes before
			TArray<UEdGraph*> AllGraphs;
			Blueprint->GetAllGraphs(AllGraphs);
			int32 NodesBefore = 0;
			for (UEdGraph* G : AllGraphs) { if (G) NodesBefore += G->Nodes.Num(); }

			// Add a node
			FMCPToolResult AddResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_node","target":"%s","graph":"EventGraph","node_class":"Branch","pos_x":0,"pos_y":0})"),
						*CreatedAssetPath)));
			TestFalse("add_node not error", AddResult.bIsError);
			if (AddResult.bIsError) return;

			TSharedPtr<FJsonObject> AddJson = FMCPToolDirectTestHelper::ParseResultJson(AddResult);
			if (!TestNotNull("add_node JSON", AddJson.Get())) return;

			// Single-node result is the node object directly (node_id at top level)
			FString NodeGuid;
			AddJson->TryGetStringField(TEXT("node_id"), NodeGuid);
			if (!TestFalse("NodeGuid should not be empty", NodeGuid.IsEmpty())) return;
			FMCPToolResult DelResult = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"type":"node","target":"%s","parent":"%s"})"),
						*NodeGuid, *CreatedAssetPath)));
			TestFalse("delete node not error", DelResult.bIsError);

			// Verify node count back to original
			AllGraphs.Empty();
			Blueprint->GetAllGraphs(AllGraphs);
			int32 NodesAfter = 0;
			for (UEdGraph* G : AllGraphs) { if (G) NodesAfter += G->Nodes.Num(); }
			TestEqual("node count back to before", NodesAfter, NodesBefore);

			// Verify editor still open
			TestNotNull("editor still open after delete", EdSub->FindEditorForAsset(Blueprint, false));
			EdSub->CloseAllEditorsForAsset(Blueprint);
		});

		It("deletes a variable while editor is open and editor remains open", [this]()
		{
			if (!TestNotNull("create tool", CreateTool)) return;
			if (!TestNotNull("graph tool", GraphTool)) return;
			if (!TestNotNull("delete tool", DeleteTool)) return;

			// Create blueprint
			FMCPToolResult CreateResult = CreateTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					TEXT(R"({"type":"asset","class":"Blueprint","name":"BPDeleteVar_Test","path":"/Game/_MCPTest_BPDeleteVar","parent_class":"Actor"})")));
			TestFalse("create not error", CreateResult.bIsError);
			if (CreateResult.bIsError) return;

			TSharedPtr<FJsonObject> CreateJson = FMCPToolDirectTestHelper::ParseResultJson(CreateResult);
			if (!TestNotNull("create JSON", CreateJson.Get())) return;

			FString AssetName;
			CreateJson->TryGetStringField(TEXT("name"), AssetName);
			CreateJson->TryGetStringField(TEXT("path"), CreatedAssetPath);
			if (!TestFalse("created path non-empty", CreatedAssetPath.IsEmpty())) return;

			// Load and open in editor
			FString BPObjPath = FString::Printf(TEXT("%s.%s"), *CreatedAssetPath, *AssetName);
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BPObjPath);
			if (!TestNotNull("UBlueprint loaded", Blueprint)) return;

			UAssetEditorSubsystem* EdSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!TestNotNull("AssetEditorSubsystem", EdSub)) return;
			EdSub->OpenEditorForAsset(Blueprint);

			if (!TestNotNull("editor open", EdSub->FindEditorForAsset(Blueprint, false))) return;

			// Add a variable
			FMCPToolResult AddVarResult = GraphTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"action":"add_variable","target":"%s","name":"MCPTestVar","var_type":"bool"})"),
						*CreatedAssetPath)));
			TestFalse("add_variable not error", AddVarResult.bIsError);
			if (AddVarResult.bIsError) return;

			int32 VarsBefore = Blueprint->NewVariables.Num();
			TestTrue("variable was added", VarsBefore > 0);

			// Delete the variable
			FMCPToolResult DelResult = DeleteTool->Execute(
				FMCPToolDirectTestHelper::MakeParamsFromJson(
					FString::Printf(TEXT(R"({"type":"variable","target":"MCPTestVar","parent":"%s"})"),
						*CreatedAssetPath)));
			TestFalse("delete variable not error", DelResult.bIsError);

			// Verify variable removed
			int32 VarsAfter = Blueprint->NewVariables.Num();
			TestTrue("variable was removed", VarsAfter < VarsBefore);

			// Verify editor still open
			TestNotNull("editor still open after delete", EdSub->FindEditorForAsset(Blueprint, false));
			EdSub->CloseAllEditorsForAsset(Blueprint);
		});
	});
}
