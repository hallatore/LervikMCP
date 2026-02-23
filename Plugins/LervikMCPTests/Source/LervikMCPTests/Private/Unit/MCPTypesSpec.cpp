#include "Misc/AutomationTest.h"
#include "MCPTypes.h"

BEGIN_DEFINE_SPEC(FMCPTypesSpec, "Plugins.LervikMCP.Types",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMCPTypesSpec)

void FMCPTypesSpec::Define()
{
    Describe("FMCPToolResult", [this]()
    {
        It("Text() creates result with content and no error", [this]()
        {
            FMCPToolResult Result = FMCPToolResult::Text(TEXT("hello"));
            TestEqual("Content", Result.Content, TEXT("hello"));
            TestFalse("bIsError", Result.bIsError);
        });

        It("Error() creates result with error message and error flag", [this]()
        {
            FMCPToolResult Result = FMCPToolResult::Error(TEXT("something failed"));
            TestEqual("Content", Result.Content, TEXT("something failed"));
            TestTrue("bIsError", Result.bIsError);
        });
    });

    Describe("FMCPToolParameter", [this]()
    {
        It("Can be constructed with all fields", [this]()
        {
            FMCPToolParameter Param;
            Param.Name = TEXT("query");
            Param.Description = TEXT("Search query");
            Param.Type = TEXT("string");
            Param.bRequired = true;

            TestEqual("Name", Param.Name, FName(TEXT("query")));
            TestEqual("Description", Param.Description, TEXT("Search query"));
            TestEqual("Type", Param.Type, TEXT("string"));
            TestTrue("bRequired", Param.bRequired);
        });

        It("Defaults bRequired to false", [this]()
        {
            FMCPToolParameter Param;
            TestFalse("bRequired default", Param.bRequired);
        });
    });

    Describe("FMCPToolInfo", [this]()
    {
        It("Can hold name, description and parameters", [this]()
        {
            FMCPToolParameter Param;
            Param.Name = TEXT("path");
            Param.Type = TEXT("string");
            Param.bRequired = true;

            FMCPToolInfo Info;
            Info.Name = TEXT("test_tool");
            Info.Description = TEXT("A test tool");
            Info.Parameters.Add(Param);

            TestEqual("Name", Info.Name, FName(TEXT("test_tool")));
            TestEqual("Description", Info.Description, TEXT("A test tool"));
            TestEqual("Param count", Info.Parameters.Num(), 1);
            TestEqual("Param name", Info.Parameters[0].Name, FName(TEXT("path")));
        });
    });

    Describe("FMCPRequest::Parse", [this]()
    {
        It("Parses valid JSON-RPC request", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":1}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestEqual("Method", Request.Method, TEXT("tools/list"));
            TestTrue("Id is valid", Request.Id.IsValid());
        });

        It("Parses request with params object", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":2,\"params\":{\"name\":\"test\"}}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestEqual("Method", Request.Method, TEXT("tools/call"));
            TestTrue("Params valid", Request.Params.IsValid());
            TestEqual("Params.name", Request.Params->GetStringField(TEXT("name")), TEXT("test"));
        });

        It("Fails on invalid JSON", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(TEXT("{not valid json"), Request, Error);
            TestFalse("Parse failed", bOk);
            TestTrue("Has error message", !Error.IsEmpty());
        });

        It("Fails when jsonrpc version is missing", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"method\":\"test\",\"id\":1}"),
                Request, Error);
            TestFalse("Parse failed", bOk);
        });

        It("Fails when method is missing", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"id\":1}"),
                Request, Error);
            TestFalse("Parse failed", bOk);
        });

        It("Parses notification (no id field)", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\"}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestEqual("Method", Request.Method, TEXT("notifications/cancelled"));
            TestFalse("Id not set for notification", Request.Id.IsValid());
        });

        It("Parses string id", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":\"req-abc\"}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestTrue("Id valid", Request.Id.IsValid());
            TestEqual("Id type", Request.Id->Type, EJson::String);
            FString IdStr;
            Request.Id->TryGetString(IdStr);
            TestEqual("Id value", IdStr, TEXT("req-abc"));
        });

        It("Parses explicit null id", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/list\",\"id\":null}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestTrue("Id is valid (null value present)", Request.Id.IsValid());
            TestEqual("Id type is null", Request.Id->Type, EJson::Null);
            // Response with null id must serialize as \"id\": null
            FString Json = FMCPResponse::Success(Request.Id, TSharedPtr<FJsonObject>());
            TestTrue("Id serializes as null", Json.Contains(TEXT("\"id\":null")) || Json.Contains(TEXT("\"id\": null")));
        });

        It("Should succeed with array params leaving Params null", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":3,\"params\":[1,2,3]}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestFalse("Params is null for array", Request.Params.IsValid());
        });

        It("Should succeed with null params leaving Params null", [this]()
        {
            FMCPRequest Request;
            FString Error;
            bool bOk = FMCPRequest::Parse(
                TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":5,\"params\":null}"),
                Request, Error);
            TestTrue("Parse succeeded", bOk);
            TestFalse("Params is null for null params", Request.Params.IsValid());
        });
    });

    Describe("FMCPResponse", [this]()
    {
        It("Success() produces valid JSON-RPC response", [this]()
        {
            TSharedPtr<FJsonValue> Id = MakeShared<FJsonValueNumber>(42);
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("data"), TEXT("value"));

            FString Json = FMCPResponse::Success(Id, Result);
            TestTrue("Contains jsonrpc", Json.Contains(TEXT("\"jsonrpc\"")));
            TestTrue("Contains 2.0", Json.Contains(TEXT("2.0")));
            TestTrue("Contains result", Json.Contains(TEXT("\"result\"")));
            TestTrue("Contains data", Json.Contains(TEXT("\"data\"")));
            TestFalse("No error field", Json.Contains(TEXT("\"error\"")));
        });

        It("Error() produces valid JSON-RPC error response", [this]()
        {
            TSharedPtr<FJsonValue> Id = MakeShared<FJsonValueNumber>(1);
            FString Json = FMCPResponse::Error(Id, MCPErrorCodes::MethodNotFound, TEXT("Method not found"));
            TestTrue("Contains jsonrpc", Json.Contains(TEXT("\"jsonrpc\"")));
            TestTrue("Contains error", Json.Contains(TEXT("\"error\"")));
            TestTrue("Contains code", Json.Contains(TEXT("-32601")));
            TestTrue("Contains message", Json.Contains(TEXT("Method not found")));
            TestFalse("No result field", Json.Contains(TEXT("\"result\"")));
        });

        It("Handles null Id", [this]()
        {
            FString Json = FMCPResponse::Error(nullptr, MCPErrorCodes::ParseError, TEXT("Parse error"));
            TestTrue("Contains null id", Json.Contains(TEXT("null")));
        });
    });

    Describe("MCPErrorCodes", [this]()
    {
        It("Has correct values", [this]()
        {
            TestEqual("ParseError", MCPErrorCodes::ParseError, -32700);
            TestEqual("InvalidRequest", MCPErrorCodes::InvalidRequest, -32600);
            TestEqual("MethodNotFound", MCPErrorCodes::MethodNotFound, -32601);
            TestEqual("InvalidParams", MCPErrorCodes::InvalidParams, -32602);
            TestEqual("InternalError", MCPErrorCodes::InternalError, -32603);
        });
    });
}
