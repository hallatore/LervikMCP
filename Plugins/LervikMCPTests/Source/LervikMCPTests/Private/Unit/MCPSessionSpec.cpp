#include "Misc/AutomationTest.h"
#include "MCPSession.h"

BEGIN_DEFINE_SPEC(FMCPSessionSpec, "Plugins.LervikMCP.Session",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
    TUniquePtr<FMCPSessionManager> SessionManager;
END_DEFINE_SPEC(FMCPSessionSpec)

void FMCPSessionSpec::Define()
{
    BeforeEach([this]()
    {
        SessionManager = MakeUnique<FMCPSessionManager>();
    });

    AfterEach([this]()
    {
        SessionManager.Reset();
    });

    Describe("CreateSession", [this]()
    {
        It("Returns session with valid GUID", [this]()
        {
            FMCPSession Session = SessionManager->CreateSession(TEXT("TestClient"), TEXT("1.0"), TEXT("2024-11-05"));
            TestTrue("GUID is valid", Session.SessionId.IsValid());
        });

        It("Stores client info correctly", [this]()
        {
            FMCPSession Session = SessionManager->CreateSession(TEXT("MyClient"), TEXT("2.0"), TEXT("2024-11-05"));
            TestEqual("ClientName", Session.ClientName, TEXT("MyClient"));
            TestEqual("ClientVersion", Session.ClientVersion, TEXT("2.0"));
            TestEqual("ProtocolVersion", Session.ProtocolVersion, TEXT("2024-11-05"));
        });

        It("Sets creation time", [this]()
        {
            FDateTime Before = FDateTime::UtcNow();
            FMCPSession Session = SessionManager->CreateSession(TEXT("Client"), TEXT("1.0"), TEXT("2024-11-05"));
            FDateTime After = FDateTime::UtcNow();
            TestTrue("CreatedAt >= Before", Session.CreatedAt >= Before);
            TestTrue("CreatedAt <= After", Session.CreatedAt <= After);
        });
    });

    Describe("HasSession", [this]()
    {
        It("Returns false before any session created", [this]()
        {
            TestFalse("No session", SessionManager->HasSession());
        });

        It("Returns true after session created", [this]()
        {
            SessionManager->CreateSession(TEXT("Client"), TEXT("1.0"), TEXT("2024-11-05"));
            TestTrue("Has session", SessionManager->HasSession());
        });
    });

    Describe("GetSession", [this]()
    {
        It("Returns nullptr when no session exists", [this]()
        {
            TestNull("No session", SessionManager->GetSession());
        });

        It("Returns pointer to current session", [this]()
        {
            FMCPSession Created = SessionManager->CreateSession(TEXT("Client"), TEXT("1.0"), TEXT("2024-11-05"));
            const FMCPSession* Retrieved = SessionManager->GetSession();
            TestNotNull("Has session", Retrieved);
            TestEqual("Same ID", Retrieved->SessionId, Created.SessionId);
        });
    });

    Describe("DestroySession", [this]()
    {
        It("Clears the current session", [this]()
        {
            SessionManager->CreateSession(TEXT("Client"), TEXT("1.0"), TEXT("2024-11-05"));
            SessionManager->DestroySession();
            TestFalse("No session", SessionManager->HasSession());
            TestNull("Null pointer", SessionManager->GetSession());
        });
    });

    Describe("Session idempotency", [this]()
    {
        It("CreateSession returns existing session if one exists", [this]()
        {
            FMCPSession First = SessionManager->CreateSession(TEXT("Client1"), TEXT("1.0"), TEXT("2024-11-05"));
            FMCPSession Second = SessionManager->CreateSession(TEXT("Client2"), TEXT("2.0"), TEXT("2024-11-05"));

            TestTrue("Has session", SessionManager->HasSession());
            TestEqual("Same session ID", First.SessionId, Second.SessionId);

            const FMCPSession* Current = SessionManager->GetSession();
            TestEqual("Still original client", Current->ClientName, TEXT("Client1"));
        });

        It("GuidMap not reset on re-create", [this]()
        {
            SessionManager->CreateSession(TEXT("Client"), TEXT("1.0"), TEXT("2024-11-05"));
            FGuid TestGuid = FGuid::NewGuid();
            FString Compact = SessionManager->GuidToCompact(TestGuid);
            TestFalse("Compact not empty", Compact.IsEmpty());

            // Second create should not reset the guid map
            SessionManager->CreateSession(TEXT("Client2"), TEXT("2.0"), TEXT("2024-11-05"));
            FGuid Recovered = SessionManager->CompactToGuid(Compact);
            TestEqual("GuidMap preserved", Recovered, TestGuid);
        });
    });
}
