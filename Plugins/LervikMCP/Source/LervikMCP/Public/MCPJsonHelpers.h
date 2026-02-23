#pragma once

#include "CoreMinimal.h"
#include "MCPTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "UObject/Field.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

class LERVIKMCP_API FMCPJsonHelpers
{
public:
    static FString JsonObjToString(const TSharedPtr<FJsonObject>& Obj);
    static FMCPToolResult SuccessResponse(const TSharedPtr<FJsonObject>& Data);
    static TSharedPtr<FJsonValue> PropertyToJsonValue(FProperty* Prop, const void* ValuePtr);
    static TSharedPtr<FJsonObject> UObjectToJson(UObject* Obj, const FString& Filter = TEXT(""), bool bSkipDefaults = false);
    static TArray<TSharedPtr<FJsonValue>> ArrayFromStrings(const TArray<FString>& Strings);

    static FString GuidToCompact(const FGuid& Guid);
    static FGuid CompactToGuid(const FString& Compact);

    /** If Warnings is non-empty, sets a "warnings" array field on Obj. No-op otherwise. */
    static void SetWarningsField(const TSharedPtr<FJsonObject>& Obj, const TArray<FString>& Warnings);

    /** Parse a vector from either [x,y,z] array or {"x":...,"y":...,"z":...} object. */
    static bool TryParseVector(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FVector& OutVec);

    /** Parse a rotator from either [pitch,yaw,roll] array or {"pitch":...,"yaw":...,"roll":...} object. */
    static bool TryParseRotator(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FRotator& OutRot);

    /**
     * Convert a JSON value to a string suitable for UProperty::ImportText_Direct.
     * Handles string, number, bool, and object types. Returns false for null/array.
     */
    static bool JsonValueToPropertyString(const TSharedPtr<FJsonValue>& Value, FString& OutStr);

    static TSharedPtr<FJsonValue> RoundedJsonNumber(double Val, int32 Decimals = 2);
};
