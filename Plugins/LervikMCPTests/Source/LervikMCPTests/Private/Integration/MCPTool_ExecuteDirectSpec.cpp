#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "Tools/MCPTool_Execute.h"

BEGIN_DEFINE_SPEC(FMCPTool_ExecuteDirectSpec, "Plugins.LervikMCP.Integration.Tools.Execute.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* ExecuteTool = nullptr;
END_DEFINE_SPEC(FMCPTool_ExecuteDirectSpec)

void FMCPTool_ExecuteDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		ExecuteTool = FMCPToolDirectTestHelper::FindTool(TEXT("execute"));
	});

	AfterEach([this]()
	{
		ExecuteTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("execute tool is registered", [this]()
		{
			TestNotNull("execute tool found", ExecuteTool);
		});
	});

	Describe("action=command", [this]()
	{
		It("returns command field in result", [this]()
		{
			if (!TestNotNull("execute tool found", ExecuteTool)) return;

			const FString Cmd = TEXT("obj list class=Actor");
			FMCPToolResult Result = ExecuteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"),  TEXT("command") },
					{ TEXT("command"), Cmd }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString CommandField;
			TestTrue("result has 'command' field", Json->TryGetStringField(TEXT("command"), CommandField));
			TestEqual("command field matches input", CommandField, Cmd);
		});

		It("obj list produces non-empty output", [this]()
		{
			if (!TestNotNull("execute tool found", ExecuteTool)) return;

			FMCPToolResult Result = ExecuteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"),  TEXT("command") },
					{ TEXT("command"), TEXT("obj list class=Actor") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Output;
			TestTrue("result has 'output' field", Json->TryGetStringField(TEXT("output"), Output));
			TestFalse("output is non-empty", Output.IsEmpty());
		});

		It("blocked command returns error", [this]()
		{
			if (!TestNotNull("execute tool found", ExecuteTool)) return;

			FMCPToolResult Result = ExecuteTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"),  TEXT("command") },
					{ TEXT("command"), TEXT("exit") }
				})
			);

			TestTrue("exit command is blocked", Result.bIsError);
		});
	});

	Describe("Runtime execute (no editor)", [this]()
	{
		It("rejects action=command with editor module error", [this]()
		{
			FMCPTool_Execute RuntimeTool;
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("action"), TEXT("command"));
			Params->SetStringField(TEXT("command"), TEXT("stat unit"));

			FMCPToolResult Result = RuntimeTool.Execute(Params);

			TestTrue(TEXT("Should be an error"), Result.bIsError);
			TestTrue(TEXT("Error mentions editor module"), Result.Content.Contains(TEXT("editor")));
		});
	});
}
