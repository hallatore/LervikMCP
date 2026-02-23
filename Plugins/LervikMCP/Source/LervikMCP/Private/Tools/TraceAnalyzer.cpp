#include "Tools/TraceAnalyzer.h"

#include "HAL/FileManager.h"
#include "Math/UnrealMathUtility.h"

#if LERVIKMCP_WITH_TRACE_ANALYSIS

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "Misc/EngineVersionComparison.h"

namespace {

void PruneTree(FTraceGpuNode& Node, int32 CurrentDepth, int32 MaxDepth, double MinMsThreshold)
{
    if (CurrentDepth >= MaxDepth)
    {
        Node.Children.Empty();
        return;
    }
    for (auto& Child : Node.Children)
        PruneTree(Child, CurrentDepth + 1, MaxDepth, MinMsThreshold);
    Node.Children.RemoveAll([MinMsThreshold](const FTraceGpuNode& Child) {
        return Child.GetAvgMs() < MinMsThreshold;
    });
}

bool ShouldKeep(const FTraceGpuNode& Node, const FString& Filter)
{
    if (Node.Name.Contains(Filter, ESearchCase::IgnoreCase))
        return true;
    for (const FTraceGpuNode& Child : Node.Children)
        if (ShouldKeep(Child, Filter))
            return true;
    return false;
}

void FilterTree(FTraceGpuNode& Node, const FString& Filter)
{
    Node.Children.RemoveAll([&Filter](const FTraceGpuNode& Child)
    {
        return !ShouldKeep(Child, Filter);
    });
    for (FTraceGpuNode& Child : Node.Children)
        FilterTree(Child, Filter);
}

void PruneByMinMs(FTraceGpuNode& Node, double MinMsThreshold)
{
    for (FTraceGpuNode& Child : Node.Children)
        PruneByMinMs(Child, MinMsThreshold);
    Node.Children.RemoveAll([MinMsThreshold](const FTraceGpuNode& Child)
    {
        return Child.GetAvgMs() < MinMsThreshold;
    });
}

// Finds the node whose children include "PostProcessing".
// That node is the semantically meaningful GPU root.
FTraceGpuNode* FindStartingPoint(FTraceGpuNode& Node)
{
    for (auto& Child : Node.Children)
    {
        if (Child.Name.Equals(TEXT("PostProcessing"), ESearchCase::IgnoreCase))
            return &Node;
    }
    for (auto& Child : Node.Children)
    {
        if (FTraceGpuNode* Found = FindStartingPoint(Child))
            return Found;
    }
    return nullptr;
}

} // namespace

