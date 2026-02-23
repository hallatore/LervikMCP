#include "MCPServer.h"
#include "MCPTypes.h"
#include "IMCPTool.h"
#include "Features/IModularFeatures.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpPath.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpServerConstants.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

FMCPServer::~FMCPServer()
{
    Stop();
}

bool FMCPServer::Start(uint32 Port, FString& OutError)
{
    if (!FHttpServerModule::IsAvailable())
    {
        OutError = TEXT("FHttpServerModule is not available");
        return false;
    }

    HttpRouter = FHttpServerModule::Get().GetHttpRouter(Port, true);
    if (!HttpRouter.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to get HTTP router for port %u"), Port);
        return false;
    }

    RouteHandle = HttpRouter->BindRoute(
        FHttpPath(TEXT("/mcp")),
        EHttpServerRequestVerbs::VERB_POST,
        FHttpRequestHandler::CreateRaw(this, &FMCPServer::HandleMcpRequest)
    );

    if (!RouteHandle.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to bind route /mcp on port %u"), Port);
        HttpRouter.Reset();
        return false;
    }

    SseRouteHandle = HttpRouter->BindRoute(
        FHttpPath(TEXT("/sse")),
        EHttpServerRequestVerbs::VERB_GET,
        FHttpRequestHandler::CreateRaw(this, &FMCPServer::HandleMethodNotAllowed)
    );

    if (!SseRouteHandle.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to bind route /sse on port %u"), Port);
        HttpRouter->UnbindRoute(RouteHandle);
        RouteHandle.Reset();
        HttpRouter.Reset();
        return false;
    }

    GetMcpRouteHandle = HttpRouter->BindRoute(
        FHttpPath(TEXT("/mcp")),
        EHttpServerRequestVerbs::VERB_GET,
        FHttpRequestHandler::CreateRaw(this, &FMCPServer::HandleMethodNotAllowed)
    );

    if (!GetMcpRouteHandle.IsValid())
    {
        OutError = FString::Printf(TEXT("Failed to bind GET route /mcp on port %u"), Port);
        HttpRouter->UnbindRoute(SseRouteHandle);
        SseRouteHandle.Reset();
        HttpRouter->UnbindRoute(RouteHandle);
        RouteHandle.Reset();
        HttpRouter.Reset();
        return false;
    }

    PreprocessorHandle = HttpRouter->RegisterRequestPreprocessor(
        FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
        {
            if (Request.RelativePath.GetPath() == TEXT("/") && Request.Verb == EHttpServerRequestVerbs::VERB_GET)
            {
                return HandleMethodNotAllowed(Request, OnComplete);
            }
            return false;
        })
    );

    InFlightTaskCount.Reset();
    bShuttingDown.Store(false);

    FHttpServerModule::Get().StartAllListeners();
    ServerPort = Port;
    bIsRunning = true;

    // Auto-create a static session so clients don't need to call initialize
    SessionManager.CreateSession(TEXT(""), TEXT(""), TEXT("2024-11-05"));

    return true;
}

void FMCPServer::Stop()
{
    bShuttingDown.Store(true);

    // Drain in-flight async tool executions.
    const double DrainTimeoutSec = 5.0;
    const double DrainStart = FPlatformTime::Seconds();
    while (InFlightTaskCount.GetValue() > 0)
    {
        if (FPlatformTime::Seconds() - DrainStart > DrainTimeoutSec)
        {
            UE_LOG(LogTemp, Warning, TEXT("MCPServer::Stop() — drain timeout, %d tasks still in flight"), InFlightTaskCount.GetValue());
            break;
        }
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(0.01f);
    }

    if (HttpRouter.IsValid())
    {
        if (RouteHandle.IsValid())
        {
            HttpRouter->UnbindRoute(RouteHandle);
            RouteHandle.Reset();
        }
        if (SseRouteHandle.IsValid())
        {
            HttpRouter->UnbindRoute(SseRouteHandle);
            SseRouteHandle.Reset();
        }
        if (GetMcpRouteHandle.IsValid())
        {
            HttpRouter->UnbindRoute(GetMcpRouteHandle);
            GetMcpRouteHandle.Reset();
        }
        if (PreprocessorHandle.IsValid())
        {
            HttpRouter->UnregisterRequestPreprocessor(PreprocessorHandle);
            PreprocessorHandle.Reset();
        }
    }

    HttpRouter.Reset();
    ServerPort = 0;
    bIsRunning = false;
    bShuttingDown.Store(false);
}

