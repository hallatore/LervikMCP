#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"

BEGIN_DEFINE_SPEC(FMCPTool_EditorDirectSpec, "Plugins.LervikMCP.Integration.Tools.Editor.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* EditorTool = nullptr;
END_DEFINE_SPEC(FMCPTool_EditorDirectSpec)

void FMCPTool_EditorDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		EditorTool = FMCPToolDirectTestHelper::FindTool(TEXT("editor"));
	});

	AfterEach([this]()
	{
		EditorTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("editor tool is registered", [this]()
		{
			TestNotNull("editor tool found", EditorTool);
		});
	});

	Describe("silent failure warnings", [this]()
	{
		It("open with invalid path populates warnings array", [this]()
		{
			if (!TestNotNull("editor tool found", EditorTool)) return;

			FMCPToolResult Result = EditorTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("open") },
					{ TEXT("target"), TEXT("/Temp/DoesNotExist_ZZZ_MCP") }
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

		It("navigate with invalid path populates warnings array", [this]()
		{
			if (!TestNotNull("editor tool found", EditorTool)) return;

			FMCPToolResult Result = EditorTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("navigate") },
					{ TEXT("target"), TEXT("/Temp/DoesNotExist_ZZZ_MCP") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("result has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
		});

		It("select with invalid actor name populates warnings array", [this]()
		{
			if (!TestNotNull("editor tool found", EditorTool)) return;

			FMCPToolResult Result = EditorTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("select") },
					{ TEXT("target"), TEXT("NonExistentActor_ZZZ_MCP_9999") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("result has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
		});

		It("deselect with invalid actor name populates warnings array", [this]()
		{
			if (!TestNotNull("editor tool found", EditorTool)) return;

			FMCPToolResult Result = EditorTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("deselect") },
					{ TEXT("target"), TEXT("NonExistentActor_ZZZ_MCP_9999") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("result has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
		});

		It("save with invalid path populates warnings array", [this]()
		{
			if (!TestNotNull("editor tool found", EditorTool)) return;

			AddExpectedErrorPlain(TEXT("SaveAsset failed"), EAutomationExpectedErrorFlags::Contains, 1);

			FMCPToolResult Result = EditorTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("save") },
					{ TEXT("target"), TEXT("/Game/DoesNotExist_ZZZ_MCP/FakeAsset") }
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
}
