#pragma once

#include "Modules/ModuleManager.h"

class IMCPTool;

class LERVIKMCPEDITOR_API FLervikMCPEditorModule : public IModuleInterface
{
public:
    FLervikMCPEditorModule() = default;
    FLervikMCPEditorModule(const FLervikMCPEditorModule&) = delete;
    FLervikMCPEditorModule& operator=(const FLervikMCPEditorModule&) = delete;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();

    TArray<TUniquePtr<IMCPTool>> Tools;
};
