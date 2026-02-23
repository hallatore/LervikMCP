#pragma once

#include "MCPTypes.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

template<typename TFunc>
FMCPToolResult ExecuteOnGameThread(TFunc&& Func)
{
    if (IsInGameThread())
    {
        return Func();
    }

    FMCPToolResult Result;
    FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);

    AsyncTask(ENamedThreads::GameThread, [&Result, &Func, DoneEvent]()
    {
        Result = Func();
        DoneEvent->Trigger();
    });

    DoneEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
    return Result;
}
