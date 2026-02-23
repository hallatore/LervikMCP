#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPErrorCodes
{
    constexpr int32 ParseError = -32700;
    constexpr int32 InvalidRequest = -32600;
    constexpr int32 MethodNotFound = -32601;
    constexpr int32 InvalidParams = -32602;
    constexpr int32 InternalError = -32603;
}

struct LERVIKMCP_API FMCPToolParameter
{
    FName Name;
    FString Description;
    FString Type; // JSON Schema type: "string", "number", "boolean", "object", "array"
    bool bRequired = false;
    FString ItemsType; // For array types: the "type" value for items schema (e.g. "string", "number", "object"). Empty = permissive {}.
};

struct LERVIKMCP_API FMCPToolInfo
{
    FName Name;
    FString Description;
    TArray<FMCPToolParameter> Parameters;
};

struct LERVIKMCP_API FMCPToolResult
{
    FString Content;
    bool bIsError = false;

    static FMCPToolResult Text(const FString& InContent);
    static FMCPToolResult Error(const FString& ErrorMessage);
};

struct LERVIKMCP_API FMCPRequest
{
    FString Method;
    TSharedPtr<FJsonObject> Params;
    TSharedPtr<FJsonValue> Id;
    bool bIsNotification = false;

    static bool Parse(const FString& JsonString, FMCPRequest& OutRequest, FString& OutError);
};

struct LERVIKMCP_API FMCPResponse
{
    static FString Success(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
    static FString Success(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result);
    static FString Error(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);
};
