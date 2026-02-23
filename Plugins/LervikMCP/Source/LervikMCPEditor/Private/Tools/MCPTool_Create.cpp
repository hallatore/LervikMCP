#include "Tools/MCPTool_Create.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPObjectResolver.h"
#include "MCPPropertyHelpers.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "GameFramework/Actor.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "LervikMCP"

FMCPToolInfo FMCPTool_Create::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("create");
    Info.Description = TEXT("Create assets or spawn actors in the level");
    Info.Parameters  = {
        { TEXT("type"),         TEXT("Values: asset|actor"),                                                                    TEXT("string"), true  },
        { TEXT("class"),        TEXT("[asset] Values: Blueprint|Material|MaterialInstanceConstant. Required when no template. [actor] Actor class e.g. PointLight, CameraActor"), TEXT("string"), false },
        { TEXT("name"),         TEXT("Asset name or actor label"),                                                              TEXT("string"), true  },
        { TEXT("path"),         TEXT("[asset] Package path. Default: /Game. Example: /Game/Materials"),                         TEXT("string"), false },
        { TEXT("parent_class"), TEXT("[asset] Parent class for Blueprint. Default: Actor. Values: Actor|Character|Pawn or any UClass name"), TEXT("string"), false },
        { TEXT("location"),     TEXT("[actor] World position. Format: [x,y,z]"),                                                TEXT("array|object"),  false, TEXT("number") },
        { TEXT("rotation"),     TEXT("[actor] World rotation. Format: [pitch,yaw,roll]"),                                       TEXT("array|object"),  false, TEXT("number") },
        { TEXT("template"),     TEXT("Source asset/actor path to duplicate. [actor] location/rotation/scale override the duplicate's transform"), TEXT("string"), false },
        { TEXT("scale"),        TEXT("[actor] World scale. Format: [x,y,z]"),                                                   TEXT("array|object"),  false, TEXT("number") },
        { TEXT("properties"),   TEXT("UProperty values. Format: {\"PropName\":value}. Nested: {\"ComponentName\":{\"Prop\":value}}"), TEXT("object"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Create::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString Type;
        if (!Params->TryGetStringField(TEXT("type"), Type))
        {
            return FMCPToolResult::Error(TEXT("'type' is required"));
        }

        FString ClassName, Name, Path, ParentClassName, TemplateStr;
        Params->TryGetStringField(TEXT("class"),        ClassName);
        Params->TryGetStringField(TEXT("name"),         Name);
        Params->TryGetStringField(TEXT("path"),         Path);
        Params->TryGetStringField(TEXT("parent_class"), ParentClassName);
        Params->TryGetStringField(TEXT("template"),     TemplateStr);

        if (Name.IsEmpty())
        {
            return FMCPToolResult::Error(TEXT("'name' is required"));
        }

        if (!GEditor)
        {
            return FMCPToolResult::Error(TEXT("Editor not available"));
        }

        // ── type=asset ───────────────────────────────────────────────────────
        if (Type.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
        {
            // Duplicate from template
            if (!TemplateStr.IsEmpty())
            {
                UEditorAssetSubsystem* AssetSub = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
                if (!AssetSub)
                {
                    return FMCPToolResult::Error(TEXT("EditorAssetSubsystem not available"));
                }

                FString DestPath = Path.IsEmpty()
                    ? FString::Printf(TEXT("/Game/%s"), *Name)
                    : FString::Printf(TEXT("%s/%s"), *Path, *Name);

                UObject* Duplicated = AssetSub->DuplicateAsset(TemplateStr, DestPath);
                if (!Duplicated)
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *TemplateStr, *DestPath));
                }

                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                Result->SetStringField(TEXT("path"),  Duplicated->GetOutermost()->GetName());
                Result->SetStringField(TEXT("class"), Duplicated->GetClass()->GetName());
                Result->SetStringField(TEXT("name"),  Duplicated->GetName());
                return FMCPJsonHelpers::SuccessResponse(Result);
            }

            if (ClassName.IsEmpty())
            {
                return FMCPToolResult::Error(TEXT("'class' is required for asset creation without a template"));
            }

            IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
            UFactory* Factory   = nullptr;
            UClass*   AssetClass = nullptr;

            if (ClassName.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase))
            {
                UBlueprintFactory* BPFactory = NewObject<UBlueprintFactory>();
                UClass* ResolvedParent = AActor::StaticClass();
                if (!ParentClassName.IsEmpty())
                {
                    UClass* Found = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                    if (Found)
                    {
                        ResolvedParent = Found;
                    }
                }
                BPFactory->ParentClass = ResolvedParent;
                Factory    = BPFactory;
                AssetClass = UBlueprint::StaticClass();
            }
            else if (ClassName.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
            {
                Factory    = NewObject<UMaterialFactoryNew>();
                AssetClass = UMaterial::StaticClass();
            }
            else if (ClassName.Equals(TEXT("MaterialInstanceConstant"), ESearchCase::IgnoreCase))
            {
                Factory    = NewObject<UMaterialInstanceConstantFactoryNew>();
                AssetClass = UMaterialInstanceConstant::StaticClass();
            }
            else
            {
                return FMCPToolResult::Error(FString::Printf(
                    TEXT("Unsupported asset class: '%s'. Supported: Blueprint, Material, MaterialInstanceConstant"), *ClassName));
            }

            FString AssetPath = Path.IsEmpty() ? TEXT("/Game") : Path;
            UObject* NewAsset = AssetTools.CreateAsset(Name, AssetPath, AssetClass, Factory);
            if (!NewAsset)
            {
                return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create '%s' asset '%s'"), *ClassName, *Name));
            }

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("path"),  NewAsset->GetOutermost()->GetName());
            Result->SetStringField(TEXT("class"), NewAsset->GetClass()->GetName());
            Result->SetStringField(TEXT("name"),  NewAsset->GetName());
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        // ── type=actor ───────────────────────────────────────────────────────
        if (Type.Equals(TEXT("actor"), ESearchCase::IgnoreCase))
        {
            UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
            if (!ActorSub)
            {
                return FMCPToolResult::Error(TEXT("EditorActorSubsystem not available"));
            }

            FVector  Location = FVector::ZeroVector;
            FRotator Rotation = FRotator::ZeroRotator;
            bool bHasLocation = FMCPJsonHelpers::TryParseVector(Params, TEXT("location"), Location);
            bool bHasRotation = FMCPJsonHelpers::TryParseRotator(Params, TEXT("rotation"), Rotation);

            AActor* NewActor = nullptr;

            if (!TemplateStr.IsEmpty())
            {
                FString ResolveError;
                AActor* TemplateActor = FMCPObjectResolver::ResolveActor(TemplateStr, ResolveError);
                if (!TemplateActor)
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Template actor not found: %s"), *ResolveError));
                }
                NewActor = ActorSub->DuplicateActor(TemplateActor, nullptr, FVector::ZeroVector);
                if (NewActor)
                {
                    if (bHasLocation) NewActor->SetActorLocation(Location);
                    if (bHasRotation) NewActor->SetActorRotation(Rotation);
                }
            }
            else
            {
                if (ClassName.IsEmpty())
                {
                    return FMCPToolResult::Error(TEXT("'class' is required for actor creation without a template"));
                }
                UClass* ActorClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
                if (!ActorClass)
                {
                    return FMCPToolResult::Error(FString::Printf(TEXT("Class '%s' not found"), *ClassName));
                }
                NewActor = ActorSub->SpawnActorFromClass(ActorClass, Location, Rotation);
            }

            if (!NewActor)
            {
                return FMCPToolResult::Error(TEXT("Failed to spawn actor"));
            }

            if (!Name.IsEmpty())
            {
                NewActor->SetActorLabel(Name);
            }

            // Apply scale
            FVector Scale = FVector::OneVector;
            if (FMCPJsonHelpers::TryParseVector(Params, TEXT("scale"), Scale))
            {
                NewActor->SetActorScale3D(Scale);
            }

            // Apply properties
            const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
            TArray<FString> ModifiedProps;
            TArray<FString> Warnings;
            if (Params->TryGetObjectField(TEXT("properties"), PropertiesObj))
            {
                NewActor->PreEditChange(nullptr);
                FMCPPropertyHelpers::ApplyProperties(NewActor, *PropertiesObj, ModifiedProps, Warnings);
                NewActor->PostEditChange();
            }

            FVector Loc = NewActor->GetActorLocation();
            TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
            LocObj->SetField(TEXT("x"), FMCPJsonHelpers::RoundedJsonNumber(Loc.X));
            LocObj->SetField(TEXT("y"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Y));
            LocObj->SetField(TEXT("z"), FMCPJsonHelpers::RoundedJsonNumber(Loc.Z));

            FVector ActorScale = NewActor->GetActorScale3D();
            TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
            ScaleObj->SetField(TEXT("x"), FMCPJsonHelpers::RoundedJsonNumber(ActorScale.X));
            ScaleObj->SetField(TEXT("y"), FMCPJsonHelpers::RoundedJsonNumber(ActorScale.Y));
            ScaleObj->SetField(TEXT("z"), FMCPJsonHelpers::RoundedJsonNumber(ActorScale.Z));

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("name"),     NewActor->GetName());
            Result->SetStringField(TEXT("label"),    NewActor->GetActorLabel());
            Result->SetStringField(TEXT("class"),    NewActor->GetClass()->GetName());
            Result->SetObjectField(TEXT("location"), LocObj);
            Result->SetObjectField(TEXT("scale"),    ScaleObj);
            if (PropertiesObj)
            {
                TArray<TSharedPtr<FJsonValue>> ModArr;
                for (const FString& P : ModifiedProps) ModArr.Add(MakeShared<FJsonValueString>(P));
                Result->SetArrayField(TEXT("modified"), ModArr);
            }
            FMCPJsonHelpers::SetWarningsField(Result, Warnings);
            return FMCPJsonHelpers::SuccessResponse(Result);
        }

        return FMCPToolResult::Error(FString::Printf(TEXT("Unknown type: '%s'. Valid: asset, actor"), *Type));
    });
}

#undef LOCTEXT_NAMESPACE
