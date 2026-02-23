#pragma once

#include "Tools/MCPTool_Execute.h"

class FMCPTool_ExecuteEditor : public FMCPTool_Execute
{
public:
    virtual FMCPToolInfo GetToolInfo() const override;
    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override;
};
