#pragma once

#include "IMCPTool.h"

class LERVIKMCP_API FMCPTool_Execute : public IMCPTool
{
public:
    virtual FMCPToolInfo GetToolInfo() const override;
    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override;
};
