#pragma once

#include "CoreMinimal.h"

class AActor;

class FMCPObjectResolver
{
public:
    // Must be called on game thread
    static UObject* ResolveObject(const FString& Target, FString& OutError);
    static AActor* ResolveActor(const FString& Target, FString& OutError);
    static UObject* ResolveAsset(const FString& Target, FString& OutError);
};
