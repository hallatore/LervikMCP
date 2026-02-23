#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "GameFramework/Actor.h"

BEGIN_DEFINE_SPEC(FMCPTool_CreateDirectSpec, "Plugins.LervikMCP.Integration.Tools.Create.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* CreateTool = nullptr;
END_DEFINE_SPEC(FMCPTool_CreateDirectSpec)

void FMCPTool_CreateDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		CreateTool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
	});

	AfterEach([this]()
	{
		CreateTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("create tool is registered", [this]()
		{
			TestNotNull("create tool found", CreateTool);
		});
	});

	Describe("actor location formats", [this]()
	{
		It("spawns actor with array format location [x,y,z]", [this]()
		{
			if (!TestNotNull("create tool found", CreateTool)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				TEXT("{\"type\":\"actor\",\"class\":\"PointLight\",\"name\":\"TestActor_ArrayLoc\",\"location\":[100,200,300]}"));
			if (!TestNotNull("params parsed", Params.Get())) return;

			FMCPToolResult Result = CreateTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Label;
			TestTrue("result has label", Json->TryGetStringField(TEXT("label"), Label));

			const TSharedPtr<FJsonObject>* LocObj = nullptr;
			if (TestTrue("result has location object", Json->TryGetObjectField(TEXT("location"), LocObj)) && LocObj)
			{
				double X = 0.0, Y = 0.0, Z = 0.0;
				(*LocObj)->TryGetNumberField(TEXT("x"), X);
				(*LocObj)->TryGetNumberField(TEXT("y"), Y);
				(*LocObj)->TryGetNumberField(TEXT("z"), Z);
				TestEqual("location X matches", X, 100.0);
				TestEqual("location Y matches", Y, 200.0);
				TestEqual("location Z matches", Z, 300.0);
			}
		});

		It("spawns actor with object format location {x,y,z}", [this]()
		{
			if (!TestNotNull("create tool found", CreateTool)) return;

			TSharedPtr<FJsonObject> Params = FMCPToolDirectTestHelper::MakeParamsFromJson(
				TEXT("{\"type\":\"actor\",\"class\":\"PointLight\",\"name\":\"TestActor_ObjLoc\",\"location\":{\"x\":100,\"y\":200,\"z\":300}}"));
			if (!TestNotNull("params parsed", Params.Get())) return;

			FMCPToolResult Result = CreateTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Label;
			TestTrue("result has label", Json->TryGetStringField(TEXT("label"), Label));

			const TSharedPtr<FJsonObject>* LocObj = nullptr;
			if (TestTrue("result has location object", Json->TryGetObjectField(TEXT("location"), LocObj)) && LocObj)
			{
				double X = 0.0, Y = 0.0, Z = 0.0;
				(*LocObj)->TryGetNumberField(TEXT("x"), X);
				(*LocObj)->TryGetNumberField(TEXT("y"), Y);
				(*LocObj)->TryGetNumberField(TEXT("z"), Z);
				TestEqual("location X matches", X, 100.0);
				TestEqual("location Y matches", Y, 200.0);
				TestEqual("location Z matches", Z, 300.0);
			}
		});
	});

	Describe("template duplication with explicit location", [this]()
	{
		It("duplicate actor is placed at absolute location, not offset from template", [this]()
		{
			if (!TestNotNull("create tool found", CreateTool)) return;

			// Spawn template actor at [100, 200, 300]
			TSharedPtr<FJsonObject> TemplateParams = FMCPToolDirectTestHelper::MakeParamsFromJson(
				TEXT("{\"type\":\"actor\",\"class\":\"PointLight\",\"name\":\"TemplateActor_LocTest\",\"location\":[100,200,300]}"));
			if (!TestNotNull("template params parsed", TemplateParams.Get())) return;

			FMCPToolResult TemplateResult = CreateTool->Execute(TemplateParams);
			if (!TestFalse("template creation is not an error", TemplateResult.bIsError)) return;

			TSharedPtr<FJsonObject> TemplateJson = FMCPToolDirectTestHelper::ParseResultJson(TemplateResult);
			if (!TestNotNull("template result parses as JSON", TemplateJson.Get())) return;

			FString TemplateLabel;
			if (!TestTrue("template result has label", TemplateJson->TryGetStringField(TEXT("label"), TemplateLabel))) return;

			// Duplicate template, requesting absolute location [500, 500, 500]
			FString DuplicateParamsStr = FString::Printf(
				TEXT("{\"type\":\"actor\",\"template\":\"%s\",\"name\":\"DuplicateActor\",\"location\":[500,500,500]}"),
				*TemplateLabel);
			TSharedPtr<FJsonObject> DuplicateParams = FMCPToolDirectTestHelper::MakeParamsFromJson(*DuplicateParamsStr);
			if (!TestNotNull("duplicate params parsed", DuplicateParams.Get())) return;

			FMCPToolResult DuplicateResult = CreateTool->Execute(DuplicateParams);
			if (!TestFalse("duplicate result is not an error", DuplicateResult.bIsError))
			{
				AddError(FString::Printf(TEXT("Duplicate error content: %s"), *DuplicateResult.Content));
				return;
			}

			TSharedPtr<FJsonObject> DuplicateJson = FMCPToolDirectTestHelper::ParseResultJson(DuplicateResult);
			if (!TestNotNull("duplicate result parses as JSON", DuplicateJson.Get())) return;

			const TSharedPtr<FJsonObject>* LocObj = nullptr;
			if (TestTrue("duplicate result has location", DuplicateJson->TryGetObjectField(TEXT("location"), LocObj)) && LocObj)
			{
				double X = 0.0, Y = 0.0, Z = 0.0;
				(*LocObj)->TryGetNumberField(TEXT("x"), X);
				(*LocObj)->TryGetNumberField(TEXT("y"), Y);
				(*LocObj)->TryGetNumberField(TEXT("z"), Z);
				TestEqual("duplicate location X is absolute", X, 500.0);
				TestEqual("duplicate location Y is absolute", Y, 500.0);
				TestEqual("duplicate location Z is absolute", Z, 500.0);
			}
		});
	});
}
