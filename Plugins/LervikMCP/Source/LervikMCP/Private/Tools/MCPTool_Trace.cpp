#include "Tools/MCPTool_Trace.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "Tools/TraceAnalyzer.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ProfilingDebugging/TraceAuxiliary.h"

namespace {

TSharedPtr<FJsonObject> GpuNodeToJson(const FTraceGpuNode& Node)
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetStringField(TEXT("name"),   Node.Name);
    Obj->SetNumberField(TEXT("count"),  Node.Count);
    Obj->SetField(TEXT("avg_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.GetAvgMs()));
    Obj->SetField(TEXT("min_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.Count > 0 ? Node.MinMs : 0.0));
    Obj->SetField(TEXT("max_ms"), FMCPJsonHelpers::RoundedJsonNumber(Node.MaxMs));

    TArray<TSharedPtr<FJsonValue>> ChildArray;
    for (const auto& Child : Node.Children)
        ChildArray.Add(MakeShared<FJsonValueObject>(GpuNodeToJson(Child)));
    Obj->SetArrayField(TEXT("children"), ChildArray);

    return Obj;
}

} // namespace

FMCPToolInfo FMCPTool_Trace::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("trace");
    Info.Description = TEXT("Control Unreal Insights tracing and analyze GPU profiling data from .utrace files. test runs (start → 5s sleep → stop)");
    Info.Parameters  = {
        { TEXT("action"),   TEXT("'start', 'stop', 'status', 'analyze', or 'test'"),                      TEXT("string"), true  },
        { TEXT("channels"), TEXT("Trace channels comma-separated. Default: cpu,gpu,frame,bookmark"),      TEXT("string"), false },
        { TEXT("path"),     TEXT("Path to .utrace file. Required for 'analyze'; optional output path for 'start'"), TEXT("string"), false },
        { TEXT("depth"),    TEXT("GPU tree depth levels. Default: 1"),                                     TEXT("string"), false },
        { TEXT("min_ms"),   TEXT("Min avg ms threshold. Default: 0.1"),                                    TEXT("string"), false },
        { TEXT("filter"),      TEXT("Case-insensitive substring filter on GPU node names. Overrides depth limit. Returns full ancestor chain for matches."), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Trace::Execute(const TSharedPtr<FJsonObject>& Params)
{
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
        FString DepthStr, MinMsStr;
        if (Params->TryGetStringField(TEXT("depth"), DepthStr))
            DepthLimit = FMath::Max(0, FCString::Atoi(*DepthStr));
        if (Params->TryGetStringField(TEXT("min_ms"), MinMsStr))
            MinMsThreshold = FMath::Max(0.0, FCString::Atof(*MinMsStr));

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
            GpuArray.Add(MakeShared<FJsonValueObject>(GpuNodeToJson(Node)));
        Json->SetArrayField(TEXT("gpu"), GpuArray);

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
        FString Channels;
        FMCPToolResult StartResult = ExecuteOnGameThread([&, Params]() -> FMCPToolResult
        {
            if (FTraceAuxiliary::IsConnected())
            {
                if (FTraceAuxiliary::GetConnectionType() == FTraceAuxiliary::EConnectionType::File)
                    return FMCPToolResult::Error(TEXT("Trace already active"));
                FTraceAuxiliary::Stop();
            }

            if (!Params->TryGetStringField(TEXT("channels"), Channels) || Channels.IsEmpty())
                Channels = TEXT("cpu,gpu,frame,bookmark");

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
                FTraceAuxiliary::EConnectionType::File, Target, *Channels, &Options);
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
        Result->SetStringField(TEXT("channels"), Channels);
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

            FString Channels;
            if (!Params->TryGetStringField(TEXT("channels"), Channels) || Channels.IsEmpty())
                Channels = TEXT("cpu,gpu,frame,bookmark");

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
                *Channels,
                &Options);

            if (!bStarted)
                return FMCPToolResult::Error(TEXT("FTraceAuxiliary::Start failed"));

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("action"),    TEXT("start"));
            Result->SetStringField(TEXT("path"), FTraceAuxiliary::GetTraceDestinationString());
            Result->SetStringField(TEXT("channels"),  Channels);
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
