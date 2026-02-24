#include "MCPJsonHelpers.h"
#include "UObject/TextProperty.h"
#include "UObject/EnumProperty.h"
#include "Misc/EngineVersionComparison.h"
#include "MCPSearchPatterns.h"
#include "MCPServer.h"
#include "LervikMCPModule.h"

// UE 5.5+ moved FStrProperty to a separate header; UE 5.7 removed the backward-compat include from UnrealType.h
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#else
#include "UObject/StrProperty.h"
#endif

FString FMCPJsonHelpers::JsonObjToString(const TSharedPtr<FJsonObject>& Obj)
{
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
    Writer->Close();
    return OutputString;
}

FMCPToolResult FMCPJsonHelpers::SuccessResponse(const TSharedPtr<FJsonObject>& Data)
{
    return FMCPToolResult::Text(JsonObjToString(Data));
}

void FMCPJsonHelpers::SetWarningsField(const TSharedPtr<FJsonObject>& Obj, const TArray<FString>& Warnings)
{
    if (Warnings.Num() > 0)
    {
        Obj->SetArrayField(TEXT("warnings"), ArrayFromStrings(Warnings));
    }
}

TSharedPtr<FJsonValue> FMCPJsonHelpers::PropertyToJsonValue(FProperty* Prop, const void* ValuePtr)
{
    if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
    {
        return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
    }

    if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
    {
        if (NumProp->IsInteger())
        {
            return MakeShared<FJsonValueNumber>((double)NumProp->GetSignedIntPropertyValue(ValuePtr));
        }
        return MakeShared<FJsonValueNumber>(NumProp->GetFloatingPointPropertyValue(ValuePtr));
    }

    if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
    {
        return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
    }

    if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
    {
        return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
    }

    if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
    {
        return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(ValuePtr).ToString());
    }

    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
    {
        FString ExportedStr;
        EnumProp->ExportTextItem_Direct(ExportedStr, ValuePtr, nullptr, nullptr, PPF_None);
        return MakeShared<FJsonValueString>(ExportedStr);
    }

    if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
    {
        if (StructProp->Struct == TBaseStructure<FGuid>::Get())
        {
            const FGuid& GuidVal = *static_cast<const FGuid*>(ValuePtr);
            return MakeShared<FJsonValueString>(GuidToCompact(GuidVal));
        }
    }

    // Fallback: export as text for structs, objects, arrays, etc.
    FString ExportedStr;
    Prop->ExportTextItem_Direct(ExportedStr, ValuePtr, nullptr, nullptr, PPF_None);
    return MakeShared<FJsonValueString>(ExportedStr);
}

TSharedPtr<FJsonObject> FMCPJsonHelpers::UObjectToJson(UObject* Obj, const FString& Filter, bool bSkipDefaults)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    if (!Obj)
    {
        return Result;
    }

    UObject* Archetype = bSkipDefaults ? Obj->GetArchetype() : nullptr;

    for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
    {
        FProperty* Prop = *It;
        if (!Filter.IsEmpty() && !FMCPSearchPatterns::Matches(Filter, Prop->GetName()))
        {
            continue;
        }

        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);

        if (Archetype)
        {
            const void* DefaultPtr = Prop->ContainerPtrToValuePtr<void>(Archetype);
            if (Prop->Identical(ValuePtr, DefaultPtr, PPF_None))
                continue;
        }

        TSharedPtr<FJsonValue> JsonValue = PropertyToJsonValue(Prop, ValuePtr);
        if (JsonValue.IsValid())
        {
            Result->SetField(Prop->GetName(), JsonValue);
        }
    }

    return Result;
}

TArray<TSharedPtr<FJsonValue>> FMCPJsonHelpers::ArrayFromStrings(const TArray<FString>& Strings)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Strings.Num());
    for (const FString& Str : Strings)
    {
        Result.Add(MakeShared<FJsonValueString>(Str));
    }
    return Result;
}

