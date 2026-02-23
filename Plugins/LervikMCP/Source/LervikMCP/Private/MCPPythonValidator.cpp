#include "MCPPythonValidator.h"
#include "Internationalization/Regex.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarMCPPythonHardening(
	TEXT("mcp.python.hardening"),
	2,
	TEXT("Python script validation level for MCP execute_python tool.\n")
	TEXT("0 = None (no validation)\n")
	TEXT("1 = Medium (critical security checks)\n")
	TEXT("2 = High (full validation, default)"),
	ECVF_Default
);

namespace
{

struct FBlockedPattern
{
	FRegexPattern CompiledRegex;
	FString Description;

	FBlockedPattern(const FString& Pattern, const FString& Desc)
		: CompiledRegex(Pattern, ERegexPatternFlags::CaseInsensitive)
		, Description(Desc)
	{}
};

// Medium patterns — critical security checks (level >= 1)
const TArray<FBlockedPattern>& GetMediumPatterns()
{
	static TArray<FBlockedPattern> Patterns = []()
	{
		TArray<FBlockedPattern> P;

		// --- Dangerous builtins (negative lookbehind excludes method calls like .compile()) ---
		const TCHAR* BlockedBuiltins[] = {
			TEXT("exec"), TEXT("eval"), TEXT("compile"), TEXT("__import__"), TEXT("execfile"), TEXT("open")
		};
		for (const TCHAR* Fn : BlockedBuiltins)
		{
			P.Emplace(FString::Printf(TEXT("(?<!\\.)\\b%s\\s*\\("), Fn),
				FString::Printf(TEXT("Blocked builtin: %s()"), Fn));
		}

		// --- os.system, os.popen, os.exec*, os.spawn*, os.fork ---
		P.Emplace(TEXT("\\bos\\s*\\.\\s*(?:system|popen|exec[a-z]*|spawn[a-z]*|fork)\\s*\\("),
			TEXT("Blocked: os system/process call"));

		// --- Blocked imports (critical) ---
		const TCHAR* MediumModules[] = {
			// System/process
			TEXT("os"), TEXT("sys"), TEXT("subprocess"), TEXT("shutil"),
			TEXT("signal"), TEXT("platform"), TEXT("sysconfig"),
			TEXT("multiprocessing"), TEXT("threading"),
			// Native code
			TEXT("ctypes"), TEXT("_ctypes"),
			// Serialization
			TEXT("pickle"), TEXT("marshal"),
			TEXT("copyreg"), TEXT("jsonpickle"),
			// Network
			TEXT("socket"), TEXT("http"), TEXT("urllib"), TEXT("ftplib"), TEXT("smtplib"),
			TEXT("poplib"), TEXT("imaplib"), TEXT("telnetlib"), TEXT("xmlrpc"),
			TEXT("requests"), TEXT("aiohttp"), TEXT("httpx"), TEXT("ssl"), TEXT("asyncio"),
			// File I/O
			TEXT("pathlib"), TEXT("tempfile"), TEXT("io"), TEXT("glob"),
			TEXT("fileinput"), TEXT("zipfile"), TEXT("tarfile"),
			TEXT("gzip"), TEXT("bz2"), TEXT("lzma"), TEXT("csv"),
			// Other dangerous
			TEXT("webbrowser"), TEXT("antigravity"), TEXT("turtle"), TEXT("tkinter"),
			TEXT("cmd"), TEXT("pdb"), TEXT("pty"), TEXT("resource"), TEXT("mmap"),
			// Code execution / import
			TEXT("importlib"), TEXT("runpy"), TEXT("code"),
		};
		for (const TCHAR* Mod : MediumModules)
		{
			FString Escaped = FString(Mod).Replace(TEXT("_"), TEXT("\\_"));
			P.Emplace(FString::Printf(TEXT("\\b(?:import\\s+(?:\\w+\\s*,\\s*)*|from\\s+)%s\\b"), *Escaped),
				FString::Printf(TEXT("Blocked module: %s"), Mod));
		}

		return P;
	}();
	return Patterns;
}

// High patterns — additional checks (level >= 2)
const TArray<FBlockedPattern>& GetHighPatterns()
{
	static TArray<FBlockedPattern> Patterns = []()
	{
		TArray<FBlockedPattern> P;

		// --- Dunder access ---
		const TCHAR* BlockedDunders[] = {
			TEXT("__builtins__"), TEXT("__class__"), TEXT("__subclasses__"),
			TEXT("__bases__"), TEXT("__mro__"), TEXT("__dict__"),
			TEXT("__globals__"), TEXT("__code__"), TEXT("__func__"),
			TEXT("__self__"), TEXT("__wrapped__"), TEXT("__loader__"),
			TEXT("__spec__"), TEXT("__qualname__"), TEXT("__reduce__"),
		};
		for (const TCHAR* Dunder : BlockedDunders)
		{
			P.Emplace(FString(Dunder), FString::Printf(TEXT("Blocked dunder access: %s"), Dunder));
		}

		// --- Introspection modules ---
		const TCHAR* IntrospectionModules[] = {
			TEXT("inspect"), TEXT("gc"), TEXT("traceback"), TEXT("dis"), TEXT("ast"),
		};
		for (const TCHAR* Mod : IntrospectionModules)
		{
			P.Emplace(FString::Printf(TEXT("\\b(?:import\\s+(?:\\w+\\s*,\\s*)*|from\\s+)%s\\b"), Mod),
				FString::Printf(TEXT("Blocked module: %s"), Mod));
		}

		// --- Obfuscation modules ---
		const TCHAR* ObfuscationModules[] = {
			TEXT("base64"), TEXT("codecs"), TEXT("binascii"),
		};
		for (const TCHAR* Mod : ObfuscationModules)
		{
			P.Emplace(FString::Printf(TEXT("\\b(?:import\\s+(?:\\w+\\s*,\\s*)*|from\\s+)%s\\b"), Mod),
				FString::Printf(TEXT("Blocked module: %s"), Mod));
		}

		// --- Obfuscation patterns ---
		P.Emplace(TEXT("\\bchr\\s*\\(.*\\+"), TEXT("Blocked obfuscation: chr() concatenation"));
		P.Emplace(TEXT("\\\\x[0-9a-fA-F]{2}"), TEXT("Blocked obfuscation: hex escape"));
		P.Emplace(TEXT("\\\\u00[0-9a-fA-F]{2}"), TEXT("Blocked obfuscation: unicode escape"));
		P.Emplace(TEXT("\\bbytearray\\s*\\("), TEXT("Blocked obfuscation: bytearray()"));
		P.Emplace(TEXT("\\bbytes\\.fromhex\\s*\\("), TEXT("Blocked obfuscation: bytes.fromhex()"));
		P.Emplace(TEXT("\\bbytearray\\.fromhex\\s*\\("), TEXT("Blocked obfuscation: bytearray.fromhex()"));

		// --- Dynamic attribute access ---
		P.Emplace(TEXT("(?<!\\.)\\b(?:getattr|setattr|delattr|hasattr)\\s*\\("), TEXT("Blocked: dynamic attribute access"));

		// --- Additional builtins ---
		P.Emplace(TEXT("(?<!\\.)\\bglobals\\s*\\("), TEXT("Blocked introspection: globals()"));
		P.Emplace(TEXT("(?<!\\.)\\blocals\\s*\\("), TEXT("Blocked introspection: locals()"));
		P.Emplace(TEXT("(?<!\\.)\\bvars\\s*\\("), TEXT("Blocked introspection: vars()"));
		P.Emplace(TEXT("(?<!\\.)\\bdir\\s*\\("), TEXT("Blocked introspection: dir()"));
		P.Emplace(TEXT("(?<!\\.)\\bbreakpoint\\s*\\("), TEXT("Blocked builtin: breakpoint()"));

		// --- Additional modules ---
		const TCHAR* AdditionalModules[] = {
			TEXT("shelve"), TEXT("cffi"),
			TEXT("codeop"), TEXT("compileall"), TEXT("py_compile"),
			TEXT("commands"), TEXT("fnmatch"),
		};
		for (const TCHAR* Mod : AdditionalModules)
		{
			FString Escaped = FString(Mod).Replace(TEXT("_"), TEXT("\\_"));
			P.Emplace(FString::Printf(TEXT("\\b(?:import\\s+(?:\\w+\\s*,\\s*)*|from\\s+)%s\\b"), *Escaped),
				FString::Printf(TEXT("Blocked module: %s"), Mod));
		}

		return P;
	}();
	return Patterns;
}

} // anonymous namespace

