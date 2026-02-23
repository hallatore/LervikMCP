#include "IMCPTool.h"

IMCPTool::IMCPTool() = default;
IMCPTool::~IMCPTool() = default;

FName IMCPTool::GetModularFeatureName()
{
    static const FName Name(TEXT("MCPTool"));
    return Name;
}
