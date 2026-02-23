#pragma once
#include "CoreMinimal.h"

struct FTraceFrameStats
{
    int32  FrameCount      = 0;
    double AvgFrameTimeMs  = 0.0;
    double MinFrameTimeMs  = 0.0;
    double MaxFrameTimeMs  = 0.0;
};

struct FTraceGpuNode
{
    FString Name;
    int32   Count   = 0;
    double  TotalMs = 0.0;
    double  MinMs   = TNumericLimits<double>::Max();
    double  MaxMs   = 0.0;
    TArray<FTraceGpuNode> Children;

    double GetAvgMs() const { return Count > 0 ? TotalMs / Count : 0.0; }
};

struct FTraceAnalysisResult
{
    FTraceFrameStats FrameStats;
    FTraceGpuNode    GpuRoot;   // virtual root — actual top-level GPU nodes are in Children
    int32            RenderPassCount = 0;
    FString          FilePath;
    FString          Error;
};

class FTraceAnalyzer
{
public:
    static FTraceAnalysisResult Analyze(const FString& Path, int32 DepthLimit = 1, double MinMs = 0.1, const FString& Filter = TEXT(""));
};
