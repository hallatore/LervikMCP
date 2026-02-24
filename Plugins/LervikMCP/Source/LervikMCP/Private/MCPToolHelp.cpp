#include "MCPToolHelp.h"
#include "MCPJsonHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPToolHelp
{

static void AppendParams(const FMCPParamHelp* Params, int32 Count, TArray<TSharedPtr<FJsonValue>>& OutArr)
{
    for (int32 i = 0; i < Count; ++i)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Params[i].Name);
        Obj->SetStringField(TEXT("type"), Params[i].Type);
        Obj->SetBoolField(TEXT("required"), Params[i].bRequired);
        Obj->SetStringField(TEXT("description"), Params[i].Description);
        if (Params[i].ValidValues)
            Obj->SetStringField(TEXT("valid_values"), Params[i].ValidValues);
        if (Params[i].Example)
            Obj->SetStringField(TEXT("example"), Params[i].Example);
        OutArr.Add(MakeShared<FJsonValueObject>(Obj));
    }
}

FMCPToolResult FormatHelp(const FMCPToolHelpData& Help, const FString& Topic)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("tool"), Help.ToolName);
    Root->SetBoolField(TEXT("help"), true);

    // --- Overview (empty topic) ---
    if (Topic.IsEmpty() || Topic.Equals(TEXT("true"), ESearchCase::IgnoreCase))
    {
        Root->SetStringField(TEXT("description"), Help.Description);

        if (Help.ActionCount > 0 && Help.DispatchParam && Help.DispatchParam[0] != '\0')
        {
            TArray<TSharedPtr<FJsonValue>> ActionsArr;
            for (int32 i = 0; i < Help.ActionCount; ++i)
            {
                TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                Obj->SetStringField(TEXT("name"), Help.Actions[i].Name);
                Obj->SetStringField(TEXT("description"), Help.Actions[i].Description);
                ActionsArr.Add(MakeShared<FJsonValueObject>(Obj));
            }
            Root->SetArrayField(FString::Printf(TEXT("%ss"), Help.DispatchParam), ActionsArr);
            Root->SetStringField(TEXT("hint"),
                FString::Printf(TEXT("Use help='%s_name' for detailed parameter info"),
                    Help.DispatchParam));
        }

        if (Help.CommonParamCount > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ParamsArr;
            AppendParams(Help.CommonParams, Help.CommonParamCount, ParamsArr);
            Root->SetArrayField(TEXT("parameters"), ParamsArr);
        }

        return FMCPJsonHelpers::SuccessResponse(Root);
    }

    // --- Detailed: find matching action ---
    for (int32 i = 0; i < Help.ActionCount; ++i)
    {
        if (Topic.Equals(Help.Actions[i].Name, ESearchCase::IgnoreCase))
        {
            const FMCPActionHelp& Action = Help.Actions[i];
            Root->SetStringField(FString(Help.DispatchParam), Action.Name);
            Root->SetStringField(TEXT("description"), Action.Description);

            TArray<TSharedPtr<FJsonValue>> ParamsArr;
            AppendParams(Help.CommonParams, Help.CommonParamCount, ParamsArr);
            AppendParams(Action.Params, Action.ParamCount, ParamsArr);

            Root->SetArrayField(TEXT("parameters"), ParamsArr);

            if (Action.Example)
                Root->SetStringField(TEXT("example"), Action.Example);

            return FMCPJsonHelpers::SuccessResponse(Root);
        }
    }

    // --- Unknown topic ---
    FString ValidTopics;
    for (int32 i = 0; i < Help.ActionCount; ++i)
    {
        if (!ValidTopics.IsEmpty()) ValidTopics += TEXT(", ");
        ValidTopics += Help.Actions[i].Name;
    }
    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown help topic '%s'. Valid topics: %s"),
        *Topic, *ValidTopics));
}

bool CheckAndHandleHelp(const TSharedPtr<FJsonObject>& Params, const FMCPToolHelpData& Help, FMCPToolResult& OutResult)
{
    if (!Params.IsValid() || !Params->HasField(TEXT("help")))
        return false;

    FString Topic;
    if (!Params->TryGetStringField(TEXT("help"), Topic))
    {
        // help=true or help=1 etc. → overview
        Topic = TEXT("");
    }

    // Intercept skill topics before tool-specific help
    if (Topic.Equals(TEXT("skills"), ESearchCase::IgnoreCase))
    {
        OutResult = FormatSkillList();
        return true;
    }
    if (Topic.StartsWith(TEXT("skill:"), ESearchCase::IgnoreCase))
    {
        OutResult = FormatSkill(Topic.Mid(6));
        return true;
    }

    OutResult = FormatHelp(Help, Topic);
    return true;
}

FMCPToolResult FormatSkillList()
{
    const FMCPSkillData* Skills = GetRegisteredSkills();
    const int32 Count = GetRegisteredSkillCount();

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("help"), true);
    Root->SetStringField(TEXT("topic"), TEXT("skills"));

    TArray<TSharedPtr<FJsonValue>> Arr;
    for (int32 i = 0; i < Count; ++i)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Skills[i].Name);
        Obj->SetStringField(TEXT("title"), Skills[i].Title);
        Obj->SetStringField(TEXT("description"), Skills[i].Description);
        Arr.Add(MakeShared<FJsonValueObject>(Obj));
    }
    Root->SetArrayField(TEXT("skills"), Arr);
    Root->SetStringField(TEXT("hint"), TEXT("Use help='skill:<name>' for full workflow steps"));

    return FMCPJsonHelpers::SuccessResponse(Root);
}

FMCPToolResult FormatSkill(const FString& SkillName)
{
    const FMCPSkillData* Skills = GetRegisteredSkills();
    const int32 Count = GetRegisteredSkillCount();

    for (int32 i = 0; i < Count; ++i)
    {
        if (SkillName.Equals(Skills[i].Name, ESearchCase::IgnoreCase))
        {
            const FMCPSkillData& Skill = Skills[i];

            TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
            Root->SetBoolField(TEXT("help"), true);
            Root->SetStringField(TEXT("skill"), Skill.Name);
            Root->SetStringField(TEXT("title"), Skill.Title);
            Root->SetStringField(TEXT("description"), Skill.Description);
            Root->SetStringField(TEXT("prerequisites"), Skill.Prerequisites);

            TArray<TSharedPtr<FJsonValue>> StepsArr;
            for (int32 s = 0; s < Skill.StepCount; ++s)
            {
                TSharedPtr<FJsonObject> StepObj = MakeShared<FJsonObject>();
                StepObj->SetNumberField(TEXT("step"), s + 1);
                StepObj->SetStringField(TEXT("description"), Skill.Steps[s].Description);
                StepObj->SetStringField(TEXT("example"), Skill.Steps[s].ToolCall);
                StepsArr.Add(MakeShared<FJsonValueObject>(StepObj));
            }
            Root->SetArrayField(TEXT("steps"), StepsArr);
            Root->SetStringField(TEXT("tips"), Skill.Tips);

            return FMCPJsonHelpers::SuccessResponse(Root);
        }
    }

    // Unknown skill — build valid names list
    FString ValidNames;
    for (int32 i = 0; i < Count; ++i)
    {
        if (!ValidNames.IsEmpty()) ValidNames += TEXT(", ");
        ValidNames += Skills[i].Name;
    }
    return FMCPToolResult::Error(FString::Printf(
        TEXT("Unknown skill '%s'. Valid skills: %s"), *SkillName, *ValidNames));
}

} // namespace MCPToolHelp
