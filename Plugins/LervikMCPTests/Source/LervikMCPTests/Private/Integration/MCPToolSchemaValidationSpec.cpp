#include "Misc/AutomationTest.h"
#include "LervikMCPTestProjectTestHelper.h"
#include "IMCPTool.h"
#include "MCPTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Mock tool exercising all schema shapes: plain string, array with items, union type
class FMCPSchemaValidationMockTool : public IMCPTool
{
public:
    virtual FMCPToolInfo GetToolInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = TEXT("schema_validation_test_tool");
        Info.Description = TEXT("Mock tool for schema validation tests");
        Info.Parameters = {
            { TEXT("msg"),     TEXT("A plain string param"),              TEXT("string"),       true  },
            { TEXT("coords"),  TEXT("Array of numbers"),                  TEXT("array"),        false, TEXT("number") },
            { TEXT("targets"), TEXT("String or array of strings"),        TEXT("string|array"), false, TEXT("string") },
        };
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        return FMCPToolResult::Text(TEXT("ok"));
    }
};

BEGIN_DEFINE_SPEC(FMCPToolSchemaValidationSpec, "Plugins.LervikMCP.Integration.SchemaValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    FLervikMCPTestProjectTestHelper Helper;
    TUniquePtr<FMCPSchemaValidationMockTool> MockTool;
END_DEFINE_SPEC(FMCPToolSchemaValidationSpec)

void FMCPToolSchemaValidationSpec::Define()
{
    Describe("Mock tool schema shapes", [this]()
    {
        BeforeEach([this]()
        {
            Helper.Setup(this);
            MockTool = MakeUnique<FMCPSchemaValidationMockTool>();
            Helper.RegisterMockTool(MockTool.Get());
        });

        AfterEach([this]()
        {
            Helper.Teardown();
            MockTool.Reset();
        });

        LatentIt("plain string param has type string and no oneOf or items", FTimespan::FromSeconds(10.0),
            [this](const FDoneDelegate& Done)
        {
            Helper.InitializeSession([this, Done](FString SessionId)
            {
                if (SessionId.IsEmpty()) { AddError(TEXT("No session")); Done.Execute(); return; }
                Helper.ListTools(SessionId, [this, Done](const TArray<TSharedPtr<FJsonValue>>& Tools)
                {
                    TSharedPtr<FJsonObject> Tool = Helper.FindToolByName(Tools, TEXT("schema_validation_test_tool"));
                    TestTrue("Mock tool found", Tool.IsValid());
                    if (!Tool.IsValid()) { Done.Execute(); return; }

                    const TSharedPtr<FJsonObject>* SchemaPtr;
                    Tool->TryGetObjectField(TEXT("inputSchema"), SchemaPtr);
                    const TSharedPtr<FJsonObject>* PropsPtr;
                    (*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr);

                    const TSharedPtr<FJsonObject>* MsgPtr;
                    if (TestTrue("Has msg", (*PropsPtr)->TryGetObjectField(TEXT("msg"), MsgPtr)))
                    {
                        FString MsgType;
                        (*MsgPtr)->TryGetStringField(TEXT("type"), MsgType);
                        TestEqual("msg type is string", MsgType, TEXT("string"));
                        TestFalse("msg has no oneOf", (*MsgPtr)->HasField(TEXT("oneOf")));
                        TestFalse("msg has no items", (*MsgPtr)->HasField(TEXT("items")));
                    }
                    Done.Execute();
                });
            });
        });

        LatentIt("array param has type array with items", FTimespan::FromSeconds(10.0),
            [this](const FDoneDelegate& Done)
        {
            Helper.InitializeSession([this, Done](FString SessionId)
            {
                if (SessionId.IsEmpty()) { AddError(TEXT("No session")); Done.Execute(); return; }
                Helper.ListTools(SessionId, [this, Done](const TArray<TSharedPtr<FJsonValue>>& Tools)
                {
                    TSharedPtr<FJsonObject> Tool = Helper.FindToolByName(Tools, TEXT("schema_validation_test_tool"));
                    if (!Tool.IsValid()) { AddError(TEXT("Tool not found")); Done.Execute(); return; }

                    const TSharedPtr<FJsonObject>* SchemaPtr;
                    Tool->TryGetObjectField(TEXT("inputSchema"), SchemaPtr);
                    const TSharedPtr<FJsonObject>* PropsPtr;
                    (*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr);

                    const TSharedPtr<FJsonObject>* CoordsPtr;
                    if (TestTrue("Has coords", (*PropsPtr)->TryGetObjectField(TEXT("coords"), CoordsPtr)))
                    {
                        FString CoordsType;
                        (*CoordsPtr)->TryGetStringField(TEXT("type"), CoordsType);
                        TestEqual("coords type is array", CoordsType, TEXT("array"));
                        TestFalse("coords has no oneOf", (*CoordsPtr)->HasField(TEXT("oneOf")));

                        const TSharedPtr<FJsonObject>* ItemsPtr;
                        if (TestTrue("coords has items", (*CoordsPtr)->TryGetObjectField(TEXT("items"), ItemsPtr)))
                        {
                            FString ItemsType;
                            (*ItemsPtr)->TryGetStringField(TEXT("type"), ItemsType);
                            TestEqual("coords items type is number", ItemsType, TEXT("number"));
                        }
                    }
                    Done.Execute();
                });
            });
        });

        LatentIt("union param has oneOf with correct sub-schemas", FTimespan::FromSeconds(10.0),
            [this](const FDoneDelegate& Done)
        {
            Helper.InitializeSession([this, Done](FString SessionId)
            {
                if (SessionId.IsEmpty()) { AddError(TEXT("No session")); Done.Execute(); return; }
                Helper.ListTools(SessionId, [this, Done](const TArray<TSharedPtr<FJsonValue>>& Tools)
                {
                    TSharedPtr<FJsonObject> Tool = Helper.FindToolByName(Tools, TEXT("schema_validation_test_tool"));
                    if (!Tool.IsValid()) { AddError(TEXT("Tool not found")); Done.Execute(); return; }

                    const TSharedPtr<FJsonObject>* SchemaPtr;
                    Tool->TryGetObjectField(TEXT("inputSchema"), SchemaPtr);
                    const TSharedPtr<FJsonObject>* PropsPtr;
                    (*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr);

                    const TSharedPtr<FJsonObject>* TargetsPtr;
                    if (TestTrue("Has targets", (*PropsPtr)->TryGetObjectField(TEXT("targets"), TargetsPtr)))
                    {
                        TestFalse("targets has no top-level type", (*TargetsPtr)->HasField(TEXT("type")));

                        const TArray<TSharedPtr<FJsonValue>>* OneOfArray;
                        if (TestTrue("targets has oneOf", (*TargetsPtr)->TryGetArrayField(TEXT("oneOf"), OneOfArray)))
                        {
                            TestEqual("oneOf has 2 entries", OneOfArray->Num(), 2);

                            if (OneOfArray->Num() >= 2)
                            {
                                // First: {"type":"string"}
                                TSharedPtr<FJsonObject> First = (*OneOfArray)[0]->AsObject();
                                FString FirstType;
                                First->TryGetStringField(TEXT("type"), FirstType);
                                TestEqual("First oneOf type is string", FirstType, TEXT("string"));
                                TestFalse("First oneOf has no items", First->HasField(TEXT("items")));

                                // Second: {"type":"array","items":{"type":"string"}}
                                TSharedPtr<FJsonObject> Second = (*OneOfArray)[1]->AsObject();
                                FString SecondType;
                                Second->TryGetStringField(TEXT("type"), SecondType);
                                TestEqual("Second oneOf type is array", SecondType, TEXT("array"));

                                const TSharedPtr<FJsonObject>* ItemsPtr;
                                if (TestTrue("Second oneOf has items", Second->TryGetObjectField(TEXT("items"), ItemsPtr)))
                                {
                                    FString ItemsType;
                                    (*ItemsPtr)->TryGetStringField(TEXT("type"), ItemsType);
                                    TestEqual("Items type is string", ItemsType, TEXT("string"));
                                }
                            }
                        }
                    }
                    Done.Execute();
                });
            });
        });
    });

    Describe("All registered tools schema validity", [this]()
    {
        BeforeEach([this]()
        {
            Helper.Setup(this);
        });

        AfterEach([this]()
        {
            Helper.Teardown();
        });

        LatentIt("every tool property has valid type or oneOf, and array types have items", FTimespan::FromSeconds(15.0),
            [this](const FDoneDelegate& Done)
        {
            Helper.InitializeSession([this, Done](FString SessionId)
            {
                if (SessionId.IsEmpty()) { AddError(TEXT("No session")); Done.Execute(); return; }
                Helper.ListTools(SessionId, [this, Done](const TArray<TSharedPtr<FJsonValue>>& Tools)
                {
                    const TSet<FString> ValidJsonSchemaTypes = {
                        TEXT("string"), TEXT("number"), TEXT("boolean"), TEXT("object"),
                        TEXT("array"), TEXT("integer"), TEXT("null")
                    };

                    for (const auto& ToolValue : Tools)
                    {
                        TSharedPtr<FJsonObject> ToolObj = ToolValue->AsObject();
                        if (!ToolObj.IsValid()) continue;

                        FString ToolName;
                        ToolObj->TryGetStringField(TEXT("name"), ToolName);

                        const TSharedPtr<FJsonObject>* SchemaPtr;
                        if (!ToolObj->TryGetObjectField(TEXT("inputSchema"), SchemaPtr)) continue;

                        const TSharedPtr<FJsonObject>* PropsPtr;
                        if (!(*SchemaPtr)->TryGetObjectField(TEXT("properties"), PropsPtr)) continue;

                        for (const auto& Pair : (*PropsPtr)->Values)
                        {
                            const FString& PropName = Pair.Key;
                            TSharedPtr<FJsonObject> PropSchema = Pair.Value->AsObject();
                            if (!PropSchema.IsValid()) continue;

                            bool bHasType = PropSchema->HasField(TEXT("type"));
                            bool bHasOneOf = PropSchema->HasField(TEXT("oneOf"));

                            // Must have type or oneOf
                            TestTrue(
                                FString::Printf(TEXT("[%s.%s] has type or oneOf"), *ToolName, *PropName),
                                bHasType || bHasOneOf);

                            if (bHasType)
                            {
                                FString TypeVal;
                                PropSchema->TryGetStringField(TEXT("type"), TypeVal);

                                // Type must not contain |
                                TestFalse(
                                    FString::Printf(TEXT("[%s.%s] type '%s' must not contain |"), *ToolName, *PropName, *TypeVal),
                                    TypeVal.Contains(TEXT("|")));

                                // Type must be valid
                                TestTrue(
                                    FString::Printf(TEXT("[%s.%s] type '%s' is valid JSON Schema type"), *ToolName, *PropName, *TypeVal),
                                    ValidJsonSchemaTypes.Contains(TypeVal));

                                // Array must have items
                                if (TypeVal == TEXT("array"))
                                {
                                    TestTrue(
                                        FString::Printf(TEXT("[%s.%s] array type has items"), *ToolName, *PropName),
                                        PropSchema->HasField(TEXT("items")));
                                }
                            }

                            if (bHasOneOf)
                            {
                                const TArray<TSharedPtr<FJsonValue>>* OneOfArray;
                                if (PropSchema->TryGetArrayField(TEXT("oneOf"), OneOfArray))
                                {
                                    for (int32 i = 0; i < OneOfArray->Num(); i++)
                                    {
                                        TSharedPtr<FJsonObject> Sub = (*OneOfArray)[i]->AsObject();
                                        if (!Sub.IsValid()) continue;

                                        FString SubType;
                                        Sub->TryGetStringField(TEXT("type"), SubType);
                                        TestTrue(
                                            FString::Printf(TEXT("[%s.%s] oneOf[%d] type '%s' is valid"), *ToolName, *PropName, i, *SubType),
                                            ValidJsonSchemaTypes.Contains(SubType));

                                        if (SubType == TEXT("array"))
                                        {
                                            TestTrue(
                                                FString::Printf(TEXT("[%s.%s] oneOf[%d] array has items"), *ToolName, *PropName, i),
                                                Sub->HasField(TEXT("items")));
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Done.Execute();
                });
            });
        });
    });
}
