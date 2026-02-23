#pragma once

#include "Features/IModularFeature.h"
#include "MCPTypes.h"

class LERVIKMCP_API IMCPTool : public IModularFeature
{
public:
    static FName GetModularFeatureName();

    IMCPTool();
    virtual ~IMCPTool();
    virtual FMCPToolInfo GetToolInfo() const = 0;
    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) = 0;
};
