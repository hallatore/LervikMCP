#pragma once

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionFrac.h"
// UMaterialExpressionRound — no include; symbol not exported in all engine versions
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionNormalize.h"
// UMaterialExpressionSign — no include; symbol not exported in all engine versions
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "MaterialShaderType.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionReroute.h"
#include "MCPGraphHelpers.h"
#include "MCPJsonHelpers.h"

struct FMCPMaterialHLSL
{
	static FString GenerateHLSL(UMaterial* Material)
	{
		if (!Material) return TEXT("// null material");

		FString Out;

		// Header
		Out += FString::Printf(TEXT("// Material: %s\n"), *Material->GetName());
		FString ShadingStr = GetShadingModelFieldString(Material->GetShadingModels());
		FString BlendStr = GetBlendModeString(Material->GetBlendMode());
		Out += FString::Printf(TEXT("// Shading Model: %s | Blend Mode: %s\n"), *ShadingStr, *BlendStr);
		Out += TEXT("// [<ID>] (<pos_x>,<pos_y>) \u2014 each expression has a compact ID and node position\n\n");

		// Collect reachable expressions from all connected material property inputs
		TSet<UMaterialExpression*> Reachable;
		TMap<EMaterialProperty, UMaterialExpression*> PropConnections;
		for (const auto& Entry : FMCPGraphHelpers::KnownMaterialProperties())
		{
			FExpressionInput* PropInput = Material->GetExpressionInputForProperty(Entry.Prop);
			if (PropInput && PropInput->Expression)
			{
				PropConnections.Add(Entry.Prop, PropInput->Expression);
				CollectReachable(PropInput->Expression, Reachable);
			}
		}

		if (Reachable.Num() == 0)
		{
			Out += TEXT("// (no expressions connected)\n");
		}

		// Topological sort
		TArray<UMaterialExpression*> Sorted;
		{
			TSet<UMaterialExpression*> Visited;
			for (UMaterialExpression* Expr : Reachable)
			{
				TopoSort(Expr, Visited, Sorted, Reachable);
			}
		}

		// Build variable name map with collision resolution
		TMap<UMaterialExpression*, FString> VarNames = BuildVarNameMap(Sorted);

		// Separate parameters and expressions
		TArray<UMaterialExpression*> Params;
		TArray<UMaterialExpression*> Exprs;
		for (UMaterialExpression* Expr : Sorted)
		{
			if (IsParameter(Expr))
				Params.Add(Expr);
			else
				Exprs.Add(Expr);
		}

		// Emit parameters
		if (Params.Num() > 0)
		{
			Out += TEXT("// --- Parameters ---\n");
			for (UMaterialExpression* Expr : Params)
			{
				Out += EmitExpression(Expr, VarNames);
				Out += TEXT("\n");
			}
			Out += TEXT("\n");
		}

		// Emit dangling (unconnected) expressions
		TArray<UMaterialExpression*> DanglingExprs;
		for (UMaterialExpression* Expr : Material->GetExpressions())
		{
			if (Expr && !Reachable.Contains(Expr))
				DanglingExprs.Add(Expr);
		}

		if (DanglingExprs.Num() > 0)
		{
			TSet<UMaterialExpression*> DanglingSet(DanglingExprs);

			TArray<UMaterialExpression*> DanglingSorted;
			{
				TSet<UMaterialExpression*> DanglingVisited;
				for (UMaterialExpression* Expr : DanglingExprs)
					TopoSort(Expr, DanglingVisited, DanglingSorted, DanglingSet);
			}

			TMap<UMaterialExpression*, FString> DanglingVarNames = BuildVarNameMap(DanglingSorted);

			// Merge connected VarNames so dangling nodes can reference connected expressions
			DanglingVarNames.Append(VarNames);

			TArray<UMaterialExpression*> DanglingParams, DanglingNonParams;
			for (UMaterialExpression* Expr : DanglingSorted)
			{
				if (IsParameter(Expr))
					DanglingParams.Add(Expr);
				else
					DanglingNonParams.Add(Expr);
			}

			Out += TEXT("\n// --- Dangling (unconnected) ---\n");
			for (UMaterialExpression* Expr : DanglingParams)
			{
				Out += EmitExpression(Expr, DanglingVarNames);
				Out += TEXT("\n");
			}
			for (UMaterialExpression* Expr : DanglingNonParams)
			{
				Out += EmitExpression(Expr, DanglingVarNames);
				Out += TEXT("\n");
			}
		}

		// Emit expressions
		if (Exprs.Num() > 0)
		{
			Out += TEXT("// --- Expressions ---\n");
			for (UMaterialExpression* Expr : Exprs)
			{
				Out += EmitExpression(Expr, VarNames);
				Out += TEXT("\n");
			}
			Out += TEXT("\n");
		}

		// Emit material outputs
		if (PropConnections.Num() > 0)
		{
			Out += TEXT("// --- Material Outputs ---\n");
			for (const auto& Entry : FMCPGraphHelpers::KnownMaterialProperties())
			{
				if (UMaterialExpression** Found = PropConnections.Find(Entry.Prop))
				{
					FString* VN = VarNames.Find(*Found);
					FString Ref = VN ? *VN : TEXT("???");
					Out += FString::Printf(TEXT("%s = %s;\n"), Entry.Name, *Ref);
				}
			}
		}

		return Out;
	}

private:

