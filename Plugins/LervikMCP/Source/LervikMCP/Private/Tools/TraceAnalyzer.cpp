#include "Tools/TraceAnalyzer.h"

#include "HAL/FileManager.h"
#include "Math/UnrealMathUtility.h"

#if LERVIKMCP_WITH_TRACE_ANALYSIS

#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/Threads.h"
#include "TraceServices/Model/TimingProfiler.h"
#include "Misc/EngineVersionComparison.h"

namespace {

void PruneTree(FTraceTimingNode& Node, int32 CurrentDepth, int32 MaxDepth, double MinMsThreshold)
{
    if (CurrentDepth >= MaxDepth)
    {
        Node.Children.Empty();
        return;
    }
    for (auto& Child : Node.Children)
        PruneTree(Child, CurrentDepth + 1, MaxDepth, MinMsThreshold);
    Node.Children.RemoveAll([MinMsThreshold](const FTraceTimingNode& Child) {
        return Child.GetAvgMs() < MinMsThreshold;
    });
}

bool ShouldKeep(const FTraceTimingNode& Node, const FString& Filter)
{
    if (Node.Name.Contains(Filter, ESearchCase::IgnoreCase))
        return true;
    for (const FTraceTimingNode& Child : Node.Children)
        if (ShouldKeep(Child, Filter))
            return true;
    return false;
}

void FilterTree(FTraceTimingNode& Node, const FString& Filter)
{
    Node.Children.RemoveAll([&Filter](const FTraceTimingNode& Child)
    {
        return !ShouldKeep(Child, Filter);
    });
    for (FTraceTimingNode& Child : Node.Children)
        FilterTree(Child, Filter);
}

void PruneByMinMs(FTraceTimingNode& Node, double MinMsThreshold)
{
    for (FTraceTimingNode& Child : Node.Children)
        PruneByMinMs(Child, MinMsThreshold);
    Node.Children.RemoveAll([MinMsThreshold](const FTraceTimingNode& Child)
    {
        return Child.GetAvgMs() < MinMsThreshold;
    });
}

// Shared tree-building logic for both GPU and CPU timelines.
// Returns the number of top-level scopes (render passes for GPU, frames for CPU).
int32 BuildTimingTree(
    const TraceServices::ITimingProfilerProvider::Timeline& Timeline,
    FTraceTimingNode& Root,
    double StartTime, double EndTime,
    const TMap<uint32, FString>& TimerNames,
    const TraceServices::ITimingProfilerProvider* TimingProvider)
{
    int32 TopLevelCount = 0;
    TMap<FTraceTimingNode*, TMap<FString, int32>> SeenCounts;
    TArray<FTraceTimingNode*> Stack;
    Stack.Push(&Root);

    Timeline.EnumerateEvents(StartTime, EndTime,
        [&](double EvStart, double EvEnd, uint32 Depth, const TraceServices::FTimingProfilerEvent& Event)
            -> TraceServices::EEventEnumerate
        {
            if (Depth == 0)
            {
                TopLevelCount++;
                SeenCounts.Reset();
                Stack.SetNum(1);
            }
            else
            {
                while (Stack.Num() > (int32)(Depth + 1) && Stack.Num() > 1)
                    Stack.Pop();
            }

            FTraceTimingNode* Parent = Stack.Last();

            uint32 ResolvedTimerIndex = Event.TimerIndex;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
            ResolvedTimerIndex = TimingProvider->GetOriginalTimerIdFromMetadata(ResolvedTimerIndex);
#endif

            FString BaseName;
            if (const FString* Found = TimerNames.Find(ResolvedTimerIndex))
                BaseName = *Found;
            else
                BaseName = FString::Printf(TEXT("Timer_%u"), ResolvedTimerIndex);

            TMap<FString, int32>& ParentSeen = SeenCounts.FindOrAdd(Parent);
            int32& SeenCount = ParentSeen.FindOrAdd(BaseName);
            FString NodeName = SeenCount == 0
                ? BaseName
                : FString::Printf(TEXT("%s_%d"), *BaseName, SeenCount);
            SeenCount++;

            FTraceTimingNode* Node = nullptr;
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

            double DurationMs = (EvEnd - EvStart) * 1000.0;
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

    return TopLevelCount;
}

// Finds the node whose children include "PostProcessing".
// That node is the semantically meaningful GPU root.
FTraceTimingNode* FindGpuStartingPoint(FTraceTimingNode& Node)
{
    for (auto& Child : Node.Children)
    {
        if (Child.Name.Equals(TEXT("PostProcessing"), ESearchCase::IgnoreCase))
            return &Node;
    }
    for (auto& Child : Node.Children)
    {
        if (FTraceTimingNode* Found = FindGpuStartingPoint(Child))
            return Found;
    }
    return nullptr;
}


// Find node containing "FEngineLoop::Tick" recursively.
FTraceTimingNode* FindEngineLoopTick(FTraceTimingNode& Node)
{
    if (Node.Name.Contains(TEXT("FEngineLoop::Tick"), ESearchCase::IgnoreCase))
        return &Node;
    for (auto& Child : Node.Children)
        if (FTraceTimingNode* Found = FindEngineLoopTick(Child))
            return Found;
    return nullptr;
}

// Finds the frame-level node in the CPU tree.
// Locates FEngineLoop::Tick, then returns its "Frame" child if present, else the tick node itself.
FTraceTimingNode* FindCpuStartingPoint(FTraceTimingNode& Node)
{
    FTraceTimingNode* TickNode = FindEngineLoopTick(Node);
    if (!TickNode)
        return nullptr;
    for (auto& Child : TickNode->Children)
    {
        if (Child.Name.Equals(TEXT("Frame"), ESearchCase::IgnoreCase))
            return &Child;
    }
    return TickNode;
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

    double TraceStartTime = TNumericLimits<double>::Max();
    double TraceEndTime   = 0.0;
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

                // Track time window for GPU/CPU enumeration
                TraceStartTime = FMath::Min(TraceStartTime, Frame.StartTime);
                TraceEndTime   = FMath::Max(TraceEndTime,   Frame.EndTime);
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
                Result.RenderPassCount = BuildTimingTree(
                    GpuTimeline, Result.GpuRoot, TraceStartTime, TraceEndTime, TimerNames, TimingProvider);
            });
    }

