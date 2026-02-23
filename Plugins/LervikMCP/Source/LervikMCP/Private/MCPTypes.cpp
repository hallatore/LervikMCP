#include "MCPTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

FMCPToolResult FMCPToolResult::Text(const FString& InContent)
{
    FMCPToolResult Result;
    Result.Content = InContent;
    Result.bIsError = false;
    return Result;
}

FMCPToolResult FMCPToolResult::Error(const FString& ErrorMessage)
{
    FMCPToolResult Result;
    Result.Content = ErrorMessage;
    Result.bIsError = true;
    return Result;
}

bool FMCPRequest::Parse(const FString& JsonString, FMCPRequest& OutRequest, FString& OutError)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        OutError = TEXT("Invalid JSON");
        return false;
    }

    FString JsonRpcVersion;
    if (!JsonObject->TryGetStringField(TEXT("jsonrpc"), JsonRpcVersion) || JsonRpcVersion != TEXT("2.0"))
    {
        OutError = TEXT("Missing or invalid jsonrpc version");
        return false;
    }

    if (!JsonObject->TryGetStringField(TEXT("method"), OutRequest.Method))
    {
        OutError = TEXT("Missing method field");
        return false;
    }

    // Params is optional
    if (JsonObject->HasField(TEXT("params")))
    {
        const TSharedPtr<FJsonObject>* ParamsObject;
        if (JsonObject->TryGetObjectField(TEXT("params"), ParamsObject))
        {
            OutRequest.Params = *ParamsObject;
        }
    }

    OutRequest.bIsNotification = !JsonObject->HasField(TEXT("id"));
    if (!OutRequest.bIsNotification)
    {
        OutRequest.Id = JsonObject->TryGetField(TEXT("id"));
    }

    return true;
}

static FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Obj)
{
    if (!Obj.IsValid())
    {
        return TEXT("{}");
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    Writer->Close();
    return OutputString;
}

FString FMCPResponse::Success(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
{
    TSharedPtr<FJsonValue> ResultValue = Result.IsValid()
        ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(Result))
        : MakeShared<FJsonValueNull>();
    return Success(Id, ResultValue);
}

FString FMCPResponse::Success(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result)
{
    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetField(TEXT("result"), Result.IsValid() ? Result : MakeShared<FJsonValueNull>());
    Response->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());

    return SerializeJsonObject(Response);
}

FString FMCPResponse::Error(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
{
    TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
    ErrorObj->SetNumberField(TEXT("code"), Code);
    ErrorObj->SetStringField(TEXT("message"), Message);

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
    Response->SetObjectField(TEXT("error"), ErrorObj);
    Response->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());

    return SerializeJsonObject(Response);
}
