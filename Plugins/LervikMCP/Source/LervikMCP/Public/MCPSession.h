#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

struct LERVIKMCP_API FMCPSession
{
    FGuid SessionId;
    FString ClientName;
    FString ClientVersion;
    FString ProtocolVersion;
    FDateTime CreatedAt;
};

class LERVIKMCP_API FMCPSessionManager
{
public:
    FMCPSession CreateSession(const FString& ClientName, const FString& ClientVersion, const FString& ProtocolVersion);
    bool HasSession() const;
    const FMCPSession* GetSession() const;
    void DestroySession();

    FString GuidToCompact(const FGuid& Guid);
    FGuid CompactToGuid(const FString& Compact);
    void ResetGuidMap();

private:
    TOptional<FMCPSession> CurrentSession;

    TMap<FGuid, int32> GuidToIndexMap;
    TArray<FGuid> IndexToGuidArray;
    int32 NextGuidIndex = 0;
    FCriticalSection GuidMapLock;
};
