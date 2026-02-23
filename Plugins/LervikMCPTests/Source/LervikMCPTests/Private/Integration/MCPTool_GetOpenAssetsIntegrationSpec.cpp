#include "Misc/AutomationTest.h"
#include "LervikMCPTestProjectTestHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

BEGIN_DEFINE_SPEC(FMCPGetOpenAssetsIntegrationSpec, "Plugins.LervikMCP.Integration.Tools.GetOpenAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    FLervikMCPTestProjectTestHelper Helper;
END_DEFINE_SPEC(FMCPGetOpenAssetsIntegrationSpec)

void FMCPGetOpenAssetsIntegrationSpec::Define()
{
    BeforeEach([this]()
    {
        Helper.Setup(this);
    });

    AfterEach([this]()
    {
        Helper.Teardown();
    });

    LatentIt("get_open_assets appears in tools/list with correct name and description", FTimespan::FromSeconds(10.0),
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
                TSharedPtr<FJsonObject> FoundTool = Helper.FindToolByName(Tools, TEXT("get_open_assets"));

                TestTrue("get_open_assets found in tools list", FoundTool.IsValid());
                if (FoundTool.IsValid())
                {
                    FString Description;
                    FoundTool->TryGetStringField(TEXT("description"), Description);
                    TestEqual("Description matches", Description,
                        TEXT("Returns the name, path and type of all currently open assets in the editor"));
                }

                Done.Execute();
            });
        });
    });

    LatentIt("get_open_assets has empty properties and no required array in inputSchema", FTimespan::FromSeconds(10.0),
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
                TSharedPtr<FJsonObject> FoundTool = Helper.FindToolByName(Tools, TEXT("get_open_assets"));

                if (TestTrue("get_open_assets found", FoundTool.IsValid()))
                {
                    const TSharedPtr<FJsonObject>* SchemaPtr;
                    if (TestTrue("Has inputSchema", FoundTool->TryGetObjectField(TEXT("inputSchema"), SchemaPtr)))
                    {
                        FString SchemaType;
                        (*SchemaPtr)->TryGetStringField(TEXT("type"), SchemaType);
                        TestEqual("Schema type is object", SchemaType, TEXT("object"));

                        const TSharedPtr<FJsonObject>* PropsPtr;
                        if (TestTrue("Has properties field", (*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr)))
                        {
                            TestTrue("Properties is empty", (*PropsPtr)->Values.IsEmpty());
                        }

                        TestFalse("No required array", (*SchemaPtr)->HasField(TEXT("required")));
                    }
                }

                Done.Execute();
            });
        });
    });

    LatentIt("get_open_assets returns JSON with assets array and count", FTimespan::FromSeconds(10.0),
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

            Helper.CallTool(SessionId, TEXT("get_open_assets"), TEXT("{}"),
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
                            FString TextContent;
                            if (Item.IsValid())
                            {
                                Item->TryGetStringField(TEXT("text"), TextContent);
                            }

                            // Parse the tool's JSON output from content text
                            TSharedPtr<FJsonObject> Payload;
                            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TextContent);
                            if (TestTrue("Content text is valid JSON", FJsonSerializer::Deserialize(Reader, Payload) && Payload.IsValid()))
                            {
                                TestTrue("Has assets array", Payload->HasTypedField<EJson::Array>(TEXT("assets")));
                                TestTrue("Has count field", Payload->HasTypedField<EJson::Number>(TEXT("count")));
                            }
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

    LatentIt("get_open_assets returns count=0 and empty assets array when no assets are open", FTimespan::FromSeconds(10.0),
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

            Helper.CallTool(SessionId, TEXT("get_open_assets"), TEXT("{}"),
                [this, Done](TSharedPtr<FJsonObject> Result, bool bIsError)
            {
                TestFalse("Not an error", bIsError);
                if (Result.IsValid())
                {
                    const TArray<TSharedPtr<FJsonValue>>* ContentArray;
                    if (TestTrue("Has content array", Result->TryGetArrayField(TEXT("content"), ContentArray))
                        && ContentArray->Num() > 0)
                    {
                        TSharedPtr<FJsonObject> Item = (*ContentArray)[0]->AsObject();
                        FString TextContent;
                        if (Item.IsValid()) Item->TryGetStringField(TEXT("text"), TextContent);

                        TSharedPtr<FJsonObject> Payload;
                        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TextContent);
                        if (TestTrue("Content text is valid JSON", FJsonSerializer::Deserialize(Reader, Payload) && Payload.IsValid()))
                        {
                            double Count = -1.0;
                            Payload->TryGetNumberField(TEXT("count"), Count);
                            TestEqual("count is 0 when no assets open", (int32)Count, 0);

                            const TArray<TSharedPtr<FJsonValue>>* Assets;
                            if (TestTrue("Has assets array", Payload->TryGetArrayField(TEXT("assets"), Assets)))
                            {
                                TestEqual("assets array is empty", Assets->Num(), 0);
                            }
                        }
                    }
                }
                Done.Execute();
            });
        });
    });
}
