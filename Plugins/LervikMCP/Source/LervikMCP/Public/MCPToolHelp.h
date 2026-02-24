#pragma once

#include "CoreMinimal.h"
#include "MCPTypes.h"

struct FMCPParamHelp
{
    const TCHAR* Name;
    const TCHAR* Type;          // "string", "integer", "object", etc.
    bool bRequired;
    const TCHAR* Description;
    const TCHAR* ValidValues;   // comma-separated, or nullptr
    const TCHAR* Example;       // example value, or nullptr
};

struct FMCPActionHelp
{
    const TCHAR* Name;
    const TCHAR* Description;
    const FMCPParamHelp* Params;
    int32 ParamCount;
    const TCHAR* Example;       // full JSON example, or nullptr
};

struct FMCPToolHelpData
{
    const TCHAR* ToolName;
    const TCHAR* Description;
    const TCHAR* DispatchParam;     // "action", "type", or "" for simple tools
    const FMCPActionHelp* Actions;
    int32 ActionCount;
    const FMCPParamHelp* CommonParams;
    int32 CommonParamCount;
};

struct FMCPSkillStep
{
    const TCHAR* Description;
    const TCHAR* ToolCall;      // JSON example
};

struct FMCPSkillData
{
    const TCHAR* Name;          // "materials"
    const TCHAR* Title;         // "Material Creation & Editing"
    const TCHAR* Description;
    const TCHAR* Prerequisites;
    const FMCPSkillStep* Steps;
    int32 StepCount;
    const TCHAR* Tips;
};

namespace MCPToolHelp
{
    /** Format help response. Topic="" for overview, Topic="action_name" for detailed. */
    LERVIKMCP_API FMCPToolResult FormatHelp(const FMCPToolHelpData& Help, const FString& Topic);

    /** Check params for help request. Returns true if help was requested (OutResult populated). */
    LERVIKMCP_API bool CheckAndHandleHelp(const TSharedPtr<FJsonObject>& Params, const FMCPToolHelpData& Help, FMCPToolResult& OutResult);

    /** Get registered skill definitions. */
    LERVIKMCP_API const FMCPSkillData* GetRegisteredSkills();
    LERVIKMCP_API int32 GetRegisteredSkillCount();

    /** Format skill list (help="skills") or specific skill (help="skill:name"). */
    LERVIKMCP_API FMCPToolResult FormatSkillList();
    LERVIKMCP_API FMCPToolResult FormatSkill(const FString& SkillName);
}
