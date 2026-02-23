#include "Misc/AutomationTest.h"
#include "IMCPTool.h"
#include "Features/IModularFeatures.h"

// Mock tool for testing
class FMockMCPTool : public IMCPTool
{
public:
    FName ToolName;
    FString ToolDescription;
    FMCPToolResult ResultToReturn;

    FMockMCPTool(FName InName, const FString& InDescription)
        : ToolName(InName), ToolDescription(InDescription)
    {
        ResultToReturn = FMCPToolResult::Text(TEXT("mock result"));
    }

    virtual FMCPToolInfo GetToolInfo() const override
    {
        FMCPToolInfo Info;
        Info.Name = ToolName;
        Info.Description = ToolDescription;
        return Info;
    }

    virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
    {
        return ResultToReturn;
    }
};

BEGIN_DEFINE_SPEC(FMCPToolRegistrationSpec, "Plugins.LervikMCP.ToolRegistration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    TUniquePtr<FMockMCPTool> MockTool;
    TUniquePtr<FMockMCPTool> MockTool2;
    int32 BaselineToolCount = 0;
END_DEFINE_SPEC(FMCPToolRegistrationSpec)

void FMCPToolRegistrationSpec::Define()
{
    BeforeEach([this]()
    {
        BaselineToolCount = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(IMCPTool::GetModularFeatureName()).Num();
        MockTool = MakeUnique<FMockMCPTool>(TEXT("mock_tool"), TEXT("A mock tool"));
        MockTool2 = MakeUnique<FMockMCPTool>(TEXT("mock_tool_2"), TEXT("Another mock tool"));
    });

    AfterEach([this]()
    {
        // Clean up any registered tools
        if (MockTool.IsValid())
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
        }
        if (MockTool2.IsValid())
        {
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool2.Get());
        }
        MockTool.Reset();
        MockTool2.Reset();
    });

    Describe("IMCPTool registration via IModularFeatures", [this]()
    {
        It("Can register a tool", [this]()
        {
            IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());

            TArray<IMCPTool*> Tools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(
                IMCPTool::GetModularFeatureName());
            TestTrue("Tool found", Tools.Contains(MockTool.Get()));
        });

        It("GetToolInfo returns correct info", [this]()
        {
            FMCPToolInfo Info = MockTool->GetToolInfo();
            TestEqual("Name", Info.Name, FName(TEXT("mock_tool")));
            TestEqual("Description", Info.Description, TEXT("A mock tool"));
        });

        It("Execute returns expected result", [this]()
        {
            MockTool->ResultToReturn = FMCPToolResult::Text(TEXT("custom result"));
            FMCPToolResult Result = MockTool->Execute(nullptr);
            TestEqual("Content", Result.Content, TEXT("custom result"));
            TestFalse("Not error", Result.bIsError);
        });

        It("Can unregister a tool", [this]()
        {
            IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
            IModularFeatures::Get().UnregisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());

            TArray<IMCPTool*> Tools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(
                IMCPTool::GetModularFeatureName());
            TestFalse("Tool not found", Tools.Contains(MockTool.Get()));
        });

        It("Can register and discover multiple tools", [this]()
        {
            IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool.Get());
            IModularFeatures::Get().RegisterModularFeature(IMCPTool::GetModularFeatureName(), MockTool2.Get());

            TArray<IMCPTool*> Tools = IModularFeatures::Get().GetModularFeatureImplementations<IMCPTool>(
                IMCPTool::GetModularFeatureName());
            TestTrue("Tool1 found", Tools.Contains(MockTool.Get()));
            TestTrue("Tool2 found", Tools.Contains(MockTool2.Get()));
            TestEqual("Count", Tools.Num(), BaselineToolCount + 2);
        });
    });
}
