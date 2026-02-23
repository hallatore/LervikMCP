#include "Misc/AutomationTest.h"
#include "LervikMCPTestProjectTestHelper.h"
#include "IMCPTool.h"
#include "MCPTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FMCPToolIntegrationSpecMockTool : public IMCPTool
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

BEGIN_DEFINE_SPEC(FMCPToolIntegrationSpec, "Plugins.LervikMCP.Integration.Tools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    FLervikMCPTestProjectTestHelper Helper;
    TUniquePtr<FMCPToolIntegrationSpecMockTool> MockTool;
END_DEFINE_SPEC(FMCPToolIntegrationSpec)

void FMCPToolIntegrationSpec::Define()
{
    BeforeEach([this]()
    {
        Helper.Setup(this);
        MockTool = MakeUnique<FMCPToolIntegrationSpecMockTool>();
        Helper.RegisterMockTool(MockTool.Get());
    });

    AfterEach([this]()
    {
        Helper.Teardown();
        MockTool.Reset();
    });

    LatentIt("tools/list includes registered mock tool with correct inputSchema", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        Helper.InitializeSession([this, Done](FString SessionId)
        {
            if (SessionId.IsEmpty())
            {
                AddError(TEXT("Failed to get session ID"));
                Done.Execute();
                return;
            }

            Helper.ListTools(SessionId, [this, Done](const TArray<TSharedPtr<FJsonValue>>& Tools)
            {
                TestEqual("Tool count includes mock", Tools.Num(), Helper.GetBaselineToolCount() + 1);

                TSharedPtr<FJsonObject> FoundTool = Helper.FindToolByName(Tools, TEXT("test_tool"));

                TestTrue("test_tool found in tools list", FoundTool.IsValid());
                if (FoundTool.IsValid())
                {
                    FString Description;
                    FoundTool->TryGetStringField(TEXT("description"), Description);
                    TestEqual("Description matches", Description, TEXT("A test tool for integration tests"));

                    const TSharedPtr<FJsonObject>* SchemaPtr;
                    if (TestTrue("Has inputSchema", FoundTool->TryGetObjectField(TEXT("inputSchema"), SchemaPtr)))
                    {
                        FString SchemaType;
                        (*SchemaPtr)->TryGetStringField(TEXT("type"), SchemaType);
                        TestEqual("Schema type is object", SchemaType, TEXT("object"));

                        const TSharedPtr<FJsonObject>* PropsPtr;
                        if (TestTrue("Has properties", (*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr)))
                        {
                            TestTrue("Has message property", (*PropsPtr)->HasField(TEXT("message")));

                            const TSharedPtr<FJsonObject>* MessagePtr;
                            if ((*PropsPtr)->TryGetObjectField(TEXT("message"), MessagePtr))
                            {
                                FString MsgType;
                                (*MessagePtr)->TryGetStringField(TEXT("type"), MsgType);
                                TestEqual("message type is string", MsgType, TEXT("string"));
                            }
                        }

                        const TArray<TSharedPtr<FJsonValue>>* RequiredArray;
                        if (TestTrue("Has required array", (*SchemaPtr)->TryGetArrayField(TEXT("required"), RequiredArray)))
                        {
                            bool bFoundMessage = false;
                            for (const auto& Val : *RequiredArray)
                            {
                                if (Val->AsString() == TEXT("message"))
                                {
                                    bFoundMessage = true;
                                    break;
                                }
                            }
                            TestTrue("message is in required array", bFoundMessage);
                        }
                    }
                }

                Done.Execute();
            });
        });
    });

    LatentIt("tools/call with valid params returns success result", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        Helper.InitializeSession([this, Done](FString SessionId)
        {
            if (SessionId.IsEmpty())
            {
                AddError(TEXT("Failed to get session ID"));
                Done.Execute();
                return;
            }

            Helper.CallTool(SessionId, TEXT("test_tool"), TEXT("{\"message\":\"hello\"}"),
                [this, Done](TSharedPtr<FJsonObject> Result, bool bIsError)
            {
                TestFalse("Not an error", bIsError);
                if (Result.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* ContentArray;
                    if (TestTrue("Has content array", Result->TryGetArrayField(TEXT("content"), ContentArray)))
                    {
                        TestTrue("Content has items", ContentArray->Num() > 0);
                        if (ContentArray->Num() > 0)
                        {
                            TSharedPtr<FJsonObject> Item = (*ContentArray)[0]->AsObject();
                            FString Text;
                            if (Item.IsValid())
                            {
                                Item->TryGetStringField(TEXT("text"), Text);
                            }
                            TestEqual("Echoed result", Text, TEXT("echo: hello"));
                        }
                    }

                    bool bToolIsError = false;
                    Result->TryGetBoolField(TEXT("isError"), bToolIsError);
                    TestFalse("isError is false in result", bToolIsError);
                }

                Done.Execute();
            });
        });
    });

    LatentIt("tools/call with missing required param returns graceful result", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        Helper.InitializeSession([this, Done](FString SessionId)
        {
            if (SessionId.IsEmpty())
            {
                AddError(TEXT("Failed to get session ID"));
                Done.Execute();
                return;
            }

            Helper.CallTool(SessionId, TEXT("test_tool"), TEXT("{}"),
                [this, Done](TSharedPtr<FJsonObject> Result, bool bIsError)
            {
                TestFalse("Not a protocol error", bIsError);
                if (Result.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* ContentArray;
                    if (TestTrue("Has content array", Result->TryGetArrayField(TEXT("content"), ContentArray)))
                    {
                        TestTrue("Content has items", ContentArray->Num() > 0);
                        if (ContentArray->Num() > 0)
                        {
                            TSharedPtr<FJsonObject> Item = (*ContentArray)[0]->AsObject();
                            FString Text;
                            if (Item.IsValid())
                            {
                                Item->TryGetStringField(TEXT("text"), Text);
                            }
                            TestEqual("Graceful fallback text", Text, TEXT("no message"));
                        }
                    }
                }

                Done.Execute();
            });
        });
    });

    LatentIt("tools/call without session header succeeds (static session)", FTimespan::FromSeconds(10.0),
        [this](const FDoneDelegate& Done)
    {
        Helper.CallTool(TEXT(""), TEXT("test_tool"), TEXT("{\"message\":\"hello\"}"),
            [this, Done](TSharedPtr<FJsonObject> Result, bool bIsError)
        {
            TestFalse("Not an error", bIsError);
            Done.Execute();
        });
    });
}
