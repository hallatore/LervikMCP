#include "LervikMCPModule.h"
#include "MCPServer.h"
#include "HAL/IConsoleManager.h"
#include "Async/Async.h"
#include "Tools/MCPTool_Execute.h"
#include "Tools/MCPTool_Trace.h"
#include "Features/IModularFeatures.h"

DEFINE_LOG_CATEGORY_STATIC(LogLervikMCP, Log, All);

static TAutoConsoleVariable<int32> CVarMcpEnable(
    TEXT("mcp.enable"), 0,
    TEXT("Enable (1) or disable (0) the MCP HTTP server"),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarMcpPort(
    TEXT("mcp.port"), 8090,
    TEXT("Port for the MCP HTTP server"),
    ECVF_Default);

void FLervikMCPModule::StartupModule()
{
    FModuleManager::Get().LoadModuleChecked(TEXT("HTTPServer"));

    FConsoleVariableDelegate OnCVarChanged = FConsoleVariableDelegate::CreateLambda([this](IConsoleVariable*)
    {
        if (IsInGameThread())
        {
            ApplyServerState();
        }
        else
        {
            AsyncTask(ENamedThreads::GameThread, [this]() { ApplyServerState(); });
        }
    });
    CVarMcpEnable.AsVariable()->SetOnChangedCallback(OnCVarChanged);
    CVarMcpPort.AsVariable()->SetOnChangedCallback(OnCVarChanged);

    StatusCommand = IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("MCP.Status"),
        TEXT("Show MCP server status"),
        FConsoleCommandWithArgsDelegate::CreateRaw(this, &FLervikMCPModule::HandleStatusCommand),
        ECVF_Default);

    UE_LOG(LogLervikMCP, Log, TEXT("LervikMCP module loaded. Set mcp.enable=1 to start."));

    RuntimeTools.Add(MakeUnique<FMCPTool_Execute>());
    RuntimeTools.Add(MakeUnique<FMCPTool_Trace>());
    for (const auto& Tool : RuntimeTools)
    {
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), Tool.Get());
    }

    ApplyServerState();
}

void FLervikMCPModule::ShutdownModule()
{
    for (const auto& Tool : RuntimeTools)
    {
        IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), Tool.Get());
    }
    RuntimeTools.Empty();

    CVarMcpEnable.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());
    CVarMcpPort.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate());

    if (Server.IsValid())
    {
        Server->Stop();
        Server.Reset();
    }

    if (StatusCommand)
    {
        IConsoleManager::Get().UnregisterConsoleObject(StatusCommand);
        StatusCommand = nullptr;
    }
}

FMCPServer* FLervikMCPModule::GetServer() const
{
    return Server.Get();
}

FString FLervikMCPModule::GuidToCompact(const FGuid& Guid)
{
    if (Server.IsValid())
    {
        return Server->GetSessionManager().GuidToCompact(Guid);
    }
    return Guid.ToString(EGuidFormats::DigitsLower);
}

FGuid FLervikMCPModule::CompactToGuid(const FString& Compact)
{
    if (Server.IsValid())
    {
        return Server->GetSessionManager().CompactToGuid(Compact);
    }
    FGuid Fallback;
    FGuid::Parse(Compact, Fallback);
    return Fallback;
}

void FLervikMCPModule::ApplyServerState()
{
    const bool bEnabled = CVarMcpEnable.GetValueOnGameThread() != 0;
    const uint32 Port = (uint32)CVarMcpPort.GetValueOnGameThread();

    const bool bRunning = Server.IsValid() && Server->IsRunning();

    if (!bEnabled && bRunning)
    {
        Server->Stop();
        Server.Reset();
        UE_LOG(LogLervikMCP, Log, TEXT("MCP server stopped"));
    }
    else if (bEnabled && !bRunning)
    {
        if (StartServer(Port))
        {
            UE_LOG(LogLervikMCP, Log, TEXT("MCP server started on port %u"), Port);
        }
    }
    else if (bEnabled && bRunning && Port != Server->GetPort())
    {
        Server->Stop();
        Server.Reset();
        if (StartServer(Port))
        {
            UE_LOG(LogLervikMCP, Log, TEXT("MCP server restarted on port %u"), Port);
        }
    }
}

bool FLervikMCPModule::StartServer(uint32 Port)
{
    Server = MakeUnique<FMCPServer>();
    FString StartError;
    if (Server->Start(Port, StartError))
    {
        return true;
    }
    UE_LOG(LogLervikMCP, Error, TEXT("Failed to start MCP server: %s"), *StartError);
    Server.Reset();
    return false;
}

void FLervikMCPModule::HandleStatusCommand(const TArray<FString>& Args)
{
    if (Server.IsValid() && Server->IsRunning())
    {
        UE_LOG(LogLervikMCP, Log, TEXT("MCP server: RUNNING on port %u"), Server->GetPort());
        TOptional<FMCPSession> Snapshot = Server->GetSessionSnapshot();
        if (Snapshot.IsSet())
        {
            UE_LOG(LogLervikMCP, Log, TEXT("  Session: %s (client: %s %s)"),
                *Snapshot->SessionId.ToString(EGuidFormats::DigitsWithHyphens),
                *Snapshot->ClientName, *Snapshot->ClientVersion);
        }
        else
        {
            UE_LOG(LogLervikMCP, Log, TEXT("  No active session"));
        }
    }
    else
    {
        UE_LOG(LogLervikMCP, Log, TEXT("MCP server: STOPPED"));
    }
}

IMPLEMENT_MODULE(FLervikMCPModule, LervikMCP)