    // Narrow to the semantically meaningful root: parent of PostProcessing
    if (FTraceTimingNode* StartNode = FindGpuStartingPoint(Result.GpuRoot))
    {
        FTraceTimingNode NewRoot;
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

    // ── CPU tree (game thread) ─────────────────────────────────────────────────
    const TraceServices::IThreadProvider& ThreadProvider = TraceServices::ReadThreadProvider(*Session);

    uint32 GameThreadId = 0;
    bool bFoundGameThread = false;
    ThreadProvider.EnumerateThreads([&](const TraceServices::FThreadInfo& Thread)
    {
        if (!bFoundGameThread && FCString::Stristr(Thread.Name, TEXT("GameThread")))
        {
            GameThreadId = Thread.Id;
            bFoundGameThread = true;
        }
    });

    if (bFoundGameThread && TimingProvider && ValidCount > 0)
    {
        uint32 CpuTimelineIdx = 0;
        if (TimingProvider->GetCpuThreadTimelineIndex(GameThreadId, CpuTimelineIdx))
        {
            TimingProvider->ReadTimeline(CpuTimelineIdx,
                [&](const TraceServices::ITimingProfilerProvider::Timeline& CpuTimeline)
                {
                    Result.CpuFrameCount = BuildTimingTree(
                        CpuTimeline, Result.CpuRoot, TraceStartTime, TraceEndTime, TimerNames, TimingProvider);
                });
        }
    }

    // Narrow CPU tree to FEngineLoop::Tick children
    if (FTraceTimingNode* CpuStart = FindCpuStartingPoint(Result.CpuRoot))
    {
        FTraceTimingNode NewCpuRoot;
        NewCpuRoot.Children = MoveTemp(CpuStart->Children);
        Result.CpuRoot = MoveTemp(NewCpuRoot);
    }

    // Prune CPU tree
    if (!Filter.IsEmpty())
    {
        FilterTree(Result.CpuRoot, Filter);
        PruneByMinMs(Result.CpuRoot, MinMs);
        FilterTree(Result.CpuRoot, Filter);
    }
    else
    {
        PruneTree(Result.CpuRoot, 0, DepthLimit, MinMs);
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
