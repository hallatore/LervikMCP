#pragma once

#include "CoreMinimal.h"

/**
 * Validates Python code to ensure it only uses the Unreal Engine Python API.
 * Blocks dangerous builtins, file I/O, network, subprocess, introspection, and obfuscation patterns.
 */
class LERVIKMCP_API FMCPPythonValidator
{
public:
	/** Returns true if Code is safe to execute. On failure, OutError describes the violation. */
	static bool Validate(const FString& Code, FString& OutError);
};
