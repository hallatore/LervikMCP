#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "UObject/UnrealType.h"

BEGIN_DEFINE_SPEC(FMCPTool_ModifyDirectSpec, "Plugins.LervikMCP.Integration.Tools.Modify.Direct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* ModifyTool = nullptr;
END_DEFINE_SPEC(FMCPTool_ModifyDirectSpec)

void FMCPTool_ModifyDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		ModifyTool = FMCPToolDirectTestHelper::FindTool(TEXT("modify"));
	});

	AfterEach([this]()
	{
		ModifyTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("modify tool is registered", [this]()
		{
			TestNotNull("modify tool found", ModifyTool);
		});
	});

	Describe("property modification", [this]()
	{
		It("modifies a material property and lists it in modified", [this]()
		{
			if (!TestNotNull("modify tool found", ModifyTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ModifyProp"));
			if (!TestNotNull("material created", Mat)) return;

			Mat->TwoSided = false;

			TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
			PropsObj->SetStringField(TEXT("TwoSided"), TEXT("true"));

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat));
			Params->SetObjectField(TEXT("properties"), PropsObj);

			FMCPToolResult Result = ModifyTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			FBoolProperty* Prop = FindFProperty<FBoolProperty>(UMaterial::StaticClass(), TEXT("TwoSided"));
			if (TestNotNull("TwoSided property found via reflection", Prop))
			{
				TestTrue("TwoSided was set to true", Prop->GetPropertyValue_InContainer(Mat));
			}

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Modified = nullptr;
			if (TestTrue("result has modified array", Json->TryGetArrayField(TEXT("modified"), Modified)) && Modified)
			{
				bool bFound = false;
				for (const TSharedPtr<FJsonValue>& V : *Modified)
				{
					if (V->AsString() == TEXT("TwoSided")) { bFound = true; break; }
				}
				TestTrue("modified contains TwoSided", bFound);
			}
		});

		It("reports unrecognized properties in a warnings array", [this]()
		{
			if (!TestNotNull("modify tool found", ModifyTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_ModifyBadProp"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
			PropsObj->SetStringField(TEXT("FakeProperty987"), TEXT("somevalue"));

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat));
			Params->SetObjectField(TEXT("properties"), PropsObj);

			FMCPToolResult Result = ModifyTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			if (TestTrue("result has warnings array for unknown properties",
				Json->TryGetArrayField(TEXT("warnings"), Warnings)) && Warnings)
			{
				bool bFound = false;
				for (const TSharedPtr<FJsonValue>& V : *Warnings)
				{
					if (V->AsString().Contains(TEXT("FakeProperty987"))) { bFound = true; break; }
				}
				TestTrue("warnings mentions FakeProperty987", bFound);
			}
		});
	});

	Describe("transform on non-actor target", [this]()
	{
		It("uses warnings array (not warning string) when target is not an Actor", [this]()
		{
			if (!TestNotNull("modify tool found", ModifyTool)) return;

			UMaterial* Mat = Helper.CreateTransientMaterial(TEXT("TestMat_NonActorTransform"));
			if (!TestNotNull("material created", Mat)) return;

			TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> LocArr;
			LocArr.Add(MakeShared<FJsonValueNumber>(0.0));
			LocArr.Add(MakeShared<FJsonValueNumber>(0.0));
			LocArr.Add(MakeShared<FJsonValueNumber>(0.0));
			TransformObj->SetArrayField(TEXT("location"), LocArr);

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("target"), FMCPToolDirectTestHelper::GetAssetPath(Mat));
			Params->SetObjectField(TEXT("transform"), TransformObj);

			FMCPToolResult Result = ModifyTool->Execute(Params);
			TestFalse("result is not an error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* Warnings = nullptr;
			TestTrue("result has warnings array", Json->TryGetArrayField(TEXT("warnings"), Warnings));
			TestFalse("result has no singular warning field", Json->HasField(TEXT("warning")));
		});
	});
}