	// ── Graph traversal ────────────────────────────────────────────────

	static void CollectReachable(UMaterialExpression* Expr, TSet<UMaterialExpression*>& Visited)
	{
		if (!Expr || Visited.Contains(Expr)) return;
		Visited.Add(Expr);

		int32 Count = FMCPGraphHelpers::GetExpressionInputCount(Expr);
		for (int32 i = 0; i < Count; ++i)
		{
			FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, i);
			if (Input && Input->Expression)
				CollectReachable(Input->Expression, Visited);
		}

		// MaterialFunctionCall has FunctionInputs with their own Input connections
		if (auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			for (auto& FI : FuncCall->FunctionInputs)
			{
				if (FI.Input.Expression)
					CollectReachable(FI.Input.Expression, Visited);
			}
		}

		// NamedRerouteUsage links to its Declaration via pointer, not a standard FExpressionInput
		if (auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expr))
			if (Usage->Declaration)
				CollectReachable(Usage->Declaration, Visited);
	}

	static void TopoSort(UMaterialExpression* Expr, TSet<UMaterialExpression*>& Visited,
		TArray<UMaterialExpression*>& Sorted, const TSet<UMaterialExpression*>& Reachable)
	{
		if (!Expr || !Reachable.Contains(Expr) || Visited.Contains(Expr)) return;
		Visited.Add(Expr);

		int32 Count = FMCPGraphHelpers::GetExpressionInputCount(Expr);
		for (int32 i = 0; i < Count; ++i)
		{
			FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, i);
			if (Input && Input->Expression)
				TopoSort(Input->Expression, Visited, Sorted, Reachable);
		}

		if (auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			for (auto& FI : FuncCall->FunctionInputs)
			{
				if (FI.Input.Expression)
					TopoSort(FI.Input.Expression, Visited, Sorted, Reachable);
			}
		}

		// NamedRerouteUsage: ensure Declaration is sorted before this Usage
		if (auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expr))
			if (Usage->Declaration)
				TopoSort(Usage->Declaration, Visited, Sorted, Reachable);

		Sorted.Add(Expr);
	}

	// ── Naming helpers ─────────────────────────────────────────────────

	static FString SanitizeName(const FString& Name)
	{
		FString Result;
		Result.Reserve(Name.Len());
		bool bLastWasUnderscore = true; // true to skip leading underscores
		for (TCHAR Ch : Name)
		{
			if (FChar::IsAlnum(Ch))
			{
				Result.AppendChar(Ch);
				bLastWasUnderscore = false;
			}
			else if (!bLastWasUnderscore)
			{
				Result.AppendChar(TEXT('_'));
				bLastWasUnderscore = true;
			}
		}
		// Strip trailing underscore
		if (Result.Len() > 0 && Result[Result.Len() - 1] == TEXT('_'))
			Result.LeftChopInline(1);
		if (Result.IsEmpty()) Result = TEXT("Unnamed");
		return Result;
	}

	static TMap<UMaterialExpression*, FString> BuildVarNameMap(const TArray<UMaterialExpression*>& Sorted)
	{
		TMap<UMaterialExpression*, FString> VarNames;
		TMap<FString, TArray<UMaterialExpression*>> NameToExprs;
		for (UMaterialExpression* Expr : Sorted)
		{
			FString Name = BuildVarName(Expr);
			VarNames.Add(Expr, Name);
			NameToExprs.FindOrAdd(Name).Add(Expr);
		}
		for (auto& Pair : NameToExprs)
		{
			if (Pair.Value.Num() > 1)
			{
				for (UMaterialExpression* Expr : Pair.Value)
				{
					FString CompactGuid = FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid);
					VarNames[Expr] = FString::Printf(TEXT("%s_%s"), *Pair.Key, *CompactGuid);
				}
			}
		}

		// Second pass: resolve any remaining collisions with numeric suffixes
		// Sort collision groups by topo index for deterministic suffix assignment
		TMap<FString, TArray<UMaterialExpression*>> NameToExprs2;
		for (auto& Pair : VarNames)
		{
			NameToExprs2.FindOrAdd(Pair.Value).Add(Pair.Key);
		}
		for (auto& Pair : NameToExprs2)
		{
			if (Pair.Value.Num() > 1)
			{
				Pair.Value.Sort([&Sorted](UMaterialExpression& A, UMaterialExpression& B)
				{
					return Sorted.IndexOfByKey(&A) < Sorted.IndexOfByKey(&B);
				});
				int32 Suffix = 0;
				for (UMaterialExpression* Expr : Pair.Value)
				{
					VarNames[Expr] = FString::Printf(TEXT("%s_%d"), *Pair.Key, Suffix++);
				}
			}
		}

		return VarNames;
	}

	static bool IsStaticSwitchParameter(UMaterialExpression* Expr)
	{
		return Cast<UMaterialExpressionStaticSwitchParameter>(Expr) != nullptr;
	}

	static bool IsParameter(UMaterialExpression* Expr)
	{
		if (IsStaticSwitchParameter(Expr)) return false;
		return Cast<UMaterialExpressionParameter>(Expr) != nullptr
			|| Cast<UMaterialExpressionTextureSampleParameter>(Expr) != nullptr;
	}

	static bool HasParameterName(UMaterialExpression* Expr)
	{
		return IsParameter(Expr) || IsStaticSwitchParameter(Expr);
	}

	static FString GetParameterName(UMaterialExpression* Expr)
	{
		if (auto* P = Cast<UMaterialExpressionParameter>(Expr))
			return P->ParameterName.ToString();
		if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
			return TP->ParameterName.ToString();
		return TEXT("");
	}

	static FString BuildVarName(UMaterialExpression* Expr)
	{
		if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
			return FString::Printf(TEXT("Switch_%s"), *SanitizeName(SSP->ParameterName.ToString()));
		if (IsParameter(Expr))
		{
			FString PName = GetParameterName(Expr);
			return FString::Printf(TEXT("Param_%s"), *SanitizeName(PName));
		}
		if (auto* Decl = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expr))
			return FString::Printf(TEXT("Reroute_%s"), *SanitizeName(Decl->Name.ToString()));
		if (Cast<UMaterialExpressionReroute>(Expr))
		{
			FString CompactGuid = FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid);
			return FString::Printf(TEXT("Wire_%s"), *CompactGuid);
		}
		FString ClassName = CompactClassName(Expr);
		FString CompactGuid = FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid);
		return FString::Printf(TEXT("%s_%s"), *ClassName, *CompactGuid);
	}

	static FString CompactClassName(UMaterialExpression* Expr)
	{
		FString Name = Expr->GetClass()->GetName();
		Name.ReplaceInline(TEXT("MaterialExpression"), TEXT(""));
		return Name;
	}

	// ── Comment / annotation ───────────────────────────────────────────

	static FString TrailingComment(UMaterialExpression* Expr)
	{
		FString GUID = FMCPJsonHelpers::GuidToCompact(Expr->MaterialExpressionGuid);
		int32 X = Expr->MaterialExpressionEditorX;
		int32 Y = Expr->MaterialExpressionEditorY;
		FString Desc = Expr->Desc;
		if (HasParameterName(Expr))
			Desc = GetParameterName(Expr);

		if (Desc.IsEmpty())
			return FString::Printf(TEXT("// [%s] (%d,%d)"), *GUID, X, Y);
		return FString::Printf(TEXT("// [%s] (%d,%d) \"%s\""), *GUID, X, Y, *Desc);
	}

	// ── Input reference helpers ────────────────────────────────────────

	static FString GetInputPinName(UMaterialExpression* Expr, int32 InputIndex)
	{
		FName PinName = Expr->GetInputName(InputIndex);
		if (PinName.IsNone()) return FString();
		FString Name = PinName.ToString();
		int32 ParenIdx;
		if (Name.FindChar(TEXT('('), ParenIdx))
			Name = Name.Left(ParenIdx).TrimEnd();
		return Name;
	}

	static FString BuildInputList(UMaterialExpression* Expr,
		const TMap<UMaterialExpression*, FString>& VarNames, const FString& DefaultValue = TEXT("null"))
	{
		FString InputList;
		int32 Count = FMCPGraphHelpers::GetExpressionInputCount(Expr);
		for (int32 i = 0; i < Count; ++i)
		{
			if (i > 0) InputList += TEXT(", ");
			FString PinName = GetInputPinName(Expr, i);
			if (!PinName.IsEmpty())
				InputList += PinName + TEXT(": ");
			InputList += InputRef(Expr, i, VarNames, DefaultValue);
		}
		return InputList;
	}

	static FString InputRef(UMaterialExpression* Expr, int32 InputIndex,
		const TMap<UMaterialExpression*, FString>& VarNames, const FString& DefaultValue = TEXT("0"))
	{
		FExpressionInput* Input = FMCPGraphHelpers::GetExpressionInput(Expr, InputIndex);
		if (!Input || !Input->Expression)
			return DefaultValue;

		const FString* VN = VarNames.Find(Input->Expression);
		if (!VN) return DefaultValue;

		// Check if using a specific output channel
		if (Input->OutputIndex > 0)
		{
			TArray<FExpressionOutput>& Outputs = Input->Expression->GetOutputs();
			if (Outputs.IsValidIndex(Input->OutputIndex))
			{
				FString PinName = FMCPGraphHelpers::ExprOutputPinName(Outputs[Input->OutputIndex]);
				if (PinName.Len() == 1) // R, G, B, A → swizzle
					return *VN + TEXT(".") + PinName.ToLower();
				if (!PinName.IsEmpty())
					return *VN + TEXT(".") + PinName;
			}
		}
		return *VN;
	}

	static FString Fmt(float V)
	{
		// Clean float formatting: strip trailing zeros
		FString S = FString::SanitizeFloat(V);
		return S;
	}

	static FString FmtColor(const FLinearColor& C, int32 Components)
	{
		if (Components == 3)
			return FString::Printf(TEXT("float3(%s, %s, %s)"), *Fmt(C.R), *Fmt(C.G), *Fmt(C.B));
		return FString::Printf(TEXT("float4(%s, %s, %s, %s)"), *Fmt(C.R), *Fmt(C.G), *Fmt(C.B), *Fmt(C.A));
	}

	// ── Per-expression HLSL emission ───────────────────────────────────

	static FString EmitExpression(UMaterialExpression* Expr, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		const FString VN = VarNames.FindRef(Expr);
		const FString Comment = TrailingComment(Expr);

		// ── Constants ──
		if (auto* C = Cast<UMaterialExpressionConstant>(Expr))
			return FString::Printf(TEXT("float %s = %s; %s"), *VN, *Fmt(C->R), *Comment);

		if (auto* C2 = Cast<UMaterialExpressionConstant2Vector>(Expr))
			return FString::Printf(TEXT("float2 %s = float2(%s, %s); %s"), *VN, *Fmt(C2->R), *Fmt(C2->G), *Comment);

		if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
			return FString::Printf(TEXT("float3 %s = float3(%s, %s, %s); %s"), *VN, *Fmt(C3->Constant.R), *Fmt(C3->Constant.G), *Fmt(C3->Constant.B), *Comment);

		if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
			return FString::Printf(TEXT("float4 %s = %s; %s"), *VN, *FmtColor(C4->Constant, 4), *Comment);

		// ── Parameters ──
		if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
			return FString::Printf(TEXT("float %s = %s; %s"), *VN, *Fmt(SP->DefaultValue), *Comment);

		if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
			return FString::Printf(TEXT("float4 %s = %s; %s"), *VN, *FmtColor(VP->DefaultValue, 4), *Comment);

		if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
		{
			FString TrueVal = InputRef(Expr, 0, VarNames, TEXT("0"));
			FString FalseVal = InputRef(Expr, 1, VarNames, TEXT("0"));
			return FString::Printf(TEXT("auto %s = %s ? %s : %s; %s"), *VN,
				SSP->DefaultValue ? TEXT("true") : TEXT("false"), *TrueVal, *FalseVal, *Comment);
		}

		if (auto* SBP = Cast<UMaterialExpressionStaticBoolParameter>(Expr))
			return FString::Printf(TEXT("bool %s = %s; %s"), *VN, SBP->DefaultValue ? TEXT("true") : TEXT("false"), *Comment);

		// TextureSampleParameter2D (must check before TextureSample since it inherits)
		if (auto* TSP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
			return EmitTextureSample(TSP, VN, Comment, VarNames);

		// ── Texture ──
		if (auto* TS = Cast<UMaterialExpressionTextureSample>(Expr))
			return EmitTextureSample(TS, VN, Comment, VarNames);

		if (auto* TC = Cast<UMaterialExpressionTextureCoordinate>(Expr))
			return FString::Printf(TEXT("float2 %s = TexCoord[%d]; %s"), *VN, TC->CoordinateIndex, *Comment);

		if (auto* TO = Cast<UMaterialExpressionTextureObject>(Expr))
		{
			FString Path = TO->Texture ? TO->Texture->GetPathName() : TEXT("None");
			return FString::Printf(TEXT("Texture2D %s = Texture2D'%s'; %s"), *VN, *Path, *Comment);
		}

		// ── Binary ops ──
		if (Cast<UMaterialExpressionAdd>(Expr))
			return EmitBinaryOp(Expr, VN, Comment, TEXT("+"), VarNames);
		if (Cast<UMaterialExpressionSubtract>(Expr))
			return EmitBinaryOp(Expr, VN, Comment, TEXT("-"), VarNames);
		if (Cast<UMaterialExpressionMultiply>(Expr))
			return EmitBinaryOp(Expr, VN, Comment, TEXT("*"), VarNames);
		if (Cast<UMaterialExpressionDivide>(Expr))
			return EmitBinaryOp(Expr, VN, Comment, TEXT("/"), VarNames);

		if (Cast<UMaterialExpressionDotProduct>(Expr))
			return EmitBinaryFunc(Expr, VN, Comment, TEXT("dot"), VarNames);
		if (Cast<UMaterialExpressionCrossProduct>(Expr))
		{
			FString A = InputRef(Expr, 0, VarNames);
			FString B = InputRef(Expr, 1, VarNames);
			return FString::Printf(TEXT("float3 %s = cross(%s, %s); %s"), *VN, *A, *B, *Comment);
		}

		// ── Power ──
		if (auto* Pow = Cast<UMaterialExpressionPower>(Expr))
		{
			FString Base = InputRef(Expr, 0, VarNames, TEXT("0"));
			FString Exp = InputRef(Expr, 1, VarNames, Fmt(Pow->ConstExponent));
			return FString::Printf(TEXT("auto %s = pow(%s, %s); %s"), *VN, *Base, *Exp, *Comment);
		}

		// ── Ternary ──
		if (auto* Lerp = Cast<UMaterialExpressionLinearInterpolate>(Expr))
		{
			FString A = InputRef(Expr, 0, VarNames, Fmt(Lerp->ConstA));
			FString B = InputRef(Expr, 1, VarNames, Fmt(Lerp->ConstB));
			FString Alpha = InputRef(Expr, 2, VarNames, Fmt(Lerp->ConstAlpha));
			return FString::Printf(TEXT("auto %s = lerp(%s, %s, %s); %s"), *VN, *A, *B, *Alpha, *Comment);
		}

		if (auto* Cl = Cast<UMaterialExpressionClamp>(Expr))
		{
			FString Input = InputRef(Expr, 0, VarNames, TEXT("0"));
			FString Min = InputRef(Expr, 1, VarNames, Fmt(Cl->MinDefault));
			FString Max = InputRef(Expr, 2, VarNames, Fmt(Cl->MaxDefault));
			return FString::Printf(TEXT("auto %s = clamp(%s, %s, %s); %s"), *VN, *Input, *Min, *Max, *Comment);
		}

		if (Cast<UMaterialExpressionIf>(Expr))
		{
			FString A = InputRef(Expr, 0, VarNames, TEXT("0"));
			FString B = InputRef(Expr, 1, VarNames, TEXT("0"));
			FString AgtB = InputRef(Expr, 2, VarNames, TEXT("0"));
			FString AeqB = InputRef(Expr, 3, VarNames, FString());
			FString AltB = InputRef(Expr, 4, VarNames, TEXT("0"));
			if (AeqB.IsEmpty()) AeqB = AgtB;
			return FString::Printf(TEXT("auto %s = (%s > %s) ? %s : (%s == %s) ? %s : %s; %s"), *VN, *A, *B, *AgtB, *A, *B, *AeqB, *AltB, *Comment);
		}

		// ── Unary ──
		if (Cast<UMaterialExpressionOneMinus>(Expr))
		{
			FString I = InputRef(Expr, 0, VarNames);
			return FString::Printf(TEXT("auto %s = 1.0 - %s; %s"), *VN, *I, *Comment);
		}
		if (Cast<UMaterialExpressionAbs>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("abs"), VarNames);
		if (Cast<UMaterialExpressionSaturate>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("saturate"), VarNames);
		if (Cast<UMaterialExpressionFloor>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("floor"), VarNames);
		if (Cast<UMaterialExpressionCeil>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("ceil"), VarNames);
		if (Cast<UMaterialExpressionFrac>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("frac"), VarNames);
		if (Expr->GetClass()->GetFName() == TEXT("MaterialExpressionRound"))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("round"), VarNames);
		if (Cast<UMaterialExpressionSquareRoot>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("sqrt"), VarNames);
		if (Cast<UMaterialExpressionNormalize>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("normalize"), VarNames);
		if (Expr->GetClass()->GetFName() == TEXT("MaterialExpressionSign"))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("sign"), VarNames);
		if (Cast<UMaterialExpressionSine>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("sin"), VarNames);
		if (Cast<UMaterialExpressionCosine>(Expr))
			return EmitUnaryFunc(Expr, VN, Comment, TEXT("cos"), VarNames);

		// ── Component/Vector ops ──
		if (auto* CM = Cast<UMaterialExpressionComponentMask>(Expr))
		{
			FString I = InputRef(Expr, 0, VarNames);
			FString Mask;
			if (CM->R) Mask.AppendChar(TEXT('r'));
			if (CM->G) Mask.AppendChar(TEXT('g'));
			if (CM->B) Mask.AppendChar(TEXT('b'));
			if (CM->A) Mask.AppendChar(TEXT('a'));
			return FString::Printf(TEXT("auto %s = %s.%s; %s"), *VN, *I, *Mask, *Comment);
		}

		if (Cast<UMaterialExpressionAppendVector>(Expr))
		{
			FString A = InputRef(Expr, 0, VarNames);
			FString B = InputRef(Expr, 1, VarNames);
			return FString::Printf(TEXT("auto %s = append(%s, %s); %s"), *VN, *A, *B, *Comment);
		}

		// ── Switch ──
		if (auto* SS = Cast<UMaterialExpressionStaticSwitch>(Expr))
		{
			FString TrueVal = InputRef(Expr, 0, VarNames, TEXT("0"));
			FString FalseVal = InputRef(Expr, 1, VarNames, TEXT("0"));
			FString Value = InputRef(Expr, 2, VarNames, SS->DefaultValue ? TEXT("true") : TEXT("false"));
			return FString::Printf(TEXT("auto %s = %s ? %s : %s; %s"), *VN, *Value, *TrueVal, *FalseVal, *Comment);
		}

		// ── World/Engine ──
		if (Cast<UMaterialExpressionTime>(Expr))
			return FString::Printf(TEXT("float %s = Time; %s"), *VN, *Comment);
		if (Cast<UMaterialExpressionWorldPosition>(Expr))
			return FString::Printf(TEXT("float3 %s = WorldPosition; %s"), *VN, *Comment);
		if (Cast<UMaterialExpressionVertexNormalWS>(Expr))
			return FString::Printf(TEXT("float3 %s = VertexNormalWS; %s"), *VN, *Comment);
		if (Cast<UMaterialExpressionPixelNormalWS>(Expr))
			return FString::Printf(TEXT("float3 %s = PixelNormalWS; %s"), *VN, *Comment);
		if (Cast<UMaterialExpressionCameraPositionWS>(Expr))
			return FString::Printf(TEXT("float3 %s = CameraPositionWS; %s"), *VN, *Comment);

		// ── Custom ──
		if (auto* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			FString InputList = BuildInputList(Expr, VarNames, TEXT("0"));
			FString Desc = Custom->Description.IsEmpty() ? TEXT("Custom") : Custom->Description;
			// Show the code inline (first line only if multiline, to keep compact)
			FString CodePreview = Custom->Code.Replace(TEXT("\n"), TEXT(" ")).Left(120);
			return FString::Printf(TEXT("/* Custom: %s, inputs: %s */\nauto %s = /* %s */; %s"),
				*Desc, *InputList, *VN, *CodePreview, *Comment);
		}

		// ── MaterialFunctionCall ──
		if (auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			FString FuncName = FuncCall->MaterialFunction
				? FuncCall->MaterialFunction->GetName() : TEXT("Unknown");
			FString InputList = BuildInputList(Expr, VarNames);
			if (InputList.IsEmpty())
				return FString::Printf(TEXT("auto %s = FunctionCall(\"%s\"); %s"),
					*VN, *FuncName, *Comment);
			return FString::Printf(TEXT("auto %s = FunctionCall(\"%s\", %s); %s"),
				*VN, *FuncName, *InputList, *Comment);
		}

		// ── Plain Reroute (passthrough wire) ──
		if (Cast<UMaterialExpressionReroute>(Expr))
		{
			FString Input = InputRef(Expr, 0, VarNames, TEXT("null"));
			return FString::Printf(TEXT("auto %s = %s; %s"), *VN, *Input, *Comment);
		}

		// ── Named Reroute ──
		if (auto* Decl = Cast<UMaterialExpressionNamedRerouteDeclaration>(Expr))
		{
			FString Input = InputRef(Expr, 0, VarNames, TEXT("0"));
			return FString::Printf(TEXT("auto %s = %s; %s | decl: \"%s\""),
				*VN, *Input, *Comment, *Decl->Name.ToString());
		}

		if (auto* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expr))
		{
			FString DeclRef = TEXT("???");
			FString RerouteName;
			if (Usage->Declaration)
			{
				const FString* DeclVN = VarNames.Find(Usage->Declaration);
				DeclRef = DeclVN ? *DeclVN : TEXT("???");
				RerouteName = Usage->Declaration->Name.ToString();
			}
			return FString::Printf(TEXT("auto %s = %s; %s | reroute: \"%s\""),
				*VN, *DeclRef, *Comment, *RerouteName);
		}

		// ── Fallback ──
		return EmitFallback(Expr, VN, Comment, VarNames);
	}

	// ── Emission helpers ───────────────────────────────────────────────

	static FString EmitTextureSample(UMaterialExpressionTextureSample* TS, const FString& VN,
		const FString& Comment, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		FString TexRef = TS->Texture ? TS->Texture->GetPathName() : TEXT("None");
		FString UV = InputRef(TS, 0, VarNames, TEXT("TexCoord[0]"));
		return FString::Printf(TEXT("float4 %s = Texture2DSample(%s, %s); %s"), *VN, *TexRef, *UV, *Comment);
	}

	static FString EmitBinaryOp(UMaterialExpression* Expr, const FString& VN,
		const FString& Comment, const TCHAR* Op, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		// Binary ops have ConstA/ConstB defaults; try to read them
		float ConstA = 0.f, ConstB = 0.f;
		if (auto* Add = Cast<UMaterialExpressionAdd>(Expr)) { ConstA = Add->ConstA; ConstB = Add->ConstB; }
		else if (auto* Sub = Cast<UMaterialExpressionSubtract>(Expr)) { ConstA = Sub->ConstA; ConstB = Sub->ConstB; }
		else if (auto* Mul = Cast<UMaterialExpressionMultiply>(Expr)) { ConstA = Mul->ConstA; ConstB = Mul->ConstB; }
		else if (auto* Div = Cast<UMaterialExpressionDivide>(Expr)) { ConstA = Div->ConstA; ConstB = Div->ConstB; }

		FString A = InputRef(Expr, 0, VarNames, Fmt(ConstA));
		FString B = InputRef(Expr, 1, VarNames, Fmt(ConstB));
		return FString::Printf(TEXT("auto %s = %s %s %s; %s"), *VN, *A, Op, *B, *Comment);
	}

	static FString EmitBinaryFunc(UMaterialExpression* Expr, const FString& VN,
		const FString& Comment, const TCHAR* Func, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		FString A = InputRef(Expr, 0, VarNames);
		FString B = InputRef(Expr, 1, VarNames);
		return FString::Printf(TEXT("float %s = %s(%s, %s); %s"), *VN, Func, *A, *B, *Comment);
	}

	static FString EmitUnaryFunc(UMaterialExpression* Expr, const FString& VN,
		const FString& Comment, const TCHAR* Func, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		FString I = InputRef(Expr, 0, VarNames);
		return FString::Printf(TEXT("auto %s = %s(%s); %s"), *VN, Func, *I, *Comment);
	}

	static FString EmitFallback(UMaterialExpression* Expr, const FString& VN,
		const FString& Comment, const TMap<UMaterialExpression*, FString>& VarNames)
	{
		FString ClassName = CompactClassName(Expr);
		FString InputList = BuildInputList(Expr, VarNames);
		return FString::Printf(TEXT("auto %s = %s(%s); %s"), *VN, *ClassName, *InputList, *Comment);
	}
};
