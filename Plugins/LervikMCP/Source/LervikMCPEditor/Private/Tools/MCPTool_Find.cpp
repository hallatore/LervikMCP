#include "Tools/MCPTool_Find.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPSearchPatterns.h"
#include "MCPToolHelp.h"
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

    // ── Help data ────────────────────────────────────────────────────────

    static const FMCPParamHelp sFindAssetParams[] = {
        { TEXT("class"),     TEXT("string"),  false, TEXT("Class filter (wildcards supported)"), TEXT("Blueprint, Material, StaticMesh, Texture2D"), TEXT("Material") },
        { TEXT("path"),      TEXT("string"),  false, TEXT("Path/folder filter (wildcards supported)"), nullptr, TEXT("/Game/Materials") },
        { TEXT("name"),      TEXT("string"),  false, TEXT("Name filter (wildcards supported)"), nullptr, TEXT("M_*") },
        { TEXT("tag"),       TEXT("string"),  false, TEXT("Asset registry tag filter. Format: tag_name=value"), nullptr, nullptr },
        { TEXT("filter"),    TEXT("string"),  false, TEXT("Post-filter glob/regex on result names"), nullptr, nullptr },
        { TEXT("recursive"), TEXT("boolean"), false, TEXT("Search recursively. Default: true"), nullptr, nullptr },
        { TEXT("limit"),     TEXT("integer"), false, TEXT("Max results. Default: 100"), nullptr, nullptr },
    };

    static const FMCPParamHelp sFindActorParams[] = {
        { TEXT("class"),  TEXT("string"),  false, TEXT("Actor class filter (wildcards supported)"), nullptr, TEXT("PointLight") },
        { TEXT("name"),   TEXT("string"),  false, TEXT("Name/label filter (wildcards supported)"), nullptr, nullptr },
        { TEXT("filter"), TEXT("string"),  false, TEXT("Post-filter glob/regex on result names"), nullptr, nullptr },
        { TEXT("limit"),  TEXT("integer"), false, TEXT("Max results. Default: 100"), nullptr, nullptr },
    };

    static const FMCPParamHelp sFindClassParams[] = {
        { TEXT("parent"), TEXT("string"),  false, TEXT("Parent class name for derived class search"), nullptr, TEXT("Actor") },
        { TEXT("name"),   TEXT("string"),  false, TEXT("Name filter (wildcards supported)"), nullptr, nullptr },
        { TEXT("filter"), TEXT("string"),  false, TEXT("Post-filter glob/regex on result names"), nullptr, nullptr },
        { TEXT("limit"),  TEXT("integer"), false, TEXT("Max results. Default: 100"), nullptr, nullptr },
    };

    static const FMCPParamHelp sFindPropertyParams[] = {
        { TEXT("target"), TEXT("string"),  true,  TEXT("Object path to list UProperty names"), nullptr, TEXT("/Game/BP_MyActor.BP_MyActor") },
        { TEXT("filter"), TEXT("string"),  false, TEXT("Post-filter glob/regex on result names"), nullptr, nullptr },
    };

    static const FMCPParamHelp sFindSelectionParams[] = {
        { TEXT("filter"), TEXT("string"),  false, TEXT("Post-filter glob/regex on result names"), nullptr, nullptr },
    };

    static const FMCPActionHelp sFindActions[] = {
        { TEXT("asset"),     TEXT("Search assets in the Asset Registry"), sFindAssetParams, UE_ARRAY_COUNT(sFindAssetParams), nullptr },
        { TEXT("actor"),     TEXT("Search actors in the current level"), sFindActorParams, UE_ARRAY_COUNT(sFindActorParams), nullptr },
        { TEXT("class"),     TEXT("Find classes derived from a parent class"), sFindClassParams, UE_ARRAY_COUNT(sFindClassParams), nullptr },
        { TEXT("property"),  TEXT("List UProperty names on an object"), sFindPropertyParams, UE_ARRAY_COUNT(sFindPropertyParams), nullptr },
        { TEXT("selection"), TEXT("Get currently selected actors"), sFindSelectionParams, UE_ARRAY_COUNT(sFindSelectionParams), nullptr },
    };

    static const FMCPToolHelpData sFindHelp = {
        TEXT("find"),
        TEXT("Search for assets, actors, classes, properties, or the current selection in the UE5 editor"),
        TEXT("type"),
        sFindActions, UE_ARRAY_COUNT(sFindActions),
        nullptr, 0
    };
}

FMCPToolInfo FMCPTool_Find::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name = TEXT("find");
    Info.Description = TEXT("Search for assets, actors, classes, properties, or the current selection in the UE5 editor");
    Info.Parameters = {
        { TEXT("type"),      TEXT("Values: asset|actor|class|property|selection"),                                        TEXT("string"),  true  },
        { TEXT("class"),     TEXT("[asset|actor] Class filter (wildcards supported)"),                                      TEXT("string"),  false },
        { TEXT("path"),      TEXT("[asset] Path/folder filter (wildcards supported)"),                                     TEXT("string"),  false },
        { TEXT("name"),      TEXT("Name filter (wildcards supported)"),                                                    TEXT("string"),  false },
        { TEXT("tag"),       TEXT("[asset] Asset registry tag filter. Format: tag_name=value"),                             TEXT("string"),  false },
        { TEXT("parent"),    TEXT("[class] Parent class name for derived class search"),                                   TEXT("string"),  false },
        { TEXT("target"),    TEXT("[property] Object path to list UProperty names. For BP user variables use inspect type=variables"), TEXT("string"),  false },
        { TEXT("filter"),    TEXT("Post-filter glob/regex on result names"),                                               TEXT("string"),  false },
        { TEXT("recursive"), TEXT("[asset] Search recursively. Default: true"),                                            TEXT("boolean"), false },
        { TEXT("limit"),     TEXT("Max results. Default: 100"),                                                            TEXT("integer"), false },
        { TEXT("help"),      TEXT("Pass help=true for overview, help='type_name' for detailed parameter info"), TEXT("string"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Find::Execute(const TSharedPtr<FJsonObject>& Params)
{
    FMCPToolResult HelpResult;
    if (MCPToolHelp::CheckAndHandleHelp(Params, sFindHelp, HelpResult))
        return HelpResult;

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
