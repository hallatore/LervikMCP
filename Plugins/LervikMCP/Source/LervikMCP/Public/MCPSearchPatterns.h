#pragma once

#include "CoreMinimal.h"

class LERVIKMCP_API FMCPSearchPatterns
{
public:
    static bool Matches(const FString& Pattern, const FString& Value);
    static FString WildcardToRegex(const FString& Wildcard);
    static TArray<FString> FilterStrings(const TArray<FString>& Values, const FString& Pattern);
};
