#include "Tools/MCPTool_Modify.h"
#include "MCPGameThreadHelper.h"
#include "MCPJsonHelpers.h"
#include "MCPObjectResolver.h"
#include "MCPPropertyHelpers.h"

#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "MCPGraphHelpers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "LervikMCP"

FMCPToolInfo FMCPTool_Modify::GetToolInfo() const
{
    FMCPToolInfo Info;
    Info.Name        = TEXT("modify");
    Info.Description = TEXT("Modify properties and/or transform of an actor or object in the UE5 editor");
    Info.Parameters  = {
        { TEXT("target"),     TEXT("Object path, actor label, 'selected', or 'ActorLabel.ComponentName' to target a specific component on a level actor"),  TEXT("string"), true  },
        { TEXT("properties"), TEXT("Property names to values as {\"PropName\": value}. Use find type=property to discover valid names. Values in UE text import format"),  TEXT("object"), false },
        { TEXT("transform"),  TEXT("Transform override: { \"location\": [x,y,z], \"rotation\": [pitch,yaw,roll], \"scale\": [x,y,z] }"), TEXT("object"), false },
    };
    return Info;
}

FMCPToolResult FMCPTool_Modify::Execute(const TSharedPtr<FJsonObject>& Params)
{
    return ExecuteOnGameThread([Params]() -> FMCPToolResult
    {
        FString TargetParam;
        if (!Params->TryGetStringField(TEXT("target"), TargetParam))
        {
            return FMCPToolResult::Error(TEXT("'target' is required"));
        }

        FString ResolveError;
        UObject* Object = FMCPObjectResolver::ResolveObject(TargetParam, ResolveError);
        if (!Object)
        {
            return FMCPToolResult::Error(ResolveError);
        }

        const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
        const TSharedPtr<FJsonObject>* TransformObj  = nullptr;
        Params->TryGetObjectField(TEXT("properties"), PropertiesObj);
        Params->TryGetObjectField(TEXT("transform"),  TransformObj);

        if (!PropertiesObj && !TransformObj)
        {
            return FMCPToolResult::Error(TEXT("At least one of 'properties' or 'transform' must be provided"));
        }

        TArray<FString> ModifiedProps;
        TArray<FString> Warnings;

        // Resolve material objects to editor preview copy if editor is open
        UObject* OriginalObject = Object;
        UMaterial* OriginalOwningMat = nullptr;

        if (UMaterial* Mat = Cast<UMaterial>(Object))
        {
            Object = FMCPGraphHelpers::ResolveMaterialForEditing(Mat);
        }
        else if (UMaterialExpression* Expr = Cast<UMaterialExpression>(Object))
        {
            UMaterial* OwningMat = Cast<UMaterial>(Expr->GetOuter());
            if (OwningMat)
            {
                UMaterial* EditMat = FMCPGraphHelpers::ResolveMaterialForEditing(OwningMat);
                if (EditMat != OwningMat && Expr->MaterialExpressionGuid.IsValid())
                {
                    OriginalOwningMat = OwningMat;
                    UMaterialExpression* EditExpr = FMCPGraphHelpers::FindExpressionByGuid(
                        EditMat, Expr->MaterialExpressionGuid.ToString());
                    if (EditExpr)
                        Object = EditExpr;
                }
            }
        }

        FScopedTransaction Transaction(LOCTEXT("MCPModify", "MCP Modify Properties"));
        Object->Modify();
        Object->PreEditChange(nullptr);

        // ── properties ───────────────────────────────────────────────────────
        if (PropertiesObj)
        {
            FMCPPropertyHelpers::ApplyProperties(Object, *PropertiesObj, ModifiedProps, Warnings);
        }

        // ── transform ────────────────────────────────────────────────────────
        if (TransformObj)
        {
            AActor* Actor = Cast<AActor>(Object);
            if (!Actor)
            {
                Object->PostEditChange();
                if (UMaterial* Mat = Cast<UMaterial>(OriginalObject))
                    FMCPGraphHelpers::RefreshMaterialEditor(Mat);
                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                TArray<TSharedPtr<FJsonValue>> ModArr;
                for (const FString& P : ModifiedProps) ModArr.Add(MakeShared<FJsonValueString>(P));
                Result->SetArrayField(TEXT("modified"), ModArr);
                Result->SetStringField(TEXT("target"),  OriginalObject->GetPathName());
                Warnings.Add(TEXT("'transform' was provided but target is not an Actor — ignored"));
                FMCPJsonHelpers::SetWarningsField(Result, Warnings);
                return FMCPJsonHelpers::SuccessResponse(Result);
            }

            FVector Loc  = Actor->GetActorLocation();
            FRotator Rot = Actor->GetActorRotation();
            FVector Scale = Actor->GetActorScale3D();

            if (FMCPJsonHelpers::TryParseVector(*TransformObj, TEXT("location"), Loc))
            {
                Actor->SetActorLocation(Loc);
                ModifiedProps.Add(TEXT("Location"));
            }

            if (FMCPJsonHelpers::TryParseRotator(*TransformObj, TEXT("rotation"), Rot))
            {
                Actor->SetActorRotation(Rot);
                ModifiedProps.Add(TEXT("Rotation"));
            }

            if (FMCPJsonHelpers::TryParseVector(*TransformObj, TEXT("scale"), Scale))
            {
                Actor->SetActorScale3D(Scale);
                ModifiedProps.Add(TEXT("Scale3D"));
            }
        }

        Object->PostEditChange();
        if (UMaterial* Mat = Cast<UMaterial>(OriginalObject))
            FMCPGraphHelpers::RefreshMaterialEditor(Mat);
        if (OriginalOwningMat)
            FMCPGraphHelpers::RefreshMaterialEditor(OriginalOwningMat);

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> ModArr;
        for (const FString& P : ModifiedProps) ModArr.Add(MakeShared<FJsonValueString>(P));
        Result->SetArrayField(TEXT("modified"), ModArr);
        Result->SetStringField(TEXT("target"),  OriginalObject->GetPathName());
        FMCPJsonHelpers::SetWarningsField(Result, Warnings);
        return FMCPJsonHelpers::SuccessResponse(Result);
    });
}

#undef LOCTEXT_NAMESPACE