bool FMCPPythonValidator::Validate(const FString& Code, FString& OutError)
{
	const int32 Level = CVarMCPPythonHardening.GetValueOnGameThread();

	// Level 0 = no validation
	if (Level <= 0)
	{
		return true;
	}

	// 1. Must start with 'import unreal' (always required at level >= 1)
	FString Trimmed = Code.TrimStart();
	static const FString RequiredPrefix = TEXT("import unreal");
	if (!Trimmed.StartsWith(RequiredPrefix))
	{
		OutError = TEXT("Python code must begin with 'import unreal'");
		return false;
	}
	if (Trimmed.Len() > RequiredPrefix.Len())
	{
		TCHAR NextChar = Trimmed[RequiredPrefix.Len()];
		if (!FChar::IsWhitespace(NextChar) && NextChar != TEXT(';') && NextChar != TEXT('\n'))
		{
			OutError = TEXT("Python code must begin with 'import unreal'");
			return false;
		}
	}

	// 2. Medium patterns (level >= 1)
	for (const FBlockedPattern& BP : GetMediumPatterns())
	{
		FRegexMatcher Matcher(BP.CompiledRegex, Code);
		if (Matcher.FindNext())
		{
			OutError = FString::Printf(TEXT("Blocked by medium security: %s"), *BP.Description);
			return false;
		}
	}

	// 3. High patterns (level >= 2)
	if (Level >= 2)
	{
		for (const FBlockedPattern& BP : GetHighPatterns())
		{
			FRegexMatcher Matcher(BP.CompiledRegex, Code);
			if (Matcher.FindNext())
			{
				OutError = FString::Printf(TEXT("Blocked by high security: %s"), *BP.Description);
				return false;
			}
		}
	}

	return true;
}
