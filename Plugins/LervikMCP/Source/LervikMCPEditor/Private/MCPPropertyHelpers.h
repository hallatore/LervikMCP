#pragma once
#include "CoreMinimal.h"

class FJsonObject;

class FMCPPropertyHelpers
{
public:
    /**
     * Apply a JSON property bag to a UObject via reflection.
     * Supports nested sub-object properties (e.g. StaticMeshComponent: { StaticMesh: "..." }).
     * When a key maps to an FObjectPropertyBase and the JSON value is an object,
     * resolves the sub-object and recursively applies the nested properties.
     */
    static void ApplyProperties(
        UObject* Object,
        const TSharedPtr<FJsonObject>& Properties,
        TArray<FString>& OutModified,
        TArray<FString>& OutWarnings,
        const FString& Prefix = TEXT(""));
};
