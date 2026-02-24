#include "Misc/AutomationTest.h"
#include "MCPToolDirectTestHelper.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

BEGIN_DEFINE_SPEC(FMCPTool_TraceDirectSpec, "Plugins.LervikMCP.Integration.Tools.Trace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	FMCPToolDirectTestHelper Helper;
	IMCPTool* TraceTool = nullptr;
END_DEFINE_SPEC(FMCPTool_TraceDirectSpec)

void FMCPTool_TraceDirectSpec::Define()
{
	BeforeEach([this]()
	{
		Helper.Setup(this);
		TraceTool = FMCPToolDirectTestHelper::FindTool(TEXT("trace"));
		// Ensure any leftover trace is stopped before each test (via game thread).
		if (TraceTool)
			TraceTool->Execute(FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } }));
	});

	AfterEach([this]()
	{
		// Route stop through the tool's ExecuteOnGameThread to ensure trace state
		// is fully updated on the game thread before the next test begins.
		if (TraceTool)
			TraceTool->Execute(FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } }));
		TraceTool = nullptr;
		Helper.Cleanup();
	});

	Describe("tool availability", [this]()
	{
		It("trace tool is registered", [this]()
		{
			TestNotNull("trace tool found", TraceTool);
		});
	});

	Describe("parameter validation", [this]()
	{
		It("returns error for missing action", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(MakeShared<FJsonObject>());
			TestTrue("missing action returns error", Result.bIsError);
		});

		It("returns error for unknown action", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("bogus") }
				})
			);
			TestTrue("unknown action returns error", Result.bIsError);
		});
	});

	Describe("trace recording", [this]()
	{
		It("status returns not connected when idle", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("status") } })
			);
			TestFalse("status is not an error", Result.bIsError);
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			bool bConnected = true;
			Json->TryGetBoolField(TEXT("connected"), bConnected);
			TestFalse("connected is false when idle", bConnected);
		});

		It("start begins recording", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("start") } })
			);
			TestFalse("start is not an error", Result.bIsError);
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			FString FilePath;
			Json->TryGetStringField(TEXT("path"), FilePath);
			TestFalse("path is non-empty", FilePath.IsEmpty());
		});

		It("status shows connected after start", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Profiling/MCPTest_%s.utrace"), *FGuid::NewGuid().ToString());
			TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("start") }, { TEXT("path"), UniquePath } })
			);

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("status") } })
			);
			TestFalse("status is not an error", Result.bIsError);
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			bool bConnected = false;
			Json->TryGetBoolField(TEXT("connected"), bConnected);
			TestTrue("connected is true after start", bConnected);
			FString FilePath;
			Json->TryGetStringField(TEXT("path"), FilePath);
			TestFalse("path is non-empty", FilePath.IsEmpty());
		});

		It("start while active returns error", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Profiling/MCPTest_%s.utrace"), *FGuid::NewGuid().ToString());
			TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("start") }, { TEXT("path"), UniquePath } })
			);
			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("start") } })
			);
			TestTrue("second start returns error", Result.bIsError);
			TestTrue("error mentions already active", Result.Content.Contains(TEXT("already active")));
		});

		It("stop ends recording", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Profiling/MCPTest_%s.utrace"), *FGuid::NewGuid().ToString());
			FMCPToolResult StartResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("start") }, { TEXT("path"), UniquePath } })
			);
			if (!TestFalse("start is not an error", StartResult.bIsError)) return;

			TSharedPtr<FJsonObject> StartJson = FMCPToolDirectTestHelper::ParseResultJson(StartResult);
			FString StartFilePath;
			if (StartJson.IsValid()) StartJson->TryGetStringField(TEXT("path"), StartFilePath);

			FMCPToolResult StopResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } })
			);
			TestFalse("stop is not an error", StopResult.bIsError);
			TSharedPtr<FJsonObject> StopJson = FMCPToolDirectTestHelper::ParseResultJson(StopResult);
			if (!TestNotNull("stop result JSON parsed", StopJson.Get())) return;
			FString StopFilePath;
			StopJson->TryGetStringField(TEXT("path"), StopFilePath);
			TestFalse("stop returns non-empty file path", StopFilePath.IsEmpty());
			TestEqual("stop path matches start path", StopFilePath, StartFilePath);
		});

		It("stop when idle returns error", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } })
			);
			TestTrue("stop when idle returns error", Result.bIsError);
		});
	});

	Describe("analyze", [this]()
	{
		It("returns error when path param is missing", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("analyze") } })
			);
			TestTrue("analyze with no path returns error", Result.bIsError);
			TestTrue("error mentions 'path'", Result.Content.Contains(TEXT("path")));
		});

		It("returns error for non-existent file", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TEXT("C:/fake/nonexistent_path.utrace") }
				})
			);
			TestTrue("analyze with bad path returns error", Result.bIsError);
			TestTrue("error mentions 'not found'", Result.Content.Contains(TEXT("not found")));
		});

		It("returns frame stats for recorded trace", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPAnalyzeTest_%s.utrace"), *FGuid::NewGuid().ToString());

			// Start recording
			FMCPToolResult StartResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("start") },
					{ TEXT("path"),   UniquePath }
				})
			);
			if (!TestFalse("start is not an error", StartResult.bIsError)) return;

			// Record for a short time
			FPlatformProcess::Sleep(0.2f);

			// Stop recording
			FMCPToolResult StopResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } })
			);
			if (!TestFalse("stop is not an error", StopResult.bIsError)) return;

			// Let the file flush
			FPlatformProcess::Sleep(0.1f);

			// Retrieve the actual path from stop result (may differ from requested path)
			FString TracePath = UniquePath;
			TSharedPtr<FJsonObject> StopJson = FMCPToolDirectTestHelper::ParseResultJson(StopResult);
			if (StopJson.IsValid())
				StopJson->TryGetStringField(TEXT("path"), TracePath);

			// Analyze
			FMCPToolResult AnalyzeResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath }
				})
			);
			if (!TestFalse("analyze is not an error", AnalyzeResult.bIsError)) return;

			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(AnalyzeResult);
			if (!TestNotNull("analyze result JSON parsed", Json.Get())) return;

			// Verify required fields exist and are non-negative
			double FrameCount = -1.0;
			double AvgMs = -1.0;
			double MinMs = -1.0;
			double MaxMs = -1.0;
			TestTrue("frame_count field present",       Json->TryGetNumberField(TEXT("frame_count"),       FrameCount));
			TestTrue("avg_frame_time_ms field present", Json->TryGetNumberField(TEXT("avg_frame_time_ms"), AvgMs));
			TestTrue("min_frame_time_ms field present", Json->TryGetNumberField(TEXT("min_frame_time_ms"), MinMs));
			TestTrue("max_frame_time_ms field present", Json->TryGetNumberField(TEXT("max_frame_time_ms"), MaxMs));
			TestTrue("frame_count >= 0",       FrameCount >= 0.0);
			TestTrue("avg_frame_time_ms >= 0", AvgMs      >= 0.0);
			TestTrue("min_frame_time_ms >= 0", MinMs      >= 0.0);
			TestTrue("max_frame_time_ms >= 0", MaxMs      >= 0.0);

			double RenderFrameCount = -1.0;
			TestTrue("render_frame_count field present", Json->TryGetNumberField(TEXT("render_frame_count"), RenderFrameCount));
			TestTrue("render_frame_count >= 0", RenderFrameCount >= 0.0);

			// GPU array must be present
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray));

			// CPU array must be present
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray));

			double CpuFrameCount = -1.0;
			TestTrue("cpu_frame_count field present", Json->TryGetNumberField(TEXT("cpu_frame_count"), CpuFrameCount));
			TestTrue("cpu_frame_count >= 0", CpuFrameCount >= 0.0);
		});
	});

	Describe("test action", [this]()
	{
		It("runs start-sleep-stop cycle", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPTestAction_%s.utrace"), *FGuid::NewGuid().ToString());

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("test") },
					{ TEXT("path"),   UniquePath }
				})
			);
			TestFalse("test is not an error", Result.bIsError);
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			FString StartPath, StopPath;
			Json->TryGetStringField(TEXT("start_path"), StartPath);
			Json->TryGetStringField(TEXT("stop_path"), StopPath);

			TestFalse("start_path non-empty", StartPath.IsEmpty());
			TestFalse("stop_path non-empty", StopPath.IsEmpty());
			TestEqual("start and stop paths match", StartPath, StopPath);
		});

		It("returns error when trace already active", [this]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPTestActive_%s.utrace"), *FGuid::NewGuid().ToString());
			TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("start") },
					{ TEXT("path"),   UniquePath }
				})
			);

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("test") } })
			);
			TestTrue("test while active returns error", Result.bIsError);
			TestTrue("error mentions already active", Result.Content.Contains(TEXT("already active")));
		});

		LatentIt("captures frames during recording", EAsyncExecution::ThreadPool,
			FTimespan::FromSeconds(30), [this](const FDoneDelegate& Done)
		{
			if (!TestNotNull("trace tool found", TraceTool)) { Done.Execute(); return; }

			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPTestFrames_%s.utrace"), *FGuid::NewGuid().ToString());

			// Execute on thread pool thread — Sleep(5s) won't block game thread
			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("test") },
					{ TEXT("path"),   UniquePath }
				})
			);
			TestFalse("test is not an error", Result.bIsError);

			FPlatformProcess::Sleep(0.2f);

			FString TracePath = UniquePath;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (Json.IsValid())
				Json->TryGetStringField(TEXT("stop_path"), TracePath);

			FMCPToolResult AnalyzeResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath }
				})
			);
			TestFalse("analyze is not an error", AnalyzeResult.bIsError);

			TSharedPtr<FJsonObject> AnalyzeJson = FMCPToolDirectTestHelper::ParseResultJson(AnalyzeResult);
			if (TestNotNull("analyze JSON parsed", AnalyzeJson.Get()))
			{
				double FrameCount = 0;
				AnalyzeJson->TryGetNumberField(TEXT("frame_count"), FrameCount);
				TestTrue("frame_count > 0 (frames were actually captured)", FrameCount > 0);
			}

			Done.Execute();
		});


	});

	Describe("gpu tree", [this]()
	{
		// Helper: start, sleep, stop, sleep, return trace path
		auto RecordTrace = [this]() -> FString
		{
			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPGpuTest_%s.utrace"), *FGuid::NewGuid().ToString());
			TraceTool->Execute(FMCPToolDirectTestHelper::MakeParams({
				{ TEXT("action"), TEXT("start") }, { TEXT("path"), UniquePath }
			}));
			FPlatformProcess::Sleep(0.2f);
			FMCPToolResult StopResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } }));
			FPlatformProcess::Sleep(0.1f);
			FString TracePath = UniquePath;
			TSharedPtr<FJsonObject> StopJson = FMCPToolDirectTestHelper::ParseResultJson(StopResult);
			if (StopJson.IsValid())
				StopJson->TryGetStringField(TEXT("path"), TracePath);
			return TracePath;
		};

		It("depth=0 returns empty gpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;
			TestTrue("gpu array is empty for depth=0", GpuArray->IsEmpty());
		});

		It("min_ms=99999 returns empty gpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("min_ms"), TEXT("99999") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;
			TestTrue("gpu array is empty with extreme min_ms", GpuArray->IsEmpty());
		});

		It("filter with no matches returns empty gpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("filter"), TEXT("ZZNONEXISTENT_99") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;
			TestTrue("gpu array is empty for non-matching filter", GpuArray->IsEmpty());
		});

		It("empty filter preserves depth behavior", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("filter"), TEXT("") },
					{ TEXT("depth"),  TEXT("1") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;

			// Empty filter falls through to depth pruning — top-level children should be empty
			for (const auto& NodeVal : *GpuArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!NodeVal.IsValid() || !NodeVal->TryGetObject(NodeObj)) continue;
				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				TestTrue("node has children field", (*NodeObj)->TryGetArrayField(TEXT("children"), Children));
				if (Children)
					TestTrue("top-level node children empty at depth=1 with empty filter", Children->IsEmpty());
			}
		});

		It("default depth nodes have empty children arrays", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("1") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;

			// Each top-level GPU node should have an empty children array (depth=1 prunes grandchildren)
			for (const auto& NodeVal : *GpuArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!NodeVal.IsValid() || !NodeVal->TryGetObject(NodeObj)) continue;
				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				TestTrue("node has children field", (*NodeObj)->TryGetArrayField(TEXT("children"), Children));
				if (Children)
					TestTrue("top-level node children empty at depth=1", Children->IsEmpty());
			}
		});

		It("filter with min_ms leaves no orphan ancestors", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			// First pass: discover GPU data
			FMCPToolResult BaseResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("base analyze not error", BaseResult.bIsError)) return;
			TSharedPtr<FJsonObject> BaseJson = FMCPToolDirectTestHelper::ParseResultJson(BaseResult);
			if (!TestNotNull("base JSON parsed", BaseJson.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* BaseGpu = nullptr;
			BaseJson->TryGetArrayField(TEXT("gpu"), BaseGpu);
			if (!BaseGpu || BaseGpu->IsEmpty()) return;

			double MinTopMs = TNumericLimits<double>::Max();
			for (const auto& Val : *BaseGpu)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Val.IsValid() || !Val->TryGetObject(Obj)) continue;
				double AvgMs = 0.0;
				(*Obj)->TryGetNumberField(TEXT("avg_ms"), AvgMs);
				MinTopMs = FMath::Min(MinTopMs, AvgMs);
			}
			if (MinTopMs <= 0.0 || !FMath::IsFinite(MinTopMs)) return;

			FString MinMsStr = FString::SanitizeFloat(MinTopMs * 0.5);

			FMCPToolResult FilterResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("filter"), TEXT("a") },
					{ TEXT("min_ms"), MinMsStr }
				})
			);
			if (!TestFalse("filter analyze not error", FilterResult.bIsError)) return;
			TSharedPtr<FJsonObject> FilterJson = FMCPToolDirectTestHelper::ParseResultJson(FilterResult);
			if (!TestNotNull("filter JSON parsed", FilterJson.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", FilterJson->TryGetArrayField(TEXT("gpu"), GpuArray))) return;

			for (const auto& Val : *GpuArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!Val.IsValid() || !Val->TryGetObject(NodeObj)) continue;
				FString NodeName;
				(*NodeObj)->TryGetStringField(TEXT("name"), NodeName);
				TestTrue(
					FString::Printf(TEXT("node '%s' matches filter"), *NodeName),
					NodeName.Contains(TEXT("a"), ESearchCase::IgnoreCase)
				);
			}
		});

		It("gpu nodes have required JSON fields", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("2") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* GpuArray = nullptr;
			if (!TestTrue("gpu field present", Json->TryGetArrayField(TEXT("gpu"), GpuArray))) return;
			if (GpuArray->IsEmpty()) return;

			auto VerifyNodeFields = [this](const TSharedPtr<FJsonObject>& Node, const FString& Context)
			{
				FString Name;
				double Count = -1.0, AvgMs = -1.0, NodeMinMs = -1.0, NodeMaxMs = -1.0;
				TestTrue(Context + TEXT(" has name (string)"),   Node->TryGetStringField(TEXT("name"),   Name));
				TestTrue(Context + TEXT(" has count (number)"),  Node->TryGetNumberField(TEXT("count"),  Count));
				TestTrue(Context + TEXT(" has avg_ms (number)"), Node->TryGetNumberField(TEXT("avg_ms"), AvgMs));
				TestTrue(Context + TEXT(" has min_ms (number)"), Node->TryGetNumberField(TEXT("min_ms"), NodeMinMs));
				TestTrue(Context + TEXT(" has max_ms (number)"), Node->TryGetNumberField(TEXT("max_ms"), NodeMaxMs));
				const TArray<TSharedPtr<FJsonValue>>* NodeChildren = nullptr;
				TestTrue(Context + TEXT(" has children (array)"), Node->TryGetArrayField(TEXT("children"), NodeChildren));
			};

			const TSharedPtr<FJsonObject>* FirstNodeObj = nullptr;
			if (!(*GpuArray)[0].IsValid() || !(*GpuArray)[0]->TryGetObject(FirstNodeObj)) return;
			VerifyNodeFields(*FirstNodeObj, TEXT("first gpu node"));

			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (!(*FirstNodeObj)->TryGetArrayField(TEXT("children"), Children) || Children->IsEmpty()) return;

			const TSharedPtr<FJsonObject>* FirstChildObj = nullptr;
			if (!(*Children)[0].IsValid() || !(*Children)[0]->TryGetObject(FirstChildObj)) return;
			VerifyNodeFields(*FirstChildObj, TEXT("first gpu node child"));
		});
	});

	Describe("cpu tree", [this]()
	{
		auto RecordTrace = [this]() -> FString
		{
			FString UniquePath = FPaths::ProjectSavedDir() / FString::Printf(
				TEXT("Profiling/MCPCpuTest_%s.utrace"), *FGuid::NewGuid().ToString());
			TraceTool->Execute(FMCPToolDirectTestHelper::MakeParams({
				{ TEXT("action"), TEXT("start") }, { TEXT("path"), UniquePath }
			}));
			FPlatformProcess::Sleep(0.2f);
			FMCPToolResult StopResult = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({ { TEXT("action"), TEXT("stop") } }));
			FPlatformProcess::Sleep(0.1f);
			FString TracePath = UniquePath;
			TSharedPtr<FJsonObject> StopJson = FMCPToolDirectTestHelper::ParseResultJson(StopResult);
			if (StopJson.IsValid())
				StopJson->TryGetStringField(TEXT("path"), TracePath);
			return TracePath;
		};

		It("depth=0 returns empty cpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;
			TestTrue("cpu array is empty for depth=0", CpuArray->IsEmpty());
		});

		It("min_ms=99999 returns empty cpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("min_ms"), TEXT("99999") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;
			TestTrue("cpu array is empty with extreme min_ms", CpuArray->IsEmpty());
		});

		It("filter with no matches returns empty cpu array", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("filter"), TEXT("ZZNONEXISTENT_99") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;
			TestTrue("cpu array is empty for non-matching filter", CpuArray->IsEmpty());
		});

		It("default depth nodes have empty children arrays", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("1") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;

			for (const auto& NodeVal : *CpuArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!NodeVal.IsValid() || !NodeVal->TryGetObject(NodeObj)) continue;
				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				TestTrue("node has children field", (*NodeObj)->TryGetArrayField(TEXT("children"), Children));
				if (Children)
					TestTrue("top-level node children empty at depth=1", Children->IsEmpty());
			}
		});

		It("cpu nodes have required JSON fields", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("depth"),  TEXT("2") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;

			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;
			if (CpuArray->IsEmpty()) return;

			auto VerifyNodeFields = [this](const TSharedPtr<FJsonObject>& Node, const FString& Context)
			{
				FString Name;
				double Count = -1.0, AvgMs = -1.0, NodeMinMs = -1.0, NodeMaxMs = -1.0;
				TestTrue(Context + TEXT(" has name (string)"),   Node->TryGetStringField(TEXT("name"),   Name));
				TestTrue(Context + TEXT(" has count (number)"),  Node->TryGetNumberField(TEXT("count"),  Count));
				TestTrue(Context + TEXT(" has avg_ms (number)"), Node->TryGetNumberField(TEXT("avg_ms"), AvgMs));
				TestTrue(Context + TEXT(" has min_ms (number)"), Node->TryGetNumberField(TEXT("min_ms"), NodeMinMs));
				TestTrue(Context + TEXT(" has max_ms (number)"), Node->TryGetNumberField(TEXT("max_ms"), NodeMaxMs));
				const TArray<TSharedPtr<FJsonValue>>* NodeChildren = nullptr;
				TestTrue(Context + TEXT(" has children (array)"), Node->TryGetArrayField(TEXT("children"), NodeChildren));
			};

			const TSharedPtr<FJsonObject>* FirstNodeObj = nullptr;
			if (!(*CpuArray)[0].IsValid() || !(*CpuArray)[0]->TryGetObject(FirstNodeObj)) return;
			VerifyNodeFields(*FirstNodeObj, TEXT("first cpu node"));

			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (!(*FirstNodeObj)->TryGetArrayField(TEXT("children"), Children) || Children->IsEmpty()) return;

			const TSharedPtr<FJsonObject>* FirstChildObj = nullptr;
			if (!(*Children)[0].IsValid() || !(*Children)[0]->TryGetObject(FirstChildObj)) return;
			VerifyNodeFields(*FirstChildObj, TEXT("first cpu node child"));
		});

		It("empty filter preserves depth behavior", [this, RecordTrace]()
		{
			if (!TestNotNull("trace tool found", TraceTool)) return;
			FString TracePath = RecordTrace();

			FMCPToolResult Result = TraceTool->Execute(
				FMCPToolDirectTestHelper::MakeParams({
					{ TEXT("action"), TEXT("analyze") },
					{ TEXT("path"),   TracePath },
					{ TEXT("filter"), TEXT("") },
					{ TEXT("depth"),  TEXT("1") },
					{ TEXT("min_ms"), TEXT("0") }
				})
			);
			if (!TestFalse("analyze is not an error", Result.bIsError)) return;
			TSharedPtr<FJsonObject> Json = FMCPToolDirectTestHelper::ParseResultJson(Result);
			if (!TestNotNull("result JSON parsed", Json.Get())) return;
			const TArray<TSharedPtr<FJsonValue>>* CpuArray = nullptr;
			if (!TestTrue("cpu field present", Json->TryGetArrayField(TEXT("cpu"), CpuArray))) return;

			for (const auto& NodeVal : *CpuArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!NodeVal.IsValid() || !NodeVal->TryGetObject(NodeObj)) continue;
				const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
				TestTrue("node has children field", (*NodeObj)->TryGetArrayField(TEXT("children"), Children));
				if (Children)
					TestTrue("top-level node children empty at depth=1 with empty filter", Children->IsEmpty());
			}
		});
	});
}
