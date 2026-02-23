#pragma once

#include "CoreMinimal.h"
#include "MCPSession.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"
#include "HAL/ThreadSafeCounter.h"

class IHttpRouter;
struct FHttpServerRequest;

class LERVIKMCP_API FMCPServer
{
public:
    ~FMCPServer();
    bool Start(uint32 Port, FString& OutError);
    void Stop();
    bool IsRunning() const;
    uint32 GetPort() const;
    FMCPSessionManager& GetSessionManager();
    TOptional<FMCPSession> GetSessionSnapshot() const;

private:
    bool HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
    bool HandleMethodNotAllowed(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

    FMCPSessionManager SessionManager;
    TSharedPtr<IHttpRouter> HttpRouter;
    FHttpRouteHandle RouteHandle;
    FHttpRouteHandle SseRouteHandle;
    FHttpRouteHandle GetMcpRouteHandle;
    FDelegateHandle PreprocessorHandle;
    uint32 ServerPort = 0;
    bool bIsRunning = false;
    TAtomic<bool> bShuttingDown{false};
    FThreadSafeCounter InFlightTaskCount;
    mutable FCriticalSection SessionLock;
};
