#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "MCPToolHelp.h"

BEGIN_DEFINE_SPEC(FMCPToolHelpSpec, "Plugins.LervikMCP.Integration.Tools.Help",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMCPToolHelpSpec)

void FMCPToolHelpSpec::Define()
{
	// All tool names that should support help
	static const TArray<FString> AllTools = {
		TEXT("get_open_assets"),
		TEXT("find"),
		TEXT("inspect"),
		TEXT("modify"),
		TEXT("create"),
		TEXT("delete"),
		TEXT("editor"),
		TEXT("graph"),
		TEXT("execute"),
		TEXT("execute_python"),
		TEXT("trace"),
	};

	Describe("help=true returns overview", [this]()
	{
		for (const FString& ToolName : AllTools)
		{
			It(FString::Printf(TEXT("%s returns help overview"), *ToolName), [this, ToolName]()
			{
				IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(ToolName);
				if (!TestNotNull(*FString::Printf(TEXT("%s tool found"), *ToolName), Tool)) return;

				TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
				Params->SetStringField(TEXT("help"), TEXT("true"));

				FMCPToolResult Result = Tool->Execute(Params);
				TestFalse("result is not error", Result.bIsError);

				TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
				if (!TestNotNull("result parses as JSON", Json.Get())) return;

				FString ToolField;
				TestTrue("has 'tool' field", Json->TryGetStringField(TEXT("tool"), ToolField));

				bool bHelp;
				TestTrue("has 'help' field set to true", Json->TryGetBoolField(TEXT("help"), bHelp) && bHelp);

				FString Desc;
				TestTrue("has 'description' field", Json->TryGetStringField(TEXT("description"), Desc));
				TestFalse("description is not empty", Desc.IsEmpty());
			});
		}
	});

	Describe("detailed help for action-based tools", [this]()
	{
		// Map of tool → a valid sub-action to get detailed help for
		static const TMap<FString, FString> ActionTools = {
			{ TEXT("find"),    TEXT("asset")    },
			{ TEXT("inspect"), TEXT("properties") },
			{ TEXT("create"),  TEXT("actor")    },
			{ TEXT("delete"),  TEXT("node")     },
			{ TEXT("editor"),  TEXT("open")     },
			{ TEXT("graph"),   TEXT("add_node") },
			{ TEXT("execute"), TEXT("get_cvar") },
			{ TEXT("trace"),   TEXT("start")    },
		};

		for (const auto& Pair : ActionTools)
		{
			It(FString::Printf(TEXT("%s returns detailed help for '%s'"), *Pair.Key, *Pair.Value), [this, Pair]()
			{
				IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(Pair.Key);
				if (!TestNotNull(*FString::Printf(TEXT("%s tool found"), *Pair.Key), Tool)) return;

				TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
				Params->SetStringField(TEXT("help"), Pair.Value);

				FMCPToolResult Result = Tool->Execute(Params);
				TestFalse("result is not error", Result.bIsError);

				TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
				if (!TestNotNull("result parses as JSON", Json.Get())) return;

				// Should have parameters array
				const TArray<TSharedPtr<FJsonValue>>* ParamsArr = nullptr;
				TestTrue("has 'parameters' array", Json->TryGetArrayField(TEXT("parameters"), ParamsArr));
				if (ParamsArr)
				{
					TestTrue("parameters array is not empty", ParamsArr->Num() > 0);
				}
			});
		}
	});

	Describe("invalid help topic returns error", [this]()
	{
		It("graph tool returns error for unknown topic", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("nonexistent_action"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("error mentions valid topics", Result.Content.Contains(TEXT("add_node")));
		});
	});

	Describe("graph action=help backward compat", [this]()
	{
		It("action=help still works", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("graph tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("action"), TEXT("help"));
			Params->SetStringField(TEXT("target"), TEXT("dummy"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString ToolField;
			TestTrue("has 'tool' field", Json->TryGetStringField(TEXT("tool"), ToolField));
		});
	});

	Describe("skills system", [this]()
	{
		It("help=skills returns skill list", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("skills"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			bool bHelp;
			TestTrue("has help=true", Json->TryGetBoolField(TEXT("help"), bHelp) && bHelp);

			const TArray<TSharedPtr<FJsonValue>>* SkillsArr = nullptr;
			TestTrue("has 'skills' array", Json->TryGetArrayField(TEXT("skills"), SkillsArr));
			if (SkillsArr)
			{
				TestEqual("skills count is 3", SkillsArr->Num(), 3);
				// Each entry should have name, title, description
				for (const auto& Val : *SkillsArr)
				{
					const TSharedPtr<FJsonObject>* Obj = nullptr;
					if (Val->TryGetObject(Obj) && Obj)
					{
						FString Name;
						TestTrue("skill has name", (*Obj)->TryGetStringField(TEXT("name"), Name));
						FString Desc;
						TestTrue("skill has description", (*Obj)->TryGetStringField(TEXT("description"), Desc));
					}
				}
			}
		});

		It("help=skill:materials returns materials skill", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("inspect"));
			if (!TestNotNull("tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("skill:materials"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Skill;
			TestTrue("has skill=materials", Json->TryGetStringField(TEXT("skill"), Skill) && Skill == TEXT("materials"));

			FString Prerequisites;
			TestTrue("has prerequisites", Json->TryGetStringField(TEXT("prerequisites"), Prerequisites));

			const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
			TestTrue("has steps array", Json->TryGetArrayField(TEXT("steps"), StepsArr));
			if (StepsArr)
			{
				TestTrue("steps count > 0", StepsArr->Num() > 0);
			}

			FString Tips;
			TestTrue("has tips", Json->TryGetStringField(TEXT("tips"), Tips));
			TestFalse("tips not empty", Tips.IsEmpty());
		});

		It("help=skill:blueprints returns blueprints skill", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("create"));
			if (!TestNotNull("tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("skill:blueprints"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Skill;
			TestTrue("has skill=blueprints", Json->TryGetStringField(TEXT("skill"), Skill) && Skill == TEXT("blueprints"));

			const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
			TestTrue("has steps array", Json->TryGetArrayField(TEXT("steps"), StepsArr));
			if (StepsArr)
			{
				TestTrue("steps count > 0", StepsArr->Num() > 0);
			}
		});

		It("help=skill:profiling returns profiling skill", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("trace"));
			if (!TestNotNull("tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("skill:profiling"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestFalse("result is not error", Result.bIsError);

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result parses as JSON", Json.Get())) return;

			FString Skill;
			TestTrue("has skill=profiling", Json->TryGetStringField(TEXT("skill"), Skill) && Skill == TEXT("profiling"));

			const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
			TestTrue("has steps array", Json->TryGetArrayField(TEXT("steps"), StepsArr));
			if (StepsArr)
			{
				TestTrue("steps count > 0", StepsArr->Num() > 0);
			}
		});

		It("help=skill:nonexistent returns error", [this]()
		{
			IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(TEXT("graph"));
			if (!TestNotNull("tool found", Tool)) return;

			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("help"), TEXT("skill:nonexistent"));

			FMCPToolResult Result = Tool->Execute(Params);
			TestTrue("result is error", Result.bIsError);
			TestTrue("error mentions valid skills", Result.Content.Contains(TEXT("materials")));
			TestTrue("error mentions valid skills", Result.Content.Contains(TEXT("blueprints")));
			TestTrue("error mentions valid skills", Result.Content.Contains(TEXT("profiling")));
		});

		It("skills accessible from any tool", [this]()
		{
			// Test on different tools to prove it works everywhere
			static const TArray<FString> TestTools = { TEXT("find"), TEXT("execute"), TEXT("modify") };
			for (const FString& ToolName : TestTools)
			{
				IMCPTool* Tool = FMCPToolDirectTestHelper::FindTool(ToolName);
				if (!TestNotNull(*FString::Printf(TEXT("%s tool found"), *ToolName), Tool)) continue;

				TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
				Params->SetStringField(TEXT("help"), TEXT("skills"));

				FMCPToolResult Result = Tool->Execute(Params);
				TestFalse(*FString::Printf(TEXT("%s: result is not error"), *ToolName), Result.bIsError);
			}
		});
	});
}
