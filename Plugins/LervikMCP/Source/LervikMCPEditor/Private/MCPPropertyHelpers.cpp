#include "MCPPropertyHelpers.h"
#include "MCPJsonHelpers.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"

void FMCPPropertyHelpers::ApplyProperties(
    UObject* Object,
    const TSharedPtr<FJsonObject>& Properties,
    TArray<FString>& OutModified,
    TArray<FString>& OutWarnings,
    const FString& Prefix)
{
    if (!Object || !Properties.IsValid()) return;

    for (const auto& KV : Properties->Values)
    {
        FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*KV.Key));
        if (!Prop)
        {
            OutWarnings.Add(FString::Printf(TEXT("Property '%s%s' not found on %s"),
                *Prefix, *KV.Key, *Object->GetClass()->GetName()));
            continue;
        }

        FString DisplayName = Prefix + KV.Key;

        // Check if value is a JSON object AND property is an object pointer → recurse into sub-object
        const TSharedPtr<FJsonObject>* NestedObj = nullptr;
        if (KV.Value->TryGetObject(NestedObj))
        {
            FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop);
            if (ObjProp)
            {
                void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
                UObject* SubObject = ObjProp->GetObjectPropertyValue(ValuePtr);
                if (SubObject)
                {
                    SubObject->Modify();
                    ApplyProperties(SubObject, *NestedObj, OutModified, OutWarnings, DisplayName + TEXT("."));
                    continue;
                }
                else
                {
                    OutWarnings.Add(FString::Printf(TEXT("Sub-object '%s' is null on %s"),
                        *DisplayName, *Object->GetClass()->GetName()));
                    continue;
                }
            }
            // Fall through to ImportText_Direct if property is not an object pointer
            // (e.g. struct properties that accept JSON-like text)
        }

        FString ValueStr;
        if (!FMCPJsonHelpers::JsonValueToPropertyString(KV.Value, ValueStr))
        {
            OutWarnings.Add(FString::Printf(TEXT("Could not convert value for '%s%s' to property string"),
                *Prefix, *KV.Key));
            continue;
        }

        void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
        const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueStr, ValuePtr, Object, PPF_None);
        if (ImportResult != nullptr)
        {
            OutModified.Add(DisplayName);
        }
        else
        {
            OutWarnings.Add(FString::Printf(TEXT("Failed to set '%s': value '%s' was rejected by %s"),
                *DisplayName, *ValueStr, *Prop->GetClass()->GetName()));
        }
    }
}