FTraceAnalysisResult FTraceAnalyzer::Analyze(const FString& Path, int32 DepthLimit, double MinMs, const FString& Filter)
{
    FTraceAnalysisResult Result;
    Result.FilePath = Path;

    if (!IFileManager::Get().FileExists(*Path))
    {
        Result.Error = FString::Printf(TEXT("Trace file not found: %s"), *Path);
        return Result;
    }

    ITraceServicesModule& TraceServicesModule =
        FModuleManager::LoadModuleChecked<ITraceServicesModule>(TEXT("TraceServices"));

    TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TraceServicesModule.GetAnalysisService();
    if (!AnalysisService.IsValid())
    {
        Result.Error = TEXT("Failed to get TraceServices analysis service");
        return Result;
    }

    // Use StartAnalysis + Wait separately to handle null session gracefully
    TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->StartAnalysis(*Path);
    if (!Session.IsValid())
    {
        Result.Error = FString::Printf(TEXT("Failed to open trace file for analysis: %s"), *Path);
        return Result;
    }

    Session->Wait();

    TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
    const TraceServices::IFrameProvider& FrameProvider = TraceServices::ReadFrameProvider(*Session);

    uint64 FrameCount = FrameProvider.GetFrameCount(TraceFrameType_Game);

    double TotalMs     = 0.0;
    double MinFrameMs  = TNumericLimits<double>::Max();
    double MaxFrameMs  = 0.0;
    int32  ValidCount  = 0;

    double GpuStartTime = TNumericLimits<double>::Max();
    double GpuEndTime   = 0.0;
    if (FrameCount > 0)
    {
        FrameProvider.EnumerateFrames(TraceFrameType_Game, (uint64)0, FrameCount,
            [&](const TraceServices::FFrame& Frame)
            {
                double DurationMs = (Frame.EndTime - Frame.StartTime) * 1000.0;
                if (!FMath::IsFinite(DurationMs) || DurationMs < 0.0) return;

                // Stats
                TotalMs += DurationMs;
                if (DurationMs < MinFrameMs) MinFrameMs = DurationMs;
                if (DurationMs > MaxFrameMs) MaxFrameMs = DurationMs;
                ++ValidCount;

                // Track time window for GPU enumeration
                GpuStartTime = FMath::Min(GpuStartTime, Frame.StartTime);
                GpuEndTime   = FMath::Max(GpuEndTime,   Frame.EndTime);
            });

        if (ValidCount > 0)
        {
            Result.FrameStats.FrameCount     = ValidCount;
            Result.FrameStats.AvgFrameTimeMs = TotalMs / (double)ValidCount;
            Result.FrameStats.MinFrameTimeMs = MinFrameMs;
            Result.FrameStats.MaxFrameTimeMs = MaxFrameMs;
        }
    }

    // ── GPU tree ──────────────────────────────────────────────────────────────
    const TraceServices::ITimingProfilerProvider* TimingProvider =
        TraceServices::ReadTimingProfilerProvider(*Session);

    if (!TimingProvider)
        return Result; // No GPU data — valid, not an error

    uint32 GpuTimelineIdx = 0;
#if UE_VERSION_OLDER_THAN(5, 7, 0)
    // Old API (5.4–5.6)
    if (!TimingProvider->GetGpuTimelineIndex(GpuTimelineIdx))
        return Result;
#else
    // New API (5.7+): enumerate GPU queues for per-queue timelines
    bool bFoundGpuTimeline = false;
    if (TimingProvider->HasGpuTiming())
    {
        TimingProvider->EnumerateGpuQueues([&](const TraceServices::FGpuQueueInfo& Queue)
        {
            if (!bFoundGpuTimeline)
            {
                GpuTimelineIdx = Queue.TimelineIndex;
                bFoundGpuTimeline = true;
            }
        });
    }
    // Fallback to old API for old .utrace files opened in a 5.7 editor
    if (!bFoundGpuTimeline)
    {
        if (!TimingProvider->GetGpuTimelineIndex(GpuTimelineIdx))
            return Result;
    }
#endif

    // Build timer name lookup: index → name
    TMap<uint32, FString> TimerNames;
    TimingProvider->ReadTimers([&](const TraceServices::ITimingProfilerTimerReader& Reader)
    {
        uint32 Count = Reader.GetTimerCount();
        for (uint32 i = 0; i < Count; ++i)
        {
            const TraceServices::FTimingProfilerTimer* Timer = Reader.GetTimer(i);
            if (Timer && Timer->Name)
                TimerNames.Add(i, FString(Timer->Name));
        }
    });

    // Enumerate GPU events and build tree — single pass over full timeline
    if (ValidCount > 0)
    {
        TimingProvider->ReadTimeline(GpuTimelineIdx,
            [&](const TraceServices::ITimingProfilerProvider::Timeline& GpuTimeline)
            {
                int32 RenderPassCount = 0;
                TMap<FTraceGpuNode*, TMap<FString, int32>> SeenCounts;
                TArray<FTraceGpuNode*> Stack;
                Stack.Push(&Result.GpuRoot);

                GpuTimeline.EnumerateEvents(GpuStartTime, GpuEndTime,
                    [&](double StartTime, double EndTime, uint32 Depth, const TraceServices::FTimingProfilerEvent& Event)
                        -> TraceServices::EEventEnumerate
                    {
                        // New render pass when we return to depth 0
                        if (Depth == 0)
                        {
                            RenderPassCount++;
                            SeenCounts.Reset();
                            Stack.SetNum(1); // Keep only root
                        }
                        else
                        {
                            // Pop stack back to the correct parent depth
                            while (Stack.Num() > (int32)(Depth + 1) && Stack.Num() > 1)
                                Stack.Pop();
                        }

                        FTraceGpuNode* Parent = Stack.Last();

                        // Resolve timer name (metadata-enhanced IDs in 5.7+ have MSB set)
                        uint32 ResolvedTimerIndex = Event.TimerIndex;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
                        ResolvedTimerIndex = TimingProvider->GetOriginalTimerIdFromMetadata(ResolvedTimerIndex);
#endif

                        FString BaseName;
                        if (const FString* Found = TimerNames.Find(ResolvedTimerIndex))
                            BaseName = *Found;
                        else
                            BaseName = FString::Printf(TEXT("Timer_%u"), ResolvedTimerIndex);

                        // Disambiguate same-name siblings within this render pass
                        TMap<FString, int32>& ParentSeen = SeenCounts.FindOrAdd(Parent);
                        int32& SeenCount = ParentSeen.FindOrAdd(BaseName);
                        FString NodeName = SeenCount == 0
                            ? BaseName
                            : FString::Printf(TEXT("%s_%d"), *BaseName, SeenCount);
                        SeenCount++;

                        // Find or create child node
                        FTraceGpuNode* Node = nullptr;
                        for (auto& Child : Parent->Children)
                        {
                            if (Child.Name == NodeName) { Node = &Child; break; }
                        }
                        if (!Node)
                        {
                            Parent->Children.Emplace();
                            Node = &Parent->Children.Last();
                            Node->Name = NodeName;
                        }

                        // Accumulate stats
                        double DurationMs = (EndTime - StartTime) * 1000.0;
                        if (FMath::IsFinite(DurationMs) && DurationMs >= 0.0)
                        {
                            Node->Count++;
                            Node->TotalMs += DurationMs;
                            Node->MinMs = FMath::Min(Node->MinMs, DurationMs);
                            Node->MaxMs = FMath::Max(Node->MaxMs, DurationMs);
                        }

                        Stack.Push(Node);
                        return TraceServices::EEventEnumerate::Continue;
                    });

                Result.RenderPassCount = RenderPassCount;
            });
    }

    // Narrow to the semantically meaningful root: parent of PostProcessing
    if (FTraceGpuNode* StartNode = FindStartingPoint(Result.GpuRoot))
    {
        FTraceGpuNode NewRoot;
        NewRoot.Children = MoveTemp(StartNode->Children);
        Result.GpuRoot = MoveTemp(NewRoot);
    }

    // Prune by depth and min_ms threshold
    if (!Filter.IsEmpty())
    {
        FilterTree(Result.GpuRoot, Filter);
        PruneByMinMs(Result.GpuRoot, MinMs);
        FilterTree(Result.GpuRoot, Filter);  // Remove orphan ancestors left by PruneByMinMs
    }
    else
    {
        PruneTree(Result.GpuRoot, 0, DepthLimit, MinMs);
    }

    return Result;
}

#else // !LERVIKMCP_WITH_TRACE_ANALYSIS

FTraceAnalysisResult FTraceAnalyzer::Analyze(const FString& Path, int32 DepthLimit, double MinMs, const FString& Filter)
{
    FTraceAnalysisResult Result;
    Result.FilePath = Path;
    Result.Error = TEXT("Trace analysis requires an Editor build (TraceServices not available)");
    return Result;
}

#endif // LERVIKMCP_WITH_TRACE_ANALYSIS
