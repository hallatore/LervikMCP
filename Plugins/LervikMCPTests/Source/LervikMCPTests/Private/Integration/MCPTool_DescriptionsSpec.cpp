#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"

// Returns the Description of the named parameter, or empty string if not found.
static FString GetMCPParamDesc(const FMCPToolInfo& Info, const TCHAR* ParamName)
{
	const FName SearchName(ParamName);
	for (const FMCPToolParameter& Param : Info.Parameters)
	{
		if (Param.Name == SearchName)
		{
			return Param.Description;
		}
	}
	return FString();
}

BEGIN_DEFINE_SPEC(FMCPTool_DescriptionsSpec, "Plugins.LervikMCP.Integration.Tools.Descriptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMCPTool_DescriptionsSpec)

void FMCPTool_DescriptionsSpec::Define()
{
	// ── create tool ──────────────────────────────────────────────────────────

	Describe("create tool", [this]()
	{
		It("class param distinguishes spawnable actor classes from asset classes", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
			if (!TestNotNull("create tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("class"));
			TestTrue("class description mentions 'spawnable'",
				Desc.Contains(TEXT("spawnable"), ESearchCase::IgnoreCase));
		});

		It("template param documents that location/rotation apply to the duplicate", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
			if (!TestNotNull("create tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("template"));
			TestTrue("template description mentions location",
				Desc.Contains(TEXT("location"), ESearchCase::IgnoreCase));
		});
	});

	// ── modify tool ──────────────────────────────────────────────────────────

	Describe("modify tool", [this]()
	{
		It("target description documents ActorLabel.ComponentName dotted path syntax", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("modify"));
			if (!TestNotNull("modify tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("target"));
			TestTrue("target description mentions 'ActorLabel.ComponentName' dotted path",
				Desc.Contains(TEXT("ActorLabel.ComponentName"), ESearchCase::CaseSensitive));
		});

		It("properties description shows JSON object format with braces", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("modify"));
			if (!TestNotNull("modify tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("properties"));
			TestTrue("properties description includes a JSON object format example (contains '{')",
				Desc.Contains(TEXT("{")));
		});
	});

	// ── graph tool ───────────────────────────────────────────────────────────

	Describe("graph tool", [this]()
	{
		It("node_class description explains that CallFunction nodes require the function param", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("node_class"));
			TestTrue("node_class description mentions 'function param' to explain the CallFunction pairing",
				Desc.Contains(TEXT("function param"), ESearchCase::IgnoreCase));
		});

		It("node_class description mentions Material expression names for Material graphs", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("node_class"));
			TestTrue("node_class description mentions Material expression names (e.g. Multiply, Add, Lerp)",
				Desc.Contains(TEXT("Multiply"), ESearchCase::CaseSensitive) ||
				Desc.Contains(TEXT("Add"),      ESearchCase::CaseSensitive) ||
				Desc.Contains(TEXT("Lerp"),     ESearchCase::CaseSensitive));
		});

		It("pos param mentions flat array format", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("pos"));
			TestTrue("pos description mentions 'flat'",
				Desc.Contains(TEXT("flat"), ESearchCase::IgnoreCase));
		});

		It("source param mentions inspect for pin discovery", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("source"));
			TestTrue("source description mentions 'inspect'",
				Desc.Contains(TEXT("inspect"), ESearchCase::IgnoreCase));
		});
	});

	// ── inspect tool ─────────────────────────────────────────────────────────

	Describe("inspect tool", [this]()
	{
		It("type description mentions 'level actor' scope for components", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("inspect tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("type"));
			TestTrue("type description mentions 'level actor' scope for components",
				Desc.Contains(TEXT("level actor"), ESearchCase::IgnoreCase));
		});

		It("type description redirects Blueprint component queries to the graph tool", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("inspect tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("type"));
			TestTrue("type description mentions 'graph' tool for Blueprint component inspection",
				Desc.Contains(TEXT("graph"), ESearchCase::IgnoreCase));
		});

		It("type description clarifies 'parameters' scope is Materials only", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("inspect tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("type"));
			TestTrue("type description mentions 'Material' scope for parameters",
				Desc.Contains(TEXT("Material"), ESearchCase::CaseSensitive));
		});

		It("type param says connections works for Blueprints and Materials", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("inspect tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("type"));
			TestTrue("type mentions 'Blueprints and Materials' for connections",
				Desc.Contains(TEXT("Blueprints and Materials"), ESearchCase::CaseSensitive));
		});

		It("filter param clarifies it does not match graph names", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("inspect tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("filter"));
			TestTrue("filter description mentions 'graph'",
				Desc.Contains(TEXT("graph"), ESearchCase::IgnoreCase));
		});
	});

	// ── find tool ────────────────────────────────────────────────────────────

	Describe("find tool", [this]()
	{
		It("target param redirects to inspect for Blueprint variables", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("find"));
			if (!TestNotNull("find tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("target"));
			TestTrue("target description mentions 'inspect'",
				Desc.Contains(TEXT("inspect"), ESearchCase::IgnoreCase));
		});
	});

	// ── editor tool ──────────────────────────────────────────────────────────

	Describe("editor tool", [this]()
	{
		It("action description clarifies navigate syncs the Content Browser to an asset path", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("editor"));
			if (!TestNotNull("editor tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("action"));
			TestTrue("action description mentions 'Content Browser' for navigate",
				Desc.Contains(TEXT("Content Browser"), ESearchCase::CaseSensitive));
		});

		It("action description clarifies select, deselect, and focus operate on level actors", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("editor"));
			if (!TestNotNull("editor tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("action"));
			TestTrue("action description mentions 'level actor' scope for select/deselect/focus",
				Desc.Contains(TEXT("level actor"), ESearchCase::IgnoreCase));
		});

		It("target description documents JSON array syntax for multi-select", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("editor"));
			if (!TestNotNull("editor tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("target"));
			TestTrue("target description mentions passing an array for multi-select",
				Desc.Contains(TEXT("array"), ESearchCase::IgnoreCase));
		});
	});

	// ── delete tool ──────────────────────────────────────────────────────────

	Describe("delete tool", [this]()
	{
		It("type param explains folder must be empty", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("delete"));
			if (!TestNotNull("delete tool registered", Tool)) return;
			FString Desc = GetMCPParamDesc(Tool->GetToolInfo(), TEXT("type"));
			TestTrue("type description mentions 'empty'",
				Desc.Contains(TEXT("empty"), ESearchCase::IgnoreCase));
		});
	});
}
