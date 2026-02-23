#include "MCPSession.h"
#include "Misc/Base64.h"

FMCPSession FMCPSessionManager::CreateSession(const FString& ClientName, const FString& ClientVersion, const FString& ProtocolVersion)
{
    if (CurrentSession.IsSet())
    {
        return CurrentSession.GetValue();
    }

    FMCPSession Session;
    Session.SessionId = FGuid::NewGuid();
    Session.ClientName = ClientName;
    Session.ClientVersion = ClientVersion;
    Session.ProtocolVersion = ProtocolVersion;
    Session.CreatedAt = FDateTime::UtcNow();

    CurrentSession = Session;
    ResetGuidMap();
    return Session;
}

bool FMCPSessionManager::HasSession() const
{
    return CurrentSession.IsSet();
}

const FMCPSession* FMCPSessionManager::GetSession() const
{
    return CurrentSession.IsSet() ? &CurrentSession.GetValue() : nullptr;
}

void FMCPSessionManager::DestroySession()
{
    CurrentSession.Reset();
    ResetGuidMap();
}

void FMCPSessionManager::ResetGuidMap()
{
    FScopeLock Lock(&GuidMapLock);
    GuidToIndexMap.Empty();
    IndexToGuidArray.Empty();
    NextGuidIndex = 0;
}

FString FMCPSessionManager::GuidToCompact(const FGuid& Guid)
{
    FScopeLock Lock(&GuidMapLock);
    int32* Found = GuidToIndexMap.Find(Guid);
    int32 Index;
    if (Found)
    {
        Index = *Found;
    }
    else
    {
        Index = NextGuidIndex++;
        GuidToIndexMap.Add(Guid, Index);
        IndexToGuidArray.Add(Guid);
    }

    // Encode index as minimal big-endian bytes, then base64
    TArray<uint8> Bytes;
    if (Index == 0)
    {
        Bytes.Add(0);
    }
    else
    {
        int32 Temp = Index;
        while (Temp > 0)
        {
            Bytes.Insert(static_cast<uint8>(Temp & 0xFF), 0);
            Temp >>= 8;
        }
    }
    FString B64 = FBase64::Encode(Bytes.GetData(), Bytes.Num());
    // Strip trailing '='
    while (B64.Len() > 0 && B64[B64.Len() - 1] == TEXT('='))
    {
        B64.LeftChopInline(1);
    }
    return B64;
}

FGuid FMCPSessionManager::CompactToGuid(const FString& Compact)
{
    // Try raw GUID parse first (backward compat)
    FGuid RawGuid;
    if (Compact.Len() == 32 && FGuid::Parse(Compact, RawGuid))
    {
        return RawGuid;
    }

    // Re-pad base64
    FString Padded = Compact;
    while (Padded.Len() % 4 != 0)
    {
        Padded += TEXT("=");
    }

    TArray<uint8> Bytes;
    if (!FBase64::Decode(Padded, Bytes))
    {
        return FGuid();
    }

    // Reconstruct index from big-endian bytes
    int32 Index = 0;
    for (uint8 B : Bytes)
    {
        Index = (Index << 8) | B;
    }

    FScopeLock Lock(&GuidMapLock);
    if (IndexToGuidArray.IsValidIndex(Index))
    {
        return IndexToGuidArray[Index];
    }
    return FGuid();
}
