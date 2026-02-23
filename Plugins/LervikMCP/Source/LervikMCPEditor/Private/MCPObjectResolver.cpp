#include "MCPObjectResolver.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Misc/PackageName.h"

UObject* FMCPObjectResolver::ResolveObject(const FString& Target, FString& OutError)
{
    // 1. "selected" keyword
    if (Target.Equals(TEXT("selected"), ESearchCase::IgnoreCase))
    {
        if (!GEditor)
        {
            OutError = TEXT("Editor not available");
            return nullptr;
        }
        UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
        if (!ActorSubsystem)
        {
            OutError = TEXT("EditorActorSubsystem not available");
            return nullptr;
        }
        TArray<AActor*> Selected = ActorSubsystem->GetSelectedLevelActors();
        if (Selected.Num() == 0 || !Selected[0])
        {
            OutError = TEXT("No actor selected");
            return nullptr;
        }
        return Selected[0];
    }

    // 2. Actor.ComponentName (no leading '/')
    if (!Target.StartsWith(TEXT("/")) && Target.Contains(TEXT(".")))
    {
        int32 DotIndex;
        Target.FindChar(TEXT('.'), DotIndex);
        FString ActorName = Target.Left(DotIndex);
        FString ComponentName = Target.Mid(DotIndex + 1);

        FString ActorError;
        AActor* Actor = ResolveActor(ActorName, ActorError);
        if (!Actor)
        {
            OutError = ActorError;
            return nullptr;
        }

        TArray<UActorComponent*> Components;
        Actor->GetComponents(Components);
        for (UActorComponent* Comp : Components)
        {
            if (Comp && Comp->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
            {
                return Comp;
            }
        }

        OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorName);
        return nullptr;
    }

    // 3. Path (starts with '/') - try asset first, then actor fallback
    if (Target.StartsWith(TEXT("/")))
    {
        FString AssetError;
        UObject* Asset = ResolveAsset(Target, AssetError);
        if (Asset)
        {
            return Asset;
        }

        FString ActorError;
        AActor* Actor = ResolveActor(Target, ActorError);
        if (Actor)
        {
            return Actor;
        }

        OutError = FString::Printf(TEXT("No asset or actor found at path '%s'"), *Target);
        return nullptr;
    }

    // 4. Actor by label
    return ResolveActor(Target, OutError);
}

AActor* FMCPObjectResolver::ResolveActor(const FString& Target, FString& OutError)
{
    if (!GEditor)
    {
        OutError = TEXT("Editor not available");
        return nullptr;
    }

    UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    if (!ActorSubsystem)
    {
        OutError = TEXT("EditorActorSubsystem not available");
        return nullptr;
    }

    TArray<AActor*> AllActors = ActorSubsystem->GetAllLevelActors();

    // First pass: match by label (case-insensitive)
    for (AActor* Actor : AllActors)
    {
        if (!Actor)
        {
            continue;
        }
        if (Actor->GetActorLabel().Equals(Target, ESearchCase::IgnoreCase))
        {
            return Actor;
        }
    }

    // Second pass: match by internal name
    for (AActor* Actor : AllActors)
    {
        if (!Actor)
        {
            continue;
        }
        if (Actor->GetName().Equals(Target, ESearchCase::IgnoreCase))
        {
            return Actor;
        }
    }

    OutError = FString::Printf(TEXT("Actor '%s' not found"), *Target);
    return nullptr;
}

UObject* FMCPObjectResolver::ResolveAsset(const FString& Target, FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("Editor not available");
		return nullptr;
	}

	UEditorAssetSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
	if (!AssetSubsystem)
	{
		OutError = TEXT("EditorAssetSubsystem not available");
		return nullptr;
	}

	UObject* Asset = nullptr;
	// Skip LoadAsset for in-memory packages — it logs warnings for paths outside the asset registry
	if (!Target.StartsWith(TEXT("/Temp/")))
	{
		Asset = AssetSubsystem->LoadAsset(Target);
	}
	if (!Asset)
	{
		// Fallback: find in-memory objects by full object path
		// Try "PackagePath.ObjectName" form first (e.g. from GetPathName())
		Asset = StaticFindObject(UObject::StaticClass(), nullptr, *Target);
		// If that found a Package, try the standard "Package.AssetName" convention
		if (!Asset || Asset->IsA<UPackage>())
		{
			FString PackagePath = Target;
			FString ObjectName;
			if (Target.Contains(TEXT(".")))
			{
				Target.Split(TEXT("."), &PackagePath, &ObjectName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			}
			else
			{
				ObjectName = FPackageName::GetShortName(Target);
			}
			UPackage* Pkg = Cast<UPackage>(Asset);
			if (!Pkg)
			{
				Pkg = FindObject<UPackage>(nullptr, *PackagePath);
			}
			if (Pkg)
			{
				Asset = StaticFindObject(UObject::StaticClass(), Pkg, *ObjectName);
			}
		}
	}
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Asset '%s' not found"), *Target);
	}
	return Asset;
}