bool FMCPJsonHelpers::TryParseVector(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FVector& OutVec)
{
    const TArray<TSharedPtr<FJsonValue>>* Arr;
    if (Obj->TryGetArrayField(Key, Arr) && Arr->Num() >= 3)
    {
        OutVec.X = (*Arr)[0]->AsNumber();
        OutVec.Y = (*Arr)[1]->AsNumber();
        OutVec.Z = (*Arr)[2]->AsNumber();
        return true;
    }
    const TSharedPtr<FJsonObject>* SubObj;
    if (Obj->TryGetObjectField(Key, SubObj))
    {
        (*SubObj)->TryGetNumberField(TEXT("x"), OutVec.X);
        (*SubObj)->TryGetNumberField(TEXT("y"), OutVec.Y);
        (*SubObj)->TryGetNumberField(TEXT("z"), OutVec.Z);
        return true;
    }
    return false;
}

bool FMCPJsonHelpers::JsonValueToPropertyString(const TSharedPtr<FJsonValue>& Value, FString& OutStr)
{
    if (!Value.IsValid()) return false;

    if (Value->TryGetString(OutStr))
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* AsObj = nullptr;
    if (Value->TryGetObject(AsObj))
    {
        FString Parts;
        for (const auto& Pair : (*AsObj)->Values)
        {
            if (!Parts.IsEmpty()) Parts += TEXT(",");
            FString SubStr;
            FMCPJsonHelpers::JsonValueToPropertyString(Pair.Value, SubStr);
            Parts += Pair.Key + TEXT("=") + SubStr;
        }
        OutStr = TEXT("(") + Parts + TEXT(")");
        return true;
    }

    double AsNum = 0.0;
    bool   AsBool = false;
    if (Value->TryGetNumber(AsNum))
    {
        OutStr = FString::SanitizeFloat(AsNum);
        return true;
    }
    else if (Value->TryGetBool(AsBool))
    {
        OutStr = AsBool ? TEXT("true") : TEXT("false");
        return true;
    }

    return false;
}

TSharedPtr<FJsonValue> FMCPJsonHelpers::RoundedJsonNumber(double Val, int32 Decimals)
{
    return MakeShared<FJsonValueNumberString>(FString::Printf(TEXT("%.*f"), Decimals, Val));
}

FString FMCPJsonHelpers::GuidToCompact(const FGuid& Guid)
{
    FLervikMCPModule* Module = FModuleManager::GetModulePtr<FLervikMCPModule>("LervikMCP");
    if (Module)
    {
        return Module->GuidToCompact(Guid);
    }
    return Guid.ToString(EGuidFormats::DigitsLower);
}

FGuid FMCPJsonHelpers::CompactToGuid(const FString& Compact)
{
    FLervikMCPModule* Module = FModuleManager::GetModulePtr<FLervikMCPModule>("LervikMCP");
    if (Module)
    {
        return Module->CompactToGuid(Compact);
    }
    FGuid Fallback;
    FGuid::Parse(Compact, Fallback);
    return Fallback;
}

bool FMCPJsonHelpers::TryParseRotator(const TSharedPtr<FJsonObject>& Obj, const FString& Key, FRotator& OutRot)
{
    const TArray<TSharedPtr<FJsonValue>>* Arr;
    if (Obj->TryGetArrayField(Key, Arr) && Arr->Num() >= 3)
    {
        OutRot.Pitch = (*Arr)[0]->AsNumber();
        OutRot.Yaw   = (*Arr)[1]->AsNumber();
        OutRot.Roll  = (*Arr)[2]->AsNumber();
        return true;
    }
    const TSharedPtr<FJsonObject>* SubObj;
    if (Obj->TryGetObjectField(Key, SubObj))
    {
        (*SubObj)->TryGetNumberField(TEXT("pitch"), OutRot.Pitch);
        (*SubObj)->TryGetNumberField(TEXT("yaw"),   OutRot.Yaw);
        (*SubObj)->TryGetNumberField(TEXT("roll"),  OutRot.Roll);
        return true;
    }
    return false;
}
