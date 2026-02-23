#pragma once

#include "Misc/AutomationTest.h"
#include "IMCPTool.h"
#include "MCPTypes.h"
#include "Features/IModularFeatures.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MaterialEditingLibrary.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "UObject/GarbageCollection.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"

class FMCPToolDirectTestHelper
{
public:
	FMCPToolDirectTestHelper() = default;

	void Setup(FAutomationTestBase* InTest)
	{
		Test = InTest;
		CreatedObjects.Reset();
	}

	void Cleanup()
	{
		if (GEditor)
		{
			UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			if (ActorSub)
			{
				for (AActor* Actor : SpawnedActors)
				{
					if (IsValid(Actor))
					{
						ActorSub->DestroyActor(Actor);
					}
				}
			}
		}
		SpawnedActors.Reset();
		CreatedObjects.Reset();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	~FMCPToolDirectTestHelper() { Cleanup(); }

	// ── Tool lookup ──────────────────────────────────────────────────────────

	static IMCPTool* FindTool(const FString& ToolName)
	{
		TArray<IMCPTool*> Tools = IModularFeatures::Get()
			.GetModularFeatureImplementations<IMCPTool>(IMCPTool::GetModularFeatureName());
		for (IMCPTool* Tool : Tools)
		{
			if (Tool->GetToolInfo().Name == ToolName)
			{
				return Tool;
			}
		}
		return nullptr;
	}

	// ── Param builder ────────────────────────────────────────────────────────

	static TSharedPtr<FJsonObject> MakeParams(const TMap<FString, FString>& StringFields)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		for (const auto& Pair : StringFields)
		{
			Obj->SetStringField(Pair.Key, Pair.Value);
		}
		return Obj;
	}

	static TSharedPtr<FJsonObject> MakeParamsFromJson(const FString& JsonString)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	// ── Result parsing ───────────────────────────────────────────────────────

	static TSharedPtr<FJsonObject> ParseResultJson(const FMCPToolResult& Result)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Content);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	/** Get the asset-style path for a UObject (Package.ObjectName format for ResolveAsset). */
	static FString GetAssetPath(UObject* Obj)
	{
		if (!Obj) return TEXT("");
		return Obj->GetPackage()->GetName();
	}

	// ── Transient asset creation ─────────────────────────────────────────────

	UMaterial* CreateTransientMaterial(const FString& Name)
	{
		UPackage* Pkg = NewObject<UPackage>(nullptr,
			*FString::Printf(TEXT("/Temp/MCPTest/%s"), *Name), RF_Transient);
		Pkg->SetPackageFlags(PKG_InMemoryOnly);

		UMaterial* Mat = NewObject<UMaterial>(Pkg, *Name, RF_Transient | RF_Public);
		if (Mat)
		{
			CreatedObjects.Add(Pkg);
			CreatedObjects.Add(Mat);
		}
		return Mat;
	}

	UMaterialExpression* AddMaterialExpression(UMaterial* Material, UClass* ExprClass, int32 PosX = 0, int32 PosY = 0)
	{
		UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, PosX, PosY);
		if (Expr && !Expr->MaterialExpressionGuid.IsValid())
		{
			Expr->MaterialExpressionGuid = FGuid::NewGuid();
		}
		return Expr;
	}

	AActor* SpawnTransientActor(TSubclassOf<AActor> ActorClass, const FVector& Location = FVector::ZeroVector)
	{
		if (!GEditor) return nullptr;
		UEditorActorSubsystem* ActorSub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!ActorSub) return nullptr;
		AActor* Actor = ActorSub->SpawnActorFromClass(ActorClass, Location);
		if (Actor)
		{
			SpawnedActors.Add(Actor);
		}
		return Actor;
	}

	UBlueprint* CreateTransientBlueprint(const FString& Name)
	{
		UPackage* Pkg = NewObject<UPackage>(nullptr,
			*FString::Printf(TEXT("/Temp/MCPTest/%s"), *Name), RF_Transient);
		Pkg->SetPackageFlags(PKG_InMemoryOnly);

		UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Pkg,
			*Name,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass()
		);

		if (BP)
		{
			CreatedObjects.Add(Pkg);
			CreatedObjects.Add(BP);
		}
		return BP;
	}

private:
	FAutomationTestBase* Test = nullptr;
	TArray<UObject*> CreatedObjects;
	TArray<AActor*> SpawnedActors;
};
