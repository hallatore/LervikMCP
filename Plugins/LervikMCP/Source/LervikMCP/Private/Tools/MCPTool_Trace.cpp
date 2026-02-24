#include "Tools/MCPTool_Trace.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPToolHelp.h"
#include "Tools/TraceAnalyzer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

namespace {

TSharedPtr<FJsonObject> TimingNodeToJson(const FTraceTimingNode& Node)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("name"),   Node.Name);
    Obj->SetNumberField(TEXT("count"),  Node.Count);
    Obj->SetField(TEXT("avg_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.GetAvgMs()));
    Obj->SetField(TEXT("min_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.Count > 0 ? Node.MinMs : 0.0));
    Obj->SetField(TEXT("max_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.MaxMs));

    TArray<TSharedPtr<FJsonValue>> ChildArray;
    for (const auto& Child : Node.Children)
        ChildArray.Add(MakeShared<FJsonValueObject>(TimingNodeToJson(Child)));
    Obj->SetArrayField(TEXT("children"), ChildArray);

    return Obj;
}

// ── Help data ────────────────────────────────────────────────────────────

static const FMCPParamHelp sTraceStartParams[] = {
    { TEXT("path"),     TEXT("string"),  false, TEXT("Optional output .utrace file path"), nullptr, nullptr },
};

static const FMCPParamHelp sTraceAnalyzeParams[] = {
    { TEXT("path"),   TEXT("string"),  true,  TEXT("Required .utrace file path to analyze"), nullptr, nullptr },
    { TEXT("depth"),  TEXT("integer"), false, TEXT("Tree depth levels for GPU and CPU. Default: 1"), nullptr, TEXT("2") },
    { TEXT("min_ms"), TEXT("number"),  false, TEXT("Min avg ms filter threshold. Default: 0.1"), nullptr, TEXT("0.5") },
    { TEXT("filter"), TEXT("string"),  false, TEXT("Case-insensitive substring filter on node names. Overrides depth limit"), nullptr, TEXT("Shadow") },
};

static const FMCPActionHelp sTraceActions[] = {
    { TEXT("start"),   TEXT("Start a new Unreal Insights trace to file"), sTraceStartParams, UE_ARRAY_COUNT(sTraceStartParams), nullptr },
    { TEXT("stop"),    TEXT("Stop the active trace and flush to disk"), nullptr, 0, nullptr },
    { TEXT("status"),  TEXT("Check if a trace is currently active"), nullptr, 0, nullptr },
    { TEXT("analyze"), TEXT("Analyze GPU and CPU profiling data from a .utrace file"), sTraceAnalyzeParams, UE_ARRAY_COUNT(sTraceAnalyzeParams), nullptr },
    { TEXT("test"),    TEXT("Start trace, wait 5s, stop, and return combined result"), nullptr, 0, nullptr },
};

static const FMCPToolHelpData sTraceHelp = {
    TEXT("trace"),
    TEXT("Control Unreal Insights tracing and analyze GPU/CPU data from .utrace files"),
    TEXT("action"),
    sTraceActions, UE_ARRAY_COUNT(sTraceActions),
    nullptr, 0
};

} // namespace

FMCPToolInfo FMCPTool_Trace::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("trace");
    Info.Description = TEXT("Control Unreal Insights tracing and analyze GPU/CPU data from .utrace files");
    Info.Parameters  = {
        { TEXT("action"),   TEXT("Values: start|stop|status|analyze|test"),                                TEXT("string"),  true  },
        { TEXT("path"),     TEXT("[analyze] Required .utrace file path. [start] Optional output path"),   TEXT("string"),  false },
        { TEXT("depth"),    TEXT("[analyze] Tree depth levels for GPU and CPU. Default: 1"),              TEXT("integer"), false },
        { TEXT("min_ms"),   TEXT("[analyze] Min avg ms filter threshold. Default: 0.1"),                  TEXT("number"),  false },
        { TEXT("filter"),   TEXT("[analyze] Case-insensitive substring filter on node names. Overrides depth limit"), TEXT("string"), false },
        { TEXT("help"),     TEXT("Pass help=true for overview, help='action_name' for detailed parameter info"), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Trace::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sTraceHelp, HelpResult))
        return HelpResult;

    FString Action;
    if (!Params->TryGetStringField(TEXT("action"), Action))
        return FMCPToolResult::Error(TEXT("'action' is required"));

    // analyze: pure file I/O, runs on the caller thread (not game thread)
    if (Action.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
    {
        FString Path;
        if (!Params->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
            return FMCPToolResult::Error(TEXT("'path' is required for analyze"));

        int32 DepthLimit = 1;
        double MinMsThreshold = 0.1;

        double DepthD;
        if (Params->TryGetNumberField(TEXT("depth"), DepthD))
            DepthLimit = FMath::Max(0, FMath::FloorToInt(DepthD));
        else
        {
            FString DepthStr;
            if (Params->TryGetStringField(TEXT("depth"), DepthStr))
                DepthLimit = FMath::Max(0, FCString::Atoi(*DepthStr));
        }

        double MinMsD;
        if (Params->TryGetNumberField(TEXT("min_ms"), MinMsD))
            MinMsThreshold = FMath::Max(0.0, MinMsD);
        else
        {
            FString MinMsStr;
            if (Params->TryGetStringField(TEXT("min_ms"), MinMsStr))
                MinMsThreshold = FMath::Max(0.0, FCString::Atof(*MinMsStr));
        }

        FString Filter;
        Params->TryGetStringField(TEXT("filter"), Filter);

        FTraceAnalysisResult R = FTraceAnalyzer::Analyze(Path, DepthLimit, MinMsThreshold, Filter);
        if (!R.Error.IsEmpty())
            return FMCPToolResult::Error(R.Error);

        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("action"),            TEXT("analyze"));
        Json->SetStringField(TEXT("path"),              R.FilePath);
        Json->SetNumberField(TEXT("frame_count"),       R.FrameStats.FrameCount);
        Json->SetNumberField(TEXT("render_frame_count"), R.RenderPassCount);
        Json->SetField(TEXT("avg_frame_time_ms"), FMCPJsonHelpers::RoundedJsonNumber(R.FrameStats.AvgFrameTimeMs));
        Json->SetField(TEXT("min_frame_time_ms"), FMCPJsonHelpers::RoundedJsonNumber(R.FrameStats.MinFrameTimeMs));
        Json->SetField(TEXT("max_frame_time_ms"), FMCPJsonHelpers::RoundedJsonNumber(R.FrameStats.MaxFrameTimeMs));

        TArray<TSharedPtr<FJsonValue>> GpuArray;
        for (const auto& Node : R.GpuRoot.Children)
            GpuArray.Add(MakeShared<FJsonValueObject>(TimingNodeToJson(Node)));
        Json->SetArrayField(TEXT("gpu"), GpuArray);

        TArray<TSharedPtr<FJsonValue>> CpuArray;
        for (const auto& Node : R.CpuRoot.Children)
            CpuArray.Add(MakeShared<FJsonValueObject>(TimingNodeToJson(Node)));
        Json->SetArrayField(TEXT("cpu"), CpuArray);
        Json->SetNumberField(TEXT("cpu_frame_count"), R.CpuFrameCount);

        return FMCPJsonHelpers::SuccessResponse(Json);
    }

    // stop: validate + stop on game thread, then poll on caller thread
    if (Action.Equals(TEXT("stop"), ESearchCase::IgnoreCase))
    {
        FString FilePath;
        FMCPToolResult ValidationResult = ExecuteOnGameThread([&]() -> FMCPToolResult
        {
            if (!FTraceAuxiliary::IsConnected() ||
                FTraceAuxiliary::GetConnectionType() != FTraceAuxiliary::EConnectionType::File)
                return FMCPToolResult::Error(TEXT("No active trace"));

            FilePath = FTraceAuxiliary::GetTraceDestinationString();
            FTraceAuxiliary::Stop();
            return FMCPToolResult{}; // success sentinel
        });

        if (ValidationResult.bIsError)
            return ValidationResult;

        // Poll on MCP handler thread — game thread is free
        const double Timeout = FPlatformTime::Seconds() + 5.0;
        while (FTraceAuxiliary::IsConnected() && FPlatformTime::Seconds() < Timeout)
            FPlatformProcess::Sleep(0.005f);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("action"), TEXT("stop"));
        Result->SetStringField(TEXT("path"), FilePath);
        if (FTraceAuxiliary::IsConnected())
            Result->SetStringField(TEXT("warning"), TEXT("Trace writer did not flush within timeout"));
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // test: start trace, sleep 5s, stop trace, return combined result
    if (Action.Equals(TEXT("test"), ESearchCase::IgnoreCase))
    {
        FPlatformProcess::Sleep(5.0f);

        FString StartPath;
        FMCPToolResult StartResult = ExecuteOnGameThread([&, Params]() -> FMCPToolResult
        {
            if (FTraceAuxiliary::IsConnected())
            {
                if (FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File)
                    return FMCPToolResult::Error(TEXT("Trace already active"));
                FTraceAuxiliary::Stop();
            }

            FString ExplicitPath;
            const TCHAR* Target = nullptr;
            FTraceAuxiliary::FOptions Options;
            Options.bExcludeTail = true;
            if (Params->TryGetStringField(TEXT("path"), ExplicitPath) && !ExplicitPath.IsEmpty())
            {
                Target = *ExplicitPath;
                Options.bTruncateFile = true;
            }

            bool bStarted = FTraceAuxiliary::Start(
                FTraceAuxiliary::EConnectionType::File, Target, TEXT("cpu,gpu,frame,bookmark"), &Options);
            if (!bStarted)
                return FMCPToolResult::Error(TEXT("FTraceAuxiliary::Start failed"));

            StartPath = FTraceAuxiliary::GetTraceDestinationString();
            return FMCPToolResult{};
        });

        if (StartResult.bIsError)
            return StartResult;

        FPlatformProcess::Sleep(5.0f);

        FString StopPath;
        FMCPToolResult StopResult = ExecuteOnGameThread([&]() -> FMCPToolResult
        {
            if (!FTraceAuxiliary::IsConnected() ||
                FTraceAuxiliary::GetConnectionType() != FTraceAuxiliary::EConnectionType::File)
                return FMCPToolResult::Error(TEXT("No active trace"));

            StopPath = FTraceAuxiliary::GetTraceDestinationString();
            FTraceAuxiliary::Stop();
            return FMCPToolResult{};
        });

        if (StopResult.bIsError)
            return StopResult;

        const double Timeout = FPlatformTime::Seconds() + 5.0;
        while (FTraceAuxiliary::IsConnected() && FPlatformTime::Seconds() < Timeout)
            FPlatformProcess::Sleep(0.005f);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("action"), TEXT("test"));
        Result->SetStringField(TEXT("start_path"), StartPath);
        Result->SetStringField(TEXT("stop_path"), StopPath);
        if (FTraceAuxiliary::IsConnected())
            Result->SetStringField(TEXT("warning"), TEXT("Trace writer did not flush within timeout"));
        return FMCPJsonHelpers::SuccessResponse(Result);
    }

    // Engine-state actions must run on the game thread
    return ExecuteOnGameThread([this, Params, Action]() -> FMCPToolResult
    {
        // ── action=start ─────────────────────────────────────────────────────
        if (Action.Equals(TEXT("start"), ESearchCase::IgnoreCase))
        {
            if (FTraceAuxiliary::IsConnected())
            {
                // A file trace started by this tool is already active.
                if (FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File)
                    return FMCPToolResult::Error(TEXT("Trace already active"));

                // Otherwise it's an auto-connected network trace; stop it so we can start our file trace.
                FTraceAuxiliary::Stop();
            }

            FString ExplicitPath;
            const TCHAR* Target = nullptr;
            FTraceAuxiliary::FOptions Options;
            Options.bExcludeTail = true;
            if (Params->TryGetStringField(TEXT("path"), ExplicitPath) && !ExplicitPath.IsEmpty())
            {
                Target = *ExplicitPath;
                Options.bTruncateFile = true;
            }

            bool bStarted = FTraceAuxiliary::Start(
                FTraceAuxiliary::EConnectionType::File,
                Target,
                TEXT("cpu,gpu,frame,bookmark"),
                &Options);

            if (!bStarted)
                return FMCPToolResult::Error(TEXT("FTraceAuxiliary::Start failed"));

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),    TEXT("start"));
            Result->SetStringField(TEXT("path"), FTraceAuxiliary::GetTraceDestinationString());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── action=status ────────────────────────────────────────────────────
        if (Action.Equals(TEXT("status"), ESearchCase::IgnoreCase))
        {
            const bool bFileTrace = FTraceAuxiliary::IsConnected() &&
                FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File;
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),    TEXT("status"));
            Result->SetBoolField(TEXT("connected"),   bFileTrace);
            Result->SetStringField(TEXT("path"), bFileTrace ? FTraceAuxiliary::GetTraceDestinationString() : TEXT(""));
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown action: '%s'. Valid: start, stop, status, analyze, test"), *Action));
    });
}
