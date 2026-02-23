#pragma once

#include "IMCPTool.h"

class FMCPTool_Inspect : public IMCPTool
{
public:
    virtual FMCPToolInfo GetToolInfo() const override;
    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override;
};
