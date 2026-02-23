#include "Tools/MCPTool_Find.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPSearchPatterns.h"
#include "MCPObjectResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    TSharedPtr<FJsonObject> MakeActorJson(AActor* Actor)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("name"), Actor->GetName());
        Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
        Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

        FVector Loc = Actor->GetActorLocation();
        TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
        LocObj->SetField(TEXT("x"), FMCPJsonHelpers::RoundedJsonNumber(Loc.X));
        LocObj->SetField(TEXT("y"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Y));
        LocObj->SetField(TEXT("z"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Z));
        Obj->SetObjectField(TEXT("location"), LocObj);
        return Obj;
    }

    FMCPToolResult MakeListResult(TArray<TSharedPtr<FJsonValue>>& Items)
    {
        TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
        Res->SetArrayField(TEXT("results"), Items);
        Res->SetNumberField(TEXT("count"), Items.Num());
        return FMCPJsonHelpers::SuccessResponse(Res);
    }
}

FMCPToolInfo FMCPTool_Find::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("find");
    Info.Description = TEXT("Search for assets, actors, classes, properties, or the current selection in the UE5 editor");
    Info.Parameters = {
        { TEXT("type"),      TEXT("What to search: 'asset', 'actor', 'class', 'property', 'selection'"), TEXT("string"),  true  },
        { TEXT("class"),     TEXT("Class filter (wildcards supported)"),                                  TEXT("string"),  false },
        { TEXT("path"),      TEXT("Path/folder filter (wildcards supported)"),                            TEXT("string"),  false },
        { TEXT("name"),      TEXT("Name filter with wildcards"),                                          TEXT("string"),  false },
        { TEXT("tag"),       TEXT("Asset registry tag filter: 'tag_name=value'"),                         TEXT("string"),  false },
        { TEXT("parent"),    TEXT("(type=class) Parent class name for derived class search"),             TEXT("string"),  false },
        { TEXT("target"),    TEXT("(type=property) Object to list UObject reflection properties. For Blueprint user variables, use inspect type=variables instead"), TEXT("string"),  false },
        { TEXT("filter"),    TEXT("Post-filter glob/regex pattern applied to results"),                   TEXT("string"),  false },
        { TEXT("recursive"), TEXT("Search recursively (default: true)"),                                  TEXT("boolean"), false },
        { TEXT("limit"),     TEXT("Max number of results (default: 100)"),                                TEXT("integer"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Find::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Type;
        if (!Params->TryGetStringField(TEXT("type"), Type))
        {
            return FMCPToolResult::Error(TEXT("'type' is required"));
        }

        FString ClassName, PathStr, NameFilter, TagStr, ParentStr, TargetStr, FilterStr;
        bool bRecursive = true;
        double LimitD = 100.0;

        Params->TryGetStringField(TEXT("class"),     ClassName);
        Params->TryGetStringField(TEXT("path"),      PathStr);
        Params->TryGetStringField(TEXT("name"),      NameFilter);
        Params->TryGetStringField(TEXT("tag"),       TagStr);
        Params->TryGetStringField(TEXT("parent"),    ParentStr);
        Params->TryGetStringField(TEXT("target"),    TargetStr);
        Params->TryGetStringField(TEXT("filter"),    FilterStr);
        Params->TryGetBoolField  (TEXT("recursive"), bRecursive);
        Params->TryGetNumberField(TEXT("limit"),     LimitD);

        const int32 Limit = FMath::Max(1, FMath::FloorToInt(LimitD));

        auto PassesFilters = [&](const FString& Name) -> bool
        {
            return (NameFilter.IsEmpty() || FMCPSearchPatterns::Matches(NameFilter, Name))
                && (FilterStr.IsEmpty()  || FMCPSearchPatterns::Matches(FilterStr,  Name));
        };

        // ── type=asset ──────────────────────────────────────────────────────
        if (Type.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
        {
            IAssetRegistry& AssetRegistry =
                FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

            FARFilter ARFilter;

            if (!PathStr.IsEmpty())
            {
                ARFilter.PackagePaths.Add(FName(*PathStr));
                ARFilter.bRecursivePaths = bRecursive;
            }
            if (!ClassName.IsEmpty())
            {
                ARFilter.bRecursiveClasses = true;
                UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                if (FoundClass)
                {
                    ARFilter.ClassPaths.Add(FoundClass->GetClassPathName());
                }
                else
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Class '%s' not found. Verify the class name is correct (e.g. 'Material', 'Blueprint', 'StaticMesh')"), *ClassName));
                }
            }
            if (!TagStr.IsEmpty())
            {
                FString TagName, TagValue;
                if (TagStr.Split(TEXT("="), &TagName, &TagValue))
                {
                    ARFilter.TagsAndValues.Add(FName(*TagName), TagValue);
                }
            }

            TArray<FAssetData> Assets;
            const bool bFilterEmpty = ARFilter.PackagePaths.IsEmpty()
                && ARFilter.ClassPaths.IsEmpty()
                && ARFilter.TagsAndValues.IsEmpty();
            if (bFilterEmpty)
            {
                AssetRegistry.GetAllAssets(Assets, false);
            }
            else
            {
                AssetRegistry.GetAssets(ARFilter, Assets);
            }

            TArray<TSharedPtr<FJsonValue>> Results;
            for (const FAssetData& Asset : Assets)
            {
                if (!PassesFilters(Asset.AssetName.ToString())) continue;

                TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
                AssetObj->SetStringField(TEXT("name"),  Asset.AssetName.ToString());
                AssetObj->SetStringField(TEXT("path"),  Asset.PackageName.ToString());
                AssetObj->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
                Results.Add(MakeShared<FJsonValueObject>(AssetObj));

                if (Results.Num() >= Limit) break;
            }
            return MakeListResult(Results);
        }

        // ── type=actor ──────────────────────────────────────────────────────
        if (Type.Equals(TEXT("actor"), ESearchCase::IgnoreCase))
        {
            if (!GEditor)
            {
                return FMCPToolResult::Error(TEXT("Editor not available"));
            }
            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            TArray<TSharedPtr<FJsonValue>> Results;
            for (AActor* Actor : ActorSub->GetAllLevelActors())
            {
                if (!Actor) continue;
                if (!ClassName.IsEmpty() && !FMCPSearchPatterns::Matches(ClassName, Actor->GetClass()->GetName())) continue;
                if (!PassesFilters(Actor->GetActorLabel())) continue;

                Results.Add(MakeShared<FJsonValueObject>(MakeActorJson(Actor)));
                if (Results.Num() >= Limit) break;
            }
            return MakeListResult(Results);
        }

        // ── type=class ──────────────────────────────────────────────────────
        if (Type.Equals(TEXT("class"), ESearchCase::IgnoreCase))
        {
            UClass* ParentClass = UObject::StaticClass();
            if (!ParentStr.IsEmpty())
            {
                UClass* Found = FindFirstObject<UClass>(*ParentStr, EFindFirstObjectOptions::EnsureIfAmbiguous);
                if (Found) ParentClass = Found;
            }

            TArray<UClass*> DerivedClasses;
            GetDerivedClasses(ParentClass, DerivedClasses, true);

            TArray<TSharedPtr<FJsonValue>> Results;
            for (UClass* Class : DerivedClasses)
            {
                if (!Class) continue;
                if (!PassesFilters(Class->GetName())) continue;

                TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
                ClassObj->SetStringField(TEXT("name"), Class->GetName());
                ClassObj->SetStringField(TEXT("path"), Class->GetPathName());
                Results.Add(MakeShared<FJsonValueObject>(ClassObj));

                if (Results.Num() >= Limit) break;
            }
            return MakeListResult(Results);
        }

        // ── type=property ───────────────────────────────────────────────────
        if (Type.Equals(TEXT("property"), ESearchCase::IgnoreCase))
        {
            if (TargetStr.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'target' is required for type=property"));
            }

            FString Error;
            UObject* Obj = FMCPObjectResolver::ResolveObject(TargetStr, Error);
            if (!Obj) return FMCPToolResult::Error(Error);

            TArray<TSharedPtr<FJsonValue>> Results;
            for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
            {
                FProperty* Prop = *It;
                if (!PassesFilters(Prop->GetName())) continue;

                TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
                PropObj->SetStringField(TEXT("name"),     Prop->GetName());
                PropObj->SetStringField(TEXT("type"),     Prop->GetCPPType());
                PropObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));
                Results.Add(MakeShared<FJsonValueObject>(PropObj));

                if (Results.Num() >= Limit) break;
            }
            return MakeListResult(Results);
        }

        // ── type=selection ──────────────────────────────────────────────────
        if (Type.Equals(TEXT("selection"), ESearchCase::IgnoreCase))
        {
            if (!GEditor)
            {
                return FMCPToolResult::Error(TEXT("Editor not available"));
            }
            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            TArray<TSharedPtr<FJsonValue>> Results;
            for (AActor* Actor : ActorSub->GetSelectedLevelActors())
            {
                if (!Actor) continue;
                Results.Add(MakeShared<FJsonValueObject>(MakeActorJson(Actor)));
            }
            return MakeListResult(Results);
        }

        return FMCPToolResult::Error(FString::Printf(TEXT("Unknown type: '%s'. Valid: asset, actor, class, property, selection"), *Type));
    });
}
