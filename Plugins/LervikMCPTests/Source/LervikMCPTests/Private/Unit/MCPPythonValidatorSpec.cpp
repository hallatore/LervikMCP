#include "Misc/AutomationTest.h"
#include "MCPPythonValidator.h"
#include "HAL/IConsoleManager.h"

BEGIN_DEFINE_SPEC(FMCPPythonValidatorSpec, "Plugins.LervikMCP.PythonValidator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FMCPPythonValidatorSpec)

void FMCPPythonValidatorSpec::Define()
{
	Describe("Valid scripts", [this]()
	{
		It("accepts simple unreal API call", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nunreal.EditorAssetLibrary.list_assets('/Game/')"), Error);
			TestTrue("Should pass", bOk);
			TestTrue("No error", Error.IsEmpty());
		});

		It("accepts get_all_level_actors", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nresult = unreal.EditorLevelLibrary.get_all_level_actors()"), Error);
			TestTrue("Should pass", bOk);
		});

		It("accepts multi-line unreal script", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\n\nasset_tools = unreal.AssetToolsHelpers.get_asset_tools()\nresult = asset_tools.create_asset('MyAsset', '/Game/Test', unreal.StaticMesh, None)\nunreal.log('Done')"), Error);
			TestTrue("Should pass", bOk);
		});

		It("accepts unreal methods containing 'open' substring", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nunreal.EditorAssetLibrary.open_editor_for_asset('/Game/M')"), Error);
			TestTrue("Should pass — .open_editor_for_asset is not bare open()", bOk);
		});

		It("accepts .compile() method calls on UE objects", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nunreal.EditorAssetLibrary.compile_blueprint(bp)"), Error);
			TestTrue("Should pass — .compile_blueprint is a UE method", bOk);
		});

		It("accepts .exec() method calls on UE objects", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nresult = obj.exec_command('stat fps')"), Error);
			TestTrue("Should pass — .exec_command is a UE method", bOk);
		});
	});

	Describe("import unreal prefix", [this]()
	{
		It("rejects code without import unreal", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(TEXT("print('hello')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions import unreal", Error.Contains(TEXT("import unreal")));
		});

		It("rejects import unreal_something as prefix", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(TEXT("import unrealengine"), Error);
			TestFalse("Should fail", bOk);
		});

		It("rejects empty code", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(TEXT(""), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions import unreal", Error.Contains(TEXT("import unreal")));
		});

		It("rejects whitespace-only code", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(TEXT("   \n\t  \n"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions import unreal", Error.Contains(TEXT("import unreal")));
		});
	});

	Describe("Blocked builtins", [this]()
	{
		It("blocks exec()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nexec('print(1)')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions exec", Error.Contains(TEXT("exec")));
		});

		It("blocks eval()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\neval('1+1')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions eval", Error.Contains(TEXT("eval")));
		});

		It("blocks open()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nf = open('/etc/passwd')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions open", Error.Contains(TEXT("open")));
		});

		It("blocks __import__()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\n__import__('os')"), Error);
			TestFalse("Should fail", bOk);
		});
	});

	Describe("Blocked modules", [this]()
	{
		It("blocks import os", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport os"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions os", Error.Contains(TEXT("os")));
		});

		It("blocks import subprocess", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport subprocess"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import requests", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport requests"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import socket", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport socket"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import pickle", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport pickle"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import ctypes", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport ctypes"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import base64", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport base64"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks import sys", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport sys"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks from os import system", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nfrom os import system"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks comma-separated import with blocked module", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nimport json, os"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions os", Error.Contains(TEXT("os")));
		});
	});

	Describe("Blocked system calls", [this]()
	{
		It("blocks os.system()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nos.system('rm -rf /')"), Error);
			TestFalse("Should fail", bOk);
		});
	});

	Describe("Blocked dunder access", [this]()
	{
		It("blocks __builtins__", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\n__builtins__['exec']('bad')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions __builtins__", Error.Contains(TEXT("__builtins__")));
		});

		It("blocks __subclasses__", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nobject.__subclasses__()"), Error);
			TestFalse("Should fail", bOk);
		});
	});

	Describe("Blocked obfuscation", [this]()
	{
		It("blocks chr() concatenation", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nx = chr(111) + chr(115)"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions chr", Error.Contains(TEXT("chr")));
		});

		It("blocks bytearray()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nx = bytearray(b'os')"), Error);
			TestFalse("Should fail", bOk);
		});
	});

	Describe("Blocked introspection", [this]()
	{
		It("blocks globals()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\ng = globals()"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks locals()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nl = locals()"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks vars()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nv = vars()"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions vars", Error.Contains(TEXT("vars")));
		});

		It("blocks dir()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nd = dir()"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions dir", Error.Contains(TEXT("dir")));
		});
	});

	Describe("Blocked dynamic attribute access", [this]()
	{
		It("blocks getattr()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\ngetattr(obj, 'method')"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks setattr()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nsetattr(obj, 'x', 1)"), Error);
			TestFalse("Should fail", bOk);
		});

		It("blocks hasattr()", [this]()
		{
			FString Error;
			bool bOk = FMCPPythonValidator::Validate(
				TEXT("import unreal\nhasattr(obj, 'name')"), Error);
			TestFalse("Should fail", bOk);
			TestTrue("Error mentions attribute", Error.Contains(TEXT("attribute")));
		});
	});

	Describe("Hardening Levels", [this]()
	{
		AfterEach([this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			if (CVar) CVar->Set(2);
		});

		It("should allow everything at level 0", [this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			CVar->Set(0);
			FString Error;
			TestTrue(TEXT("Level 0 allows exec"), FMCPPythonValidator::Validate(TEXT("exec('bad')"), Error));
			TestTrue(TEXT("Level 0 allows no import unreal"), FMCPPythonValidator::Validate(TEXT("import os"), Error));
		});

		It("should block critical at level 1", [this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			CVar->Set(1);
			FString Error;
			TestFalse(TEXT("Level 1 blocks exec"), FMCPPythonValidator::Validate(TEXT("import unreal\nexec('bad')"), Error));
			TestFalse(TEXT("Level 1 blocks import os"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport os"), Error));
			TestFalse(TEXT("Level 1 blocks subprocess"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport subprocess"), Error));
			TestFalse(TEXT("Level 1 blocks socket"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport socket"), Error));
			TestFalse(TEXT("Level 1 blocks importlib"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport importlib"), Error));
			TestFalse(TEXT("Level 1 blocks runpy"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport runpy"), Error));
			TestFalse(TEXT("Level 1 blocks code"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport code"), Error));
		});

		It("should allow high-only patterns at level 1", [this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			CVar->Set(1);
			FString Error;
			TestTrue(TEXT("Level 1 allows __builtins__"), FMCPPythonValidator::Validate(TEXT("import unreal\nprint(__builtins__)"), Error));
			TestTrue(TEXT("Level 1 allows getattr"), FMCPPythonValidator::Validate(TEXT("import unreal\ngetattr(obj, 'name')"), Error));
			TestTrue(TEXT("Level 1 allows import base64"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport base64"), Error));
			TestTrue(TEXT("Level 1 allows import inspect"), FMCPPythonValidator::Validate(TEXT("import unreal\nimport inspect"), Error));
		});

		It("should block everything at level 2", [this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			CVar->Set(2);
			FString Error;
			TestFalse(TEXT("Level 2 blocks exec"), FMCPPythonValidator::Validate(TEXT("import unreal\nexec('bad')"), Error));
			TestFalse(TEXT("Level 2 blocks __builtins__"), FMCPPythonValidator::Validate(TEXT("import unreal\nprint(__builtins__)"), Error));
			TestFalse(TEXT("Level 2 blocks getattr"), FMCPPythonValidator::Validate(TEXT("import unreal\ngetattr(obj, 'name')"), Error));
		});

		It("default level should be 2", [this]()
		{
			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mcp.python.hardening"));
			TestEqual(TEXT("Default is 2"), CVar->GetInt(), 2);
		});
	});
}
