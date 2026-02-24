#include "Tools/MCPTool_Execute.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPSearchPatterns.h"
#include "MCPToolHelp.h"

#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    static const FMCPParamHelp sRtExecGetCvarParams[] = {
        { TEXT("name"), TEXT("string"), true, TEXT("Console variable name"), nullptr, TEXT("r.ScreenPercentage") },
    };

    static const FMCPParamHelp sRtExecSetCvarParams[] = {
        { TEXT("name"),  TEXT("string"), true, TEXT("Console variable name"), nullptr, TEXT("r.ScreenPercentage") },
        { TEXT("value"), TEXT("string"), true, TEXT("Value to set"), nullptr, TEXT("100") },
    };

    static const FMCPParamHelp sRtExecListCvarsParams[] = {
        { TEXT("filter"),      TEXT("string"),  false, TEXT("Prefix or wildcard filter for variable names"), nullptr, TEXT("r.Shadow*") },
        { TEXT("includeHelp"), TEXT("boolean"), false, TEXT("Include help text and type. Default: false"), nullptr, nullptr },
    };

    static const FMCPActionHelp sRtExecActions[] = {
        { TEXT("get_cvar"),   TEXT("Get the current value of a console variable"), sRtExecGetCvarParams, UE_ARRAY_COUNT(sRtExecGetCvarParams), nullptr },
        { TEXT("set_cvar"),   TEXT("Set a console variable value"), sRtExecSetCvarParams, UE_ARRAY_COUNT(sRtExecSetCvarParams), nullptr },
        { TEXT("list_cvars"), TEXT("List console variables matching a filter"), sRtExecListCvarsParams, UE_ARRAY_COUNT(sRtExecListCvarsParams), nullptr },
    };

    static const FMCPToolHelpData sRtExecHelp = {
        TEXT("execute"),
        TEXT("Get, set, or list console variables (CVars)"),
        TEXT("action"),
        sRtExecActions, UE_ARRAY_COUNT(sRtExecActions),
        nullptr, 0
    };
}

FMCPToolInfo FMCPTool_Execute::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("execute");
    Info.Description = TEXT("Get, set, or list console variables (CVars)");
    Info.Parameters  = {
        { TEXT("action"),      TEXT("Values: get_cvar|set_cvar|list_cvars. Editor module adds: command"),     TEXT("string"),  true  },
        { TEXT("name"),        TEXT("[get_cvar|set_cvar] Console variable name"),                              TEXT("string"),  false },
        { TEXT("value"),       TEXT("[set_cvar] Value to set"),                                                TEXT("string"),  false },
        { TEXT("filter"),      TEXT("[list_cvars] Prefix or wildcard filter for variable names"),              TEXT("string"),  false },
        { TEXT("includeHelp"), TEXT("[list_cvars] Include help text and type. Default: false"),                TEXT("boolean"), false },
        { TEXT("help"),        TEXT("Pass help=true for overview, help='action_name' for detailed parameter info"), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Execute::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sRtExecHelp, HelpResult))
        return HelpResult;

    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Action;
        if (!Params->TryGetStringField(TEXT("action"), Action))
        {
            return FMCPToolResult::Error(TEXT("'action' is required"));
        }

        // ── action=command ───────────────────────────────────────────────────
        if (Action.Equals(TEXT("command"), ESearchCase::IgnoreCase))
        {
            return FMCPToolResult::Error(TEXT("action 'command' requires the editor module (LervikMCPEditor)"));
        }

        // ── action=get_cvar ──────────────────────────────────────────────────
        if (Action.Equals(TEXT("get_cvar"), ESearchCase::IgnoreCase))
        {
            FString Name;
            if (!Params->TryGetStringField(TEXT("name"), Name))
            {
                return FMCPToolResult::Error(TEXT("'name' is required for action=get_cvar"));
            }

            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
            if (!CVar)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("CVar '%s' not found"), *Name));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("name"),    Name);
            Result->SetStringField(TEXT("value"),   CVar->GetString());
            Result->SetStringField(TEXT("default"), CVar->GetDefaultValue());
            Result->SetStringField(TEXT("help"),    CVar->GetHelp());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── action=set_cvar ──────────────────────────────────────────────────
        if (Action.Equals(TEXT("set_cvar"), ESearchCase::IgnoreCase))
        {
            FString Name, Value;
            if (!Params->TryGetStringField(TEXT("name"), Name))
            {
                return FMCPToolResult::Error(TEXT("'name' is required for action=set_cvar"));
            }
            if (!Params->TryGetStringField(TEXT("value"), Value))
            {
                return FMCPToolResult::Error(TEXT("'value' is required for action=set_cvar"));
            }

            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
            if (!CVar)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("CVar '%s' not found"), *Name));
            }

            FString PreviousValue = CVar->GetString();
            CVar->Set(*Value, ECVF_SetByConsole);

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("name"),     Name);
            Result->SetStringField(TEXT("value"),    Value);
            Result->SetStringField(TEXT("previous"), PreviousValue);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── action=list_cvars ─────────────────────────────────────────────────
        if (Action.Equals(TEXT("list_cvars"), ESearchCase::IgnoreCase))
        {
            FString Filter;
            Params->TryGetStringField(TEXT("filter"), Filter);

            bool bIncludeHelp = false;
            Params->TryGetBoolField(TEXT("includeHelp"), bIncludeHelp);

            TArray<TSharedPtr<FJsonValue>> ResultArray;

            FString Prefix;
            if (!Filter.IsEmpty())
            {
                int32 WildcardPos = INDEX_NONE;
                if (Filter.FindChar(TEXT('*'), WildcardPos) || Filter.FindChar(TEXT('?'), WildcardPos))
                {
                    Prefix = Filter.Left(WildcardPos);
                }
                else
                {
                    Prefix = Filter;
                }
            }

            auto CollectObject = [&](const TCHAR* Name, IConsoleObject* ConsoleObj)
            {
                FString NameStr(Name);
                if (!Filter.IsEmpty() && !FMCPSearchPatterns::Matches(Filter, NameStr))
                {
                    return;
                }

                TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
                Obj->SetStringField(TEXT("name"), NameStr);

                IConsoleVariable* CVar = ConsoleObj->AsVariable();
                if (CVar)
                {
                    Obj->SetStringField(TEXT("value"), CVar->GetString());
                }

                if (bIncludeHelp)
                {
                    Obj->SetStringField(TEXT("help"), ConsoleObj->GetHelp());
                    Obj->SetStringField(TEXT("type"), CVar ? TEXT("variable") : TEXT("command"));
                }

                ResultArray.Add(MakeShared<FJsonValueObject>(Obj));
            };

            if (Prefix.IsEmpty())
            {
                IConsoleManager::Get().ForEachConsoleObjectThatContains(
                    FConsoleObjectVisitor::CreateLambda(CollectObject),
                    TEXT(""));
            }
            else
            {
                IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
                    FConsoleObjectVisitor::CreateLambda(CollectObject),
                    *Prefix);
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetArrayField(TEXT("cvars"),  ResultArray);
            Result->SetNumberField(TEXT("count"), ResultArray.Num());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(
            TEXT("Unknown action: '%s'. Valid: command, get_cvar, set_cvar, list_cvars"), *Action));
    });
}
