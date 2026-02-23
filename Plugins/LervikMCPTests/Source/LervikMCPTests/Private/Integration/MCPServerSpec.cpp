#include "Misc/AutomationTest.h"
#include "MCPServer.h"
#include "IMCPTool.h"
#include "MCPTypes.h"
#include "Features/IModularFeatures.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/ThreadSafeCounter.h"

static const uint32 IntTestPort = 13370;
static const FString IntTestUrl = TEXT("http://127.0.0.1:13370/mcp");

// Mock tool for integration tests
class FMCPServerSpecMockTool : public IMCPTool
{
public:
    virtual FMCPToolInfo GetToolInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("test_tool");
        Info.Description = TEXT("A test tool for integration tests");

        FMCPToolParameter Param;
        Param.Name = TEXT("message");
        Param.Description = TEXT("The message to echo");
        Param.Type = TEXT("string");
        Param.bRequired = true;
        Info.Parameters.Add(Param);

        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        FString Message;
        if (Params.IsValid() && Params->TryGetStringField(TEXT("message"), Message))
        {
            return FMCPToolResult::Text(FString::Printf(TEXT("echo: %s"), *Message));
        }
        return FMCPToolResult::Text(TEXT("no message"));
    }
};

// Slow mock tool — sleeps in Execute to simulate long-running work
class FMCPServerSpecSlowTool : public IMCPTool
{
public:
    float SleepSeconds = 0.5f;
    FThreadSafeCounter ExecutionCount;

    virtual FMCPToolInfo GetToolInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("slow_tool");
        Info.Description = TEXT("A slow tool for shutdown tests");
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        FPlatformProcess::Sleep(SleepSeconds);
        ExecutionCount.Increment();
        return FMCPToolResult::Text(TEXT("done"));
    }
};

BEGIN_DEFINE_SPEC(FMCPServerSpec, "Plugins.LervikMCP.Integration.Server",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    TUniquePtr<FMCPServer> Server;
    TUniquePtr<FMCPServerSpecMockTool> MockTool;
    TUniquePtr<FMCPServerSpecSlowTool> SlowTool;
    bool bMockToolRegistered = false;
    bool bSlowToolRegistered = false;
    int32 BaselineToolCount = 0;

    auto MakePost(const FString& Body, const FString& SessionId = TEXT(""))
    {
        auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(IntTestUrl);
        Request->SetVerb(TEXT("POST"));
        Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        if (!SessionId.IsEmpty())
        {
            Request->SetHeader(TEXT("Mcp-Session-Id"), SessionId);
        }
        Request->SetContentAsString(Body);
        return Request;
    }

    auto MakeGet(const FString& Url)
    {
        auto Request = FHttpModule::Get().CreateRequest();
        Request->SetURL(Url);
        Request->SetVerb(TEXT("GET"));
        return Request;
    }

    FString MakeInitBody()
    {
        return TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\",\"clientInfo\":{\"name\":\"TestClient\",\"version\":\"1.0\"}}}");
    }
END_DEFINE_SPEC(FMCPServerSpec)

