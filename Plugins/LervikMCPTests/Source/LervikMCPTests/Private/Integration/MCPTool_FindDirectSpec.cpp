#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"

// M_Test.uasset exists at Content/M_Test.uasset -> /Game/M_Test in the Asset Registry
static const FString KnownAssetName = TEXT("M_Test");

BEGIN_DEFINE_SPEC(FMCPTool_FindDirectSpec, "Plugins.LervikMCP.Integration.Tools.Find.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* FindTool = nullptr;
END_DEFINE_SPEC(FMCPTool_FindDirectSpec)

void FMCPTool_FindDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		FindTool = FMCPToolDirectTestHelper::FindTool(TEXT("find"));
	});

	AfterEach([this]()
	{
		FindTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("find tool is registered", [this]()
		{
			TestNotNull("find tool found", FindTool);
		});
	});

	Describe("type=asset, name filter", [this]()
	{
		It("exact name returns count > 0", [this]()
		{
			if (!TestNotNull("find tool found", FindTool)) return;

			FMCPToolResult Result = FindTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"), TEXT("asset") },
					{ TEXT("name"), KnownAssetName }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			double Count = 0.0;
			TestTrue("result has 'count' field", Json->TryGetNumberField(TEXT("count"), Count));
			TestTrue("count > 0 for exact name match", Count > 0.0);
		});

		It("wildcard name returns count > 0", [this]()
		{
			if (!TestNotNull("find tool found", FindTool)) return;

			FMCPToolResult Result = FindTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"), TEXT("asset") },
					{ TEXT("name"), TEXT("*_Test*") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			double Count = 0.0;
			TestTrue("result has 'count' field", Json->TryGetNumberField(TEXT("count"), Count));
			TestTrue("count > 0 for wildcard match", Count > 0.0);
		});
	});

	Describe("type=asset, class + path filter (regression)", [this]()
	{
		It("class=Material with game path returns no error", [this]()
		{
			if (!TestNotNull("find tool found", FindTool)) return;

			FMCPToolResult Result = FindTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),  TEXT("asset") },
					{ TEXT("class"), TEXT("Material") },
					{ TEXT("path"),  TEXT("/Game") }
				})
			);

			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			TestNotNull("result parses as JSON", Json.Get());
		});
	});

	Describe("type=asset, class filter errors", [this]()
	{
		It("class=NonExistentClassName returns an error", [this]()
		{
			if (!TestNotNull("find tool found", FindTool)) return;

			FMCPToolResult Result = FindTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("type"),  TEXT("asset") },
					{ TEXT("class"), TEXT("NonExistentClassName_ZZZ_MCP") }
				})
			);

			TestTrue("result is an error when class cannot be resolved", Result.bIsError);
		});
	});
}
