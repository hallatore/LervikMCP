#pragma once

#include "Modules/ModuleManager.h"
#include "IMCPTool.h"

class FMCPServer;

class LERVIKMCP_API FLervikMCPModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    FMCPServer* GetServer() const;

    FString GuidToCompact(const FGuid& Guid);
    FGuid CompactToGuid(const FString& Compact);

private:
    void ApplyServerState();
    bool StartServer(uint32 Port);
    void HandleStatusCommand(const TArray<FString>& Args);

    TUniquePtr<FMCPServer> Server;
    IConsoleObject* StatusCommand = nullptr;
    TArray<TUniquePtr<IMCPTool>> RuntimeTools;
};
