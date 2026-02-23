#include "Tools/MCPTool_ExecutePython.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPGraphHelpers.h"

#include "IPythonScriptPlugin.h"
#include "ScopedTransaction.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "LervikMCP"

FMCPToolInfo FMCPTool_ExecutePython::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("execute_python");
    Info.Description = TEXT("Execute a Python script in the UE5 editor. The script is wrapped in a scoped undo transaction.");
    Info.Parameters  = {
        { TEXT("code"),        TEXT("Python source code to execute. Must start with 'import unreal'"), TEXT("string"),  true },
        { TEXT("undoOnError"), TEXT("If true, undo the transaction when Python execution fails"), TEXT("boolean"), true },
    };
    return Info;
}

FMCPToolResult FMCPTool_ExecutePython::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        // 1. Extract and validate "code"
        FString Code;
        if (!Params->TryGetStringField(TEXT("code"), Code))
        {
            return FMCPToolResult::Error(TEXT("'code' is required"));
        }

        FString Trimmed = Code.TrimStart();
        static const FString RequiredPrefix = TEXT("import unreal");
        if (!Trimmed.StartsWith(RequiredPrefix))
        {
            return FMCPToolResult::Error(TEXT("Python code must begin with 'import unreal'"));
        }
        if (Trimmed.Len() > RequiredPrefix.Len())
        {
            TCHAR NextChar = Trimmed[RequiredPrefix.Len()];
            if (!FChar::IsWhitespace(NextChar) && NextChar != TEXT(';'))
            {
                return FMCPToolResult::Error(TEXT("Python code must begin with 'import unreal'"));
            }
        }

        // 2. Extract "undoOnError" (required)
        bool bUndoOnError = false;
        if (!Params->TryGetBoolField(TEXT("undoOnError"), bUndoOnError))
        {
            return FMCPToolResult::Error(TEXT("'undoOnError' is required"));
        }

        // 3. Check Python available
        IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
        if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
        {
            return FMCPToolResult::Error(TEXT("Python plugin is not available"));
        }

        // 4. Setup command
        FPythonCommandEx Cmd;
        Cmd.Command           = Code;
        Cmd.ExecutionMode     = EPythonCommandExecutionMode::ExecuteFile;
        Cmd.FileExecutionScope = EPythonFileExecutionScope::Private;
        Cmd.Flags             = EPythonCommandFlags::Unattended;

        // 5. Execute within scoped transaction
        bool bSuccess;
        {
            FScopedTransaction Transaction(LOCTEXT("MCPExecutePython", "MCP Execute Python"));
            bSuccess = PythonPlugin->ExecPythonCommandEx(Cmd);
        }

        // 6. Undo on error if requested
        if (!bSuccess && bUndoOnError && GEditor)
        {
            GEditor->UndoTransaction();
        }

        // Refresh any open Material/Blueprint editors (partial modifications may have occurred)
        FMCPGraphHelpers::RefreshAllOpenEditors();

        // 7. Collect output
        TArray<TSharedPtr<FJsonValue>> OutputLines;
        TArray<TSharedPtr<FJsonValue>> ErrorLines;
        for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
        {
            if (Entry.Type == EPythonLogOutputType::Error)
            {
                ErrorLines.Add(MakeShared<FJsonValueString>(Entry.Output));
            }
            else
            {
                OutputLines.Add(MakeShared<FJsonValueString>(Entry.Output));
            }
        }

        // 8. Build response
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"),       bSuccess);
        Result->SetArrayField(TEXT("output"),        OutputLines);
        Result->SetArrayField(TEXT("errors"),        ErrorLines);
        Result->SetStringField(TEXT("commandResult"), Cmd.CommandResult);
        if (!bSuccess && bUndoOnError)
        {
            Result->SetBoolField(TEXT("undone"), true);
        }
        return FMCPJsonHelpers::SuccessResponse(Result);
    });
}

#undef LOCTEXT_NAMESPACE
