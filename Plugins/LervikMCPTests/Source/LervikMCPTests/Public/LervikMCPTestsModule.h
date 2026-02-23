#pragma once

#include "Modules/ModuleManager.h"

class FLervikMCPTestsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