bool FMCPServer::IsRunning() const
{
    return bIsRunning;
}

uint32 FMCPServer::GetPort() const
{
    return ServerPort;
}

FMCPSessionManager& FMCPServer::GetSessionManager()
{
    return SessionManager;
}

TOptional<FMCPSession> FMCPServer::GetSessionSnapshot() const
{
    FScopeLock Lock(&SessionLock);
    if (const FMCPSession* S = SessionManager.GetSession())
    {
        return *S;
    }
    return {};
}

bool FMCPServer::HandleMethodNotAllowed(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    FString JsonBody = TEXT("{\"error\":\"SSE transport is not supported. Use Streamable HTTP transport with POST /mcp instead.\"}");
    auto Response = FHttpServerResponse::Create(JsonBody, TEXT("application/json"));
    Response->Code = EHttpServerResponseCodes::BadMethod;
    Response->Headers.Add(TEXT("Allow"), { TEXT("POST") });
    OnComplete(MoveTemp(Response));
    return true;
}

bool FMCPServer::HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    // Decode body from UTF-8
    FString BodyString;
    if (Request.Body.Num() > 0)
    {
        TArray<uint8> NullTerminated = Request.Body;
        NullTerminated.Add(0);
        BodyString = FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(NullTerminated.GetData())));
    }

    // Parse JSON-RPC request
    FMCPRequest McpRequest;
    FString ParseError;
    if (!FMCPRequest::Parse(BodyString, McpRequest, ParseError))
    {
        FString ErrorBody = FMCPResponse::Error(nullptr, MCPErrorCodes::ParseError, ParseError);
        auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Ok;
        OnComplete(MoveTemp(Response));
        return true;
    }

    // JSON-RPC 2.0: server MUST NOT reply to notifications
    // MCP Streamable HTTP spec requires 202 Accepted for notifications
    if (McpRequest.bIsNotification)
    {
        auto Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
        Response->Code = EHttpServerResponseCodes::Accepted;
        OnComplete(MoveTemp(Response));
        return true;
    }

    // Session guard: all methods except "initialize" require an active session
    if (McpRequest.Method != TEXT("initialize"))
    {
        FScopeLock Lock(&SessionLock);
        if (!SessionManager.HasSession())
        {
            FString ErrorBody = FMCPResponse::Error(McpRequest.Id, MCPErrorCodes::InvalidRequest, TEXT("No active session"));
            auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
            Response->Code = EHttpServerResponseCodes::Ok;
            OnComplete(MoveTemp(Response));
            return true;
        }
    }

    FString ActiveSessionId;
    {
        FScopeLock Lock(&SessionLock);
        if (const FMCPSession* S = SessionManager.GetSession())
            ActiveSessionId = S->SessionId.ToString(EGuidFormats::DigitsWithHyphens);
    }

    auto CompleteWithSession = [&ActiveSessionId, &OnComplete](TUniquePtr<FHttpServerResponse> Resp)
    {
        if (!ActiveSessionId.IsEmpty())
        {
            Resp->Headers.Add(TEXT("Mcp-Session-Id"), { ActiveSessionId });
        }
        OnComplete(MoveTemp(Resp));
    };

    // Dispatch
    if (McpRequest.Method == TEXT("initialize"))
    {
        FString ClientName, ClientVersion, ProtocolVersion;
        if (McpRequest.Params.IsValid())
        {
            McpRequest.Params->TryGetStringField(TEXT("protocolVersion"), ProtocolVersion);
            const TSharedPtr<FJsonObject>* ClientInfoObj = nullptr;
            if (McpRequest.Params->TryGetObjectField(TEXT("clientInfo"), ClientInfoObj) && ClientInfoObj && ClientInfoObj->IsValid())
            {
                (*ClientInfoObj)->TryGetStringField(TEXT("name"), ClientName);
                (*ClientInfoObj)->TryGetStringField(TEXT("version"), ClientVersion);
            }
        }
        if (ProtocolVersion.IsEmpty())
        {
            ProtocolVersion = TEXT("2024-11-05");
        }

        FMCPSession Session;
        {
            FScopeLock Lock(&SessionLock);
            Session = SessionManager.CreateSession(ClientName, ClientVersion, ProtocolVersion);
        }

        TSharedPtr<FJsonObject> Tools = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
        Capabilities->SetObjectField(TEXT("tools"), Tools);

        TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
        ServerInfo->SetStringField(TEXT("name"), TEXT("LervikMCP"));
        ServerInfo->SetStringField(TEXT("version"), TEXT("1.0"));

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));
        Result->SetObjectField(TEXT("capabilities"), Capabilities);
        Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

        FString ResponseBody = FMCPResponse::Success(McpRequest.Id, Result);
        auto Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Ok;
        Response->Headers.Add(TEXT("Mcp-Session-Id"), { Session.SessionId.ToString(EGuidFormats::DigitsWithHyphens) });
        OnComplete(MoveTemp(Response));
        return true;
    }
    else if (McpRequest.Method == TEXT("tools/list"))
    {
        TArray<FMCPToolInfo> ToolInfos;
        {
            IModularFeatures::FScopedLockModularFeatureList ScopedLock;
            TArray<IMCPTool*> RegisteredTools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(
                IMCPTool::GetModularFeatureName());
            for (IMCPTool* Tool : RegisteredTools)
            {
                ToolInfos.Add(Tool->GetToolInfo());
            }
        }

        TArray<TSharedPtr<FJsonValue>> ToolsArray;
        for (const FMCPToolInfo& Info : ToolInfos)
        {
            TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> RequiredArray;
            for (const FMCPToolParameter& Param : Info.Parameters)
            {
                auto MakeItemsSchema = [&Param]() {
                    TSharedPtr<FJsonObject> Items = MakeShared<FJsonObject>();
                    if (!Param.ItemsType.IsEmpty())
                    {
                        Items->SetStringField(TEXT("type"), Param.ItemsType);
                    }
                    return Items;
                };

                TSharedPtr<FJsonObject> ParamSchema = MakeShared<FJsonObject>();
                if (Param.Type.Contains(TEXT("|")))
                {
                    TArray<FString> Parts;
                    Param.Type.ParseIntoArray(Parts, TEXT("|"));
                    TArray<TSharedPtr<FJsonValue>> OneOf;
                    for (const FString& Part : Parts)
                    {
                        TSharedPtr<FJsonObject> Sub = MakeShared<FJsonObject>();
                        Sub->SetStringField(TEXT("type"), Part);
                        if (Part == TEXT("array"))
                        {
                            Sub->SetObjectField(TEXT("items"), MakeItemsSchema());
                        }
                        OneOf.Add(MakeShared<FJsonValueObject>(Sub));
                    }
                    ParamSchema->SetArrayField(TEXT("oneOf"), OneOf);
                }
                else if (Param.Type == TEXT("array"))
                {
                    ParamSchema->SetStringField(TEXT("type"), TEXT("array"));
                    ParamSchema->SetObjectField(TEXT("items"), MakeItemsSchema());
                }
                else
                {
                    ParamSchema->SetStringField(TEXT("type"), Param.Type);
                }
                if (!Param.Description.IsEmpty())
                {
                    ParamSchema->SetStringField(TEXT("description"), Param.Description);
                }
                Properties->SetObjectField(Param.Name.ToString(), ParamSchema);
                if (Param.bRequired)
                {
                    RequiredArray.Add(MakeShared<FJsonValueString>(Param.Name.ToString()));
                }
            }

            TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
            Schema->SetStringField(TEXT("type"), TEXT("object"));
            Schema->SetObjectField(TEXT("properties"), Properties);
            if (RequiredArray.Num() > 0)
            {
                Schema->SetArrayField(TEXT("required"), RequiredArray);
            }

            TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
            ToolObj->SetStringField(TEXT("name"), Info.Name.ToString());
            ToolObj->SetStringField(TEXT("description"), Info.Description);
            ToolObj->SetObjectField(TEXT("inputSchema"), Schema);
            ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("tools"), ToolsArray);

        FString ResponseBody = FMCPResponse::Success(McpRequest.Id, Result);
        auto Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Ok;
        CompleteWithSession(MoveTemp(Response));
        return true;
    }
    else if (McpRequest.Method == TEXT("tools/call"))
    {
        FString ToolName;
        if (!McpRequest.Params.IsValid() || !McpRequest.Params->TryGetStringField(TEXT("name"), ToolName))
        {
            FString ErrorBody = FMCPResponse::Error(McpRequest.Id, MCPErrorCodes::InvalidParams, TEXT("Missing tool name"));
            auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
            Response->Code = EHttpServerResponseCodes::Ok;
            CompleteWithSession(MoveTemp(Response));
            return true;
        }

        IMCPTool* FoundTool = nullptr;
        {
            IModularFeatures::FScopedLockModularFeatureList ScopedLock;
            TArray<IMCPTool*> RegisteredTools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(
                IMCPTool::GetModularFeatureName());
            for (IMCPTool* Tool : RegisteredTools)
            {
                if (Tool->GetToolInfo().Name.ToString() == ToolName)
                {
                    FoundTool = Tool;
                    break;
                }
            }
        }

        if (!FoundTool)
        {
            FString ErrorBody = FMCPResponse::Error(McpRequest.Id, MCPErrorCodes::MethodNotFound,
                FString::Printf(TEXT("Tool not found: %s"), *ToolName));
            auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
            Response->Code = EHttpServerResponseCodes::Ok;
            CompleteWithSession(MoveTemp(Response));
            return true;
        }

        TSharedPtr<FJsonObject> Arguments;
        if (McpRequest.Params.IsValid() && McpRequest.Params->HasField(TEXT("arguments")))
        {
            Arguments = McpRequest.Params->GetObjectField(TEXT("arguments"));
        }

        // Increment first to prevent TOCTOU race with Stop() drain loop
        InFlightTaskCount.Increment();
        if (bShuttingDown.Load())
        {
            InFlightTaskCount.Decrement();
            FString ErrorBody = FMCPResponse::Error(McpRequest.Id, MCPErrorCodes::InternalError, TEXT("Server is shutting down"));
            auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
            Response->Code = EHttpServerResponseCodes::Ok;
            CompleteWithSession(MoveTemp(Response));
            return true;
        }

        // Dispatch tool execution to background thread so long-running tools
        // (e.g. trace test with Sleep) don't block the game thread.
        TSharedPtr<FJsonValue> RequestIdCopy = McpRequest.Id;
        FString SessionIdCopy = ActiveSessionId;

        Async(EAsyncExecution::Thread, [this, FoundTool, Arguments, RequestIdCopy, SessionIdCopy, OnComplete]()
        {
            FMCPToolResult ToolResult = FoundTool->Execute(Arguments);

            TSharedPtr<FJsonObject> ContentItem = MakeShared<FJsonObject>();
            ContentItem->SetStringField(TEXT("type"), TEXT("text"));
            ContentItem->SetStringField(TEXT("text"), ToolResult.Content);

            TArray<TSharedPtr<FJsonValue>> ContentArray;
            ContentArray.Add(MakeShared<FJsonValueObject>(ContentItem));

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("content"), ContentArray);
            Result->SetBoolField(TEXT("isError"), ToolResult.bIsError);

            FString ResponseBody = FMCPResponse::Success(RequestIdCopy, Result);

            AsyncTask(ENamedThreads::GameThread, [this, ResponseBody, SessionIdCopy, OnComplete]()
            {
                auto Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
                Response->Code = EHttpServerResponseCodes::Ok;
                if (!SessionIdCopy.IsEmpty())
                {
                    Response->Headers.Add(TEXT("Mcp-Session-Id"), { SessionIdCopy });
                }
                OnComplete(MoveTemp(Response));
                InFlightTaskCount.Decrement();
            });
        });
        return true;
    }
    else if (McpRequest.Method == TEXT("ping"))
    {
        TSharedPtr<FJsonObject> PingResult = MakeShared<FJsonObject>();
        FString ResponseBody = FMCPResponse::Success(McpRequest.Id, PingResult);
        auto Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Ok;
        CompleteWithSession(MoveTemp(Response));
        return true;
    }
    else
    {
        FString ErrorBody = FMCPResponse::Error(McpRequest.Id, MCPErrorCodes::MethodNotFound,
            FString::Printf(TEXT("Method not found: %s"), *McpRequest.Method));
        auto Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Ok;
        CompleteWithSession(MoveTemp(Response));
        return true;
    }
}
