#pragma once

#include "Misc/AutomationTest.h"
#include "MCPServer.h"
#include "IMCPTool.h"
#include "MCPTypes.h"
#include "Features/IModularFeatures.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

class FLervikMCPTestProjectTestHelper
{
public:
    static constexpr uint32 Port = 13371;

    FLervikMCPTestProjectTestHelper() = default;
    ~FLervikMCPTestProjectTestHelper() { Teardown(); }

    void Setup(FAutomationTestBase* InTest)
    {
        Test = InTest;
        BaselineToolCount = IModularFeatures::Get()
            .GetModularFeatureImplementations<IMCPTool>(IMCPTool::GetModularFeatureName()).Num();
        Server = MakeUnique<FMCPServer>();
        FString StartError;
        if (!Server->Start(Port, StartError))
        {
            Test->AddError(StartError);
        }
    }

    void Teardown()
    {
        UnregisterAllMockTools();
        if (Server.IsValid())
        {
            Server->Stop();
            Server.Reset();
        }
        Test = nullptr;
    }

    FHttpRequestPtr MakePost(const FString& Body, const FString& SessionId = TEXT("")) const
    {
        auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(MakeUrl());
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        if (!SessionId.IsEmpty())
        {
            Request->SetHeader(TEXT("Mcp-Session-Id"), SessionId);
        }
        Request->SetContentAsString(Body);
        return Request;
    }

    FString MakeInitBody() const
    {
        return TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                    "\"params\":{\"protocolVersion\":\"2024-11-05\","
                    "\"clientInfo\":{\"name\":\"TestClient\",\"version\":\"1.0\"}}}");
    }

    void InitializeSession(TFunction<void(FString SessionId)> Callback)
    {
        auto Request = MakePost(MakeInitBody());
        Request->OnProcessRequestComplete().BindLambda(
            [this, Cb = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
            {
                if (!bSuccess || !Response.IsValid())
                {
                    if (Test) Test->AddError(TEXT("InitializeSession: HTTP request failed"));
                    Cb(TEXT(""));
                    return;
                }
                Cb(Response->GetHeader(TEXT("Mcp-Session-Id")));
            });
        Request->ProcessRequest();
    }

    void CallTool(const FString& SessionId, const FString& ToolName, const FString& ArgumentsJson,
                  TFunction<void(TSharedPtr<FJsonObject> Result, bool bIsError)> Callback)
    {
        const int32 ReqId = NextRequestId();
        const FString Body = FString::Printf(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/call\","
                 "\"params\":{\"name\":\"%s\",\"arguments\":%s}}"),
            ReqId, *ToolName, *ArgumentsJson);

        auto Request = MakePost(Body, SessionId);
        Request->OnProcessRequestComplete().BindLambda(
            [this, Cb = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
            {
                TSharedPtr<FJsonObject> JsonObj = ParseResponse(Response, bSuccess);
                if (!JsonObj.IsValid()) { Cb(nullptr, true); return; }

                if (JsonObj->HasField(TEXT("error")))
                {
                    Cb(JsonObj->GetObjectField(TEXT("error")), true);
                }
                else
                {
                    Cb(JsonObj->GetObjectField(TEXT("result")), false);
                }
            });
        Request->ProcessRequest();
    }

    void ListTools(const FString& SessionId,
                   TFunction<void(const TArray<TSharedPtr<FJsonValue>>& Tools)> Callback)
    {
        const int32 ReqId = NextRequestId();
        const FString Body = FString::Printf(
            TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"tools/list\",\"params\":{}}"), ReqId);

        auto Request = MakePost(Body, SessionId);
        Request->OnProcessRequestComplete().BindLambda(
            [this, Cb = MoveTemp(Callback)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
            {
                static const TArray<TSharedPtr<FJsonValue>> Empty;
                TSharedPtr<FJsonObject> JsonObj = ParseResponse(Response, bSuccess);
                if (!JsonObj.IsValid()) { Cb(Empty); return; }

                TSharedPtr<FJsonObject> Result = JsonObj->GetObjectField(TEXT("result"));
                const TArray<TSharedPtr<FJsonValue>>* ToolsArray;
                if (Result.IsValid() && Result->TryGetArrayField(TEXT("tools"), ToolsArray))
                {
                    Cb(*ToolsArray);
                }
                else
                {
                    if (Test) Test->AddError(TEXT("ListTools: missing result.tools in response"));
                    Cb(Empty);
                }
            });
        Request->ProcessRequest();
    }

    TSharedPtr<FJsonObject> FindToolByName(const TArray<TSharedPtr<FJsonValue>>& Tools, const FString& Name) const
    {
        for (const auto& ToolValue : Tools)
        {
            TSharedPtr<FJsonObject> ToolObj = ToolValue->AsObject();
            FString ToolName;
            if (ToolObj.IsValid() && ToolObj->TryGetStringField(TEXT("name"), ToolName) && ToolName == Name)
            {
                return ToolObj;
            }
        }
        return nullptr;
    }

    void RegisterMockTool(IMCPTool* Tool)
    {
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), Tool);
        RegisteredMockTools.Add(Tool);
    }

    void UnregisterAllMockTools()
    {
        for (IMCPTool* Tool : RegisteredMockTools)
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), Tool);
        }
        RegisteredMockTools.Empty();
    }

    int32 GetBaselineToolCount() const { return BaselineToolCount; }

private:
    TSharedPtr<FJsonObject> ParseResponse(const FHttpResponsePtr& Response, bool bHttpSuccess) const
    {
        if (!bHttpSuccess || !Response.IsValid())
        {
            if (Test) Test->AddError(TEXT("HTTP request failed or response invalid"));
            return nullptr;
        }
        TSharedPtr<FJsonObject> JsonObj;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
        if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
        {
            if (Test) Test->AddError(TEXT("Failed to parse JSON response"));
            return nullptr;
        }
        return JsonObj;
    }

    FString MakeUrl() const
    {
        return FString::Printf(TEXT("http://127.0.0.1:%u/mcp"), Port);
    }

    int32 NextRequestId() { return RequestIdCounter++; }

    FAutomationTestBase* Test = nullptr;
    TUniquePtr<FMCPServer> Server;
    TArray<IMCPTool*> RegisteredMockTools;
    int32 BaselineToolCount = 0;
    int32 RequestIdCounter = 1;
};