void FMCPServerSpec::Define()
{
    BeforeEach([this]()
    {
        MockTool.Reset();
        bMockToolRegistered = false;
        BaselineToolCount = IModularFeatures::Get()
            .GetModularFeatureImplementations<IMCPTool>(IMCPTool::GetModularFeatureName()).Num();
        Server = MakeUnique<FMCPServer>();
        FString StartError;
        if (!Server->Start(IntTestPort, StartError))
        {
            AddError(StartError);
        }
    });

    AfterEach([this]()
    {
        if (bMockToolRegistered && MockTool.IsValid())
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
            bMockToolRegistered = false;
        }
        MockTool.Reset();
        if (bSlowToolRegistered && SlowTool.IsValid())
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), SlowTool.Get());
            bSlowToolRegistered = false;
        }
        SlowTool.Reset();
        if (Server.IsValid())
        {
            Server->Stop();
            Server.Reset();
        }
    });

    It("Start() sets OutError when route is already bound", [this]()
    {
        FMCPServer SecondServer;
        FString OutError;
        const bool bResult = SecondServer.Start(IntTestPort, OutError);
        TestFalse("Second Start should fail", bResult);
        TestFalse("OutError should be non-empty", OutError.IsEmpty());
    });

    It("Server starts and IsRunning returns true", [this]()
    {
        TestTrue("Server is running", Server->IsRunning());
        TestEqual("Port matches", Server->GetPort(), IntTestPort);
    });

    LatentIt("POST initialize returns 200 with capabilities and session header", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakePost(MakeInitBody());
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TestEqual("Status 200", Response->GetResponseCode(), 200);

                FString SessionHeader = Response->GetHeader(TEXT("Mcp-Session-Id"));
                TestFalse("Session header present", SessionHeader.IsEmpty());

                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has result", JsonObj->HasField(TEXT("result")));
                    TSharedPtr<FJsonObject> Result = JsonObj->GetObjectField(TEXT("result"));
                    FString ProtocolVersion;
                    Result->TryGetStringField(TEXT("protocolVersion"), ProtocolVersion);
                    TestEqual("Protocol version", ProtocolVersion, TEXT("2024-11-05"));
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("POST invalid JSON returns parse error -32700", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakePost(TEXT("{invalid json}"));
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has error field", JsonObj->HasField(TEXT("error")));
                    TSharedPtr<FJsonObject> Error = JsonObj->GetObjectField(TEXT("error"));
                    int32 Code = 0;
                    Error->TryGetNumberField(TEXT("code"), Code);
                    TestEqual("Parse error code -32700", Code, -32700);
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("POST tools/list without session header succeeds (static session)", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakePost(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"));
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TestEqual("Status 200", Response->GetResponseCode(), 200);
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has result (no error)", JsonObj->HasField(TEXT("result")));
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("POST tools/list with valid session returns empty tools array", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            TestFalse("Got session id from init", SessionId.IsEmpty());

            auto ListRequest = MakePost(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"), SessionId);
            ListRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr ListResponse, bool bListSuccess)
            {
                TestTrue("List request succeeded", bListSuccess);
                if (bListSuccess && ListResponse.IsValid())
                {
                    TestEqual("Status 200", ListResponse->GetResponseCode(), 200);
                    TSharedPtr<FJsonObject> JsonObj;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ListResponse->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                    {
                        TestTrue("Has result", JsonObj->HasField(TEXT("result")));
                        TSharedPtr<FJsonObject> Result = JsonObj->GetObjectField(TEXT("result"));
                        const TArray<TSharedPtr<FJsonValue>>* ToolsArray;
                        if (Result.IsValid() && Result->TryGetArrayField(TEXT("tools"), ToolsArray))
                        {
                            TestEqual("No extra tools beyond baseline", ToolsArray->Num(), BaselineToolCount);
                        }
                    }
                }
                Done.Execute();
            });
            ListRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    LatentIt("Register mock tool and tools/list contains it", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        MockTool = MakeUnique<FMCPServerSpecMockTool>();
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
        bMockToolRegistered = true;

        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            auto ListRequest = MakePost(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"), SessionId);
            ListRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr ListResponse, bool bListSuccess)
            {
                TestTrue("List request succeeded", bListSuccess);
                if (bListSuccess && ListResponse.IsValid())
                {
                    TSharedPtr<FJsonObject> JsonObj;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ListResponse->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                    {
                        TSharedPtr<FJsonObject> Result = JsonObj->GetObjectField(TEXT("result"));
                        const TArray<TSharedPtr<FJsonValue>>* ToolsArray;
                        if (Result.IsValid() && Result->TryGetArrayField(TEXT("tools"), ToolsArray))
                        {
                            TestEqual("One extra tool beyond baseline", ToolsArray->Num(), BaselineToolCount + 1);
                            bool bFoundTestTool = false;
                            for (const auto& ToolValue : *ToolsArray)
                            {
                                TSharedPtr<FJsonObject> ToolObj = ToolValue->AsObject();
                                FString ToolName;
                                if (ToolObj.IsValid() && ToolObj->TryGetStringField(TEXT("name"), ToolName) && ToolName == TEXT("test_tool"))
                                {
                                    bFoundTestTool = true;
                                    break;
                                }
                            }
                            TestTrue("test_tool found in tools list", bFoundTestTool);
                        }
                    }
                }
                Done.Execute();
            });
            ListRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    LatentIt("tools/call with mock tool returns echoed result", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        MockTool = MakeUnique<FMCPServerSpecMockTool>();
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
        bMockToolRegistered = true;

        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            FString CallBody = TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"test_tool\",\"arguments\":{\"message\":\"hello\"}}}");
            auto CallRequest = MakePost(CallBody, SessionId);
            CallRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr CallResponse, bool bCallSuccess)
            {
                TestTrue("Call request succeeded", bCallSuccess);
                if (bCallSuccess && CallResponse.IsValid())
                {
                    TestEqual("Status 200", CallResponse->GetResponseCode(), 200);
                    TSharedPtr<FJsonObject> JsonObj;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CallResponse->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                    {
                        TestTrue("Has result", JsonObj->HasField(TEXT("result")));
                        TSharedPtr<FJsonObject> Result = JsonObj->GetObjectField(TEXT("result"));
                        const TArray<TSharedPtr<FJsonValue>>* ContentArray;
                        if (Result.IsValid() && Result->TryGetArrayField(TEXT("content"), ContentArray))
                        {
                            TestTrue("Has content items", ContentArray->Num() > 0);
                            if (ContentArray->Num() > 0)
                            {
                                TSharedPtr<FJsonObject> Item = (*ContentArray)[0]->AsObject();
                                FString Text;
                                Item->TryGetStringField(TEXT("text"), Text);
                                TestEqual("Echoed result", Text, TEXT("echo: hello"));
                            }
                        }
                    }
                }
                Done.Execute();
            });
            CallRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    LatentIt("tools/call with unknown tool returns error -32601", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            FString CallBody = TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"nonexistent_tool\",\"arguments\":{}}}");
            auto CallRequest = MakePost(CallBody, SessionId);
            CallRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr CallResponse, bool bCallSuccess)
            {
                TestTrue("Call request succeeded (HTTP level)", bCallSuccess);
                if (bCallSuccess && CallResponse.IsValid())
                {
                    TSharedPtr<FJsonObject> JsonObj;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CallResponse->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                    {
                        TestTrue("Has error field", JsonObj->HasField(TEXT("error")));
                        TSharedPtr<FJsonObject> Error = JsonObj->GetObjectField(TEXT("error"));
                        int32 Code = 0;
                        Error->TryGetNumberField(TEXT("code"), Code);
                        TestEqual("Method not found code -32601", Code, -32601);
                    }
                }
                Done.Execute();
            });
            CallRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    LatentIt("should accept notifications silently", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        // Notifications are handled before the session guard, so no init needed
        FString NotifBody = TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
        auto NotifRequest = MakePost(NotifBody);
        NotifRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr NotifResponse, bool bNotifSuccess)
        {
            TestTrue("Notification request succeeded", bNotifSuccess);
            if (bNotifSuccess && NotifResponse.IsValid())
            {
                TestEqual("Status 202", NotifResponse->GetResponseCode(), 202);
                TestTrue("Notification response body is empty", NotifResponse->GetContentAsString().IsEmpty());
            }
            Done.Execute();
        });
        NotifRequest->ProcessRequest();
    });

    LatentIt("should respond to ping with empty result", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            FString PingBody = TEXT("{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"ping\"}");
            auto PingRequest = MakePost(PingBody, SessionId);
            PingRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr PingResponse, bool bPingSuccess)
            {
                TestTrue("Ping request succeeded", bPingSuccess);
                if (bPingSuccess && PingResponse.IsValid())
                {
                    TestEqual("Status 200", PingResponse->GetResponseCode(), 200);
                    TSharedPtr<FJsonObject> JsonObj;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PingResponse->GetContentAsString());
                    if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                    {
                        TestTrue("Has result", JsonObj->HasField(TEXT("result")));
                        int32 Id = 0;
                        JsonObj->TryGetNumberField(TEXT("id"), Id);
                        TestEqual("Id matches", Id, 99);
                    }
                }
                Done.Execute();
            });
            PingRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    LatentIt("GET /sse returns 405 Method Not Allowed", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakeGet(TEXT("http://127.0.0.1:13370/sse"));
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TestEqual("Status 405", Response->GetResponseCode(), 405);
                TestEqual("Allow header is POST", Response->GetHeader(TEXT("Allow")), TEXT("POST"));
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has error field", JsonObj->HasField(TEXT("error")));
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("GET /mcp returns 405 Method Not Allowed", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakeGet(TEXT("http://127.0.0.1:13370/mcp"));
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TestEqual("Status 405", Response->GetResponseCode(), 405);
                TestEqual("Allow header is POST", Response->GetHeader(TEXT("Allow")), TEXT("POST"));
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has error field", JsonObj->HasField(TEXT("error")));
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("GET / returns 405 Method Not Allowed", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto Request = MakeGet(TEXT("http://127.0.0.1:13370/"));
        Request->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
        {
            TestTrue("HTTP request succeeded", bSuccess);
            if (bSuccess && Response.IsValid())
            {
                TestEqual("Status 405", Response->GetResponseCode(), 405);
                TestEqual("Allow header is POST", Response->GetHeader(TEXT("Allow")), TEXT("POST"));
                TSharedPtr<FJsonObject> JsonObj;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
                {
                    TestTrue("Has error field", JsonObj->HasField(TEXT("error")));
                }
            }
            Done.Execute();
        });
        Request->ProcessRequest();
    });

    LatentIt("should include Mcp-Session-Id in tools/list response", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        auto InitRequest = MakePost(MakeInitBody());
        InitRequest->OnProcessRequestComplete().BindLambda([this, Done](FHttpRequestPtr /*Req*/, FHttpResponsePtr InitResponse, bool bInitSuccess)
        {
            if (!bInitSuccess || !InitResponse.IsValid())
            {
                AddError(TEXT("Initialize request failed"));
                Done.Execute();
                return;
            }

            FString SessionId = InitResponse->GetHeader(TEXT("Mcp-Session-Id"));
            TestFalse("Got session id from init", SessionId.IsEmpty());

            auto ListRequest = MakePost(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"), SessionId);
            ListRequest->OnProcessRequestComplete().BindLambda([this, Done, SessionId](FHttpRequestPtr /*Req*/, FHttpResponsePtr ListResponse, bool bListSuccess)
            {
                TestTrue("List request succeeded", bListSuccess);
                if (bListSuccess && ListResponse.IsValid())
                {
                    FString ResponseSessionId = ListResponse->GetHeader(TEXT("Mcp-Session-Id"));
                    TestFalse("Mcp-Session-Id header present in tools/list response", ResponseSessionId.IsEmpty());
                    TestEqual("Session ID matches", ResponseSessionId, SessionId);
                }
                Done.Execute();
            });
            ListRequest->ProcessRequest();
        });
        InitRequest->ProcessRequest();
    });

    It("Stop() completes safely with in-flight async tool execution", [this]()
    {
        SlowTool = MakeUnique<FMCPServerSpecSlowTool>();
        SlowTool->SleepSeconds = 0.3f;
        IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), SlowTool.Get());
        bSlowToolRegistered = true;

        FString CallBody = TEXT("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"slow_tool\",\"arguments\":{}}}");
        auto CallRequest = MakePost(CallBody);
        CallRequest->OnProcessRequestComplete().BindLambda(
            [](FHttpRequestPtr, FHttpResponsePtr, bool) { /* discard */ });
        CallRequest->ProcessRequest();

        // Allow request to reach the server and dispatch to async thread
        FPlatformProcess::Sleep(0.1f);

        // Stop while the slow tool is still executing — should drain safely
        Server->Stop();

        TestFalse("Server is no longer running", Server->IsRunning());
    });
}
