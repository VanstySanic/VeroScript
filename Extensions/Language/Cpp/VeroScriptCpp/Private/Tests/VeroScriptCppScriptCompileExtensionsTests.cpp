// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptCppScriptCompileFixture.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptEngine.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
	using namespace VeroScript;
	unsigned Failures = 0;
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}
	bool Returns(VeroScriptSession& S, const char* Name, int Expected)
	{
		FValue V;
		const auto R = S.CallFunction(Name, {}, &V);
		return R.bSuccess && std::get_if<std::int32_t>(&V) && std::get<std::int32_t>(V) == Expected;
	}
	void TestControls()
	{
		std::ostringstream Out, Err;
		FStreamEngineHost H(Out, Err);
		VeroScriptSession S(H);
		VeroScript::Test::Configure(S);
		auto A = std::make_shared<FExecutionControl>(), B = std::make_shared<FExecutionControl>();
		S.SetExecutionControl(A);
		S.SetScriptCompileExtensions({});
		S.SetExecutionControl(B);
		Require(!S.GetScriptCompileExtensions().ExecutionControl, "default compile control preserves configured inheritance");
		FScriptCompileExtensionsOptions P;
		P.ExecutionControl = B;
		S.SetScriptCompileExtensions(P);
		S.SetExecutionControl(A);
		Require(S.GetScriptCompileExtensions().ExecutionControl == B, "explicit compile control stays independent even when initially equal");
		FEngineConfig C;
		VeroScript::Test::Configure(C);
		C.ExecutionControl = A;
		C.ScriptCompileExtensions.ExecutionControl = A;
		S.SetExecutionControl(A);
		C.ExecutionControl = B;
		S.ApplyConfig(C);
		Require(S.GetScriptCompileExtensions().ExecutionControl == A, "ApplyConfig preserves explicit compile control equal to previous runtime control");
		S.SetExecutionControl(A);
		Require(S.GetScriptCompileExtensions().ExecutionControl == A, "independent compile control preserved across repeated replacements");
		C.ScriptCompileExtensions.ExecutionControl.reset();
		S.ApplyConfig(C);
		S.SetExecutionControl(A);
		Require(!S.GetScriptCompileExtensions().ExecutionControl, "ApplyConfig resets compile control inheritance");
		S.SetExecutionControl({});
		S.SetScriptCompileExtensions({});
		S.SetExecutionControl(B);
		Require(!S.GetScriptCompileExtensions().ExecutionControl, "initially null inherited control preserves configured inheritance");
	}
	void TestCompilerCallbacks(const std::filesystem::path& Directory)
	{
		std::ostringstream Out, Err;
		FStreamEngineHost H(Out, Err);
		VeroScriptSession S(H);
		VeroScript::Test::Configure(S);
		Require(S.LoadSource("int old(){return 42;}", "old.vs").bSuccess, "load prior callback program");
		FHostBindingRegistry Bindings;
		std::string Error;
		unsigned Calls = 0;
		bool Reject = false, Generated = false;
		struct FProbe
		{
			VeroScriptSession& Session;
			FHostBindingRegistry& Bindings;
			unsigned& Calls;
			bool& Reject;
			bool& Generated;
		} Probe{S, Bindings, Calls, Reject, Generated};
		Require(Bindings.RegisterNative(
					"host_probe", ENativeType::Void, {},
					[](const FNativeCallContext& Ctx, FValue&, std::string& E)
					{
						auto& P = *static_cast<FProbe*>(Ctx.UserData);
						++P.Calls;
						Require(!Returns(P.Session, P.Generated ? "value" : "old", P.Generated ? 15 : 42),
							"compiler native callback cannot execute the Session");
						P.Session.Clear();
						P.Session.SetScriptCompileExtensions({});
						Require(P.Session.GetScriptCompileExtensions().Bindings == &P.Bindings,
							"compiler native callback cannot mutate Session configuration");
						if (P.Reject)
						{
							E = "compiler callback rejected";
							return false;
						}
						return true;
					},
					&Probe, Error),
			"register compiler-only native callback: " + Error);
		FEngineConfig C;
		VeroScript::Test::Configure(C);
		C.ExecutionBackend = EExecutionBackend::Vm;
		C.StdlibSearchPaths = {VEROSCRIPT_CPP_STDLIB_DIR};
		C.bUseBytecodeCache = true;
		const auto Cache = PathUtil::ToUtf8(Directory / "callback-cache");
		C.BytecodeCacheDirectory = Cache;
		C.ScriptCompileExtensions.Roots = {{PathUtil::ToUtf8(Directory / "callback-builder.vs"), {}}};
		C.ScriptCompileExtensions.TimeoutMilliseconds = 20000;
		C.ScriptCompileExtensions.Bindings = &Bindings;
		C.ScriptCompileExtensions.Configuration = {{"amount", "5"}};
		std::string Extension = CppScriptFixture::Extension;
		Extension.replace(Extension.find("calls+=1;"), std::string("calls+=1;").size(), "host_probe();calls+=1;");
		C.SourceOverlays = {{C.ScriptCompileExtensions.Roots[0].Path, Extension}};
		C.CompileProcessing.ReadDependency = [](std::string_view, std::string& Text)
		{
			Text = "7";
			return true;
		};
		S.ApplyConfig(C);
		const auto Label = PathUtil::ToUtf8(Directory / "callback-main.vs");
		const auto Loaded = S.LoadSource(CppScriptFixture::Source, Label);
		Require(Loaded.bSuccess && Calls == 5 && Returns(S, "value", 15), "compiler-only binding executes across phases: " + Loaded.Error.Message);
		Require(!std::filesystem::exists(Directory / "callback-cache"), "non-deterministic compiler bindings suppress bytecode cache");
		Generated = true;
		Reject = true;
		const auto Failed = S.LoadSource(CppScriptFixture::Source, Label);
		Require(!Failed.bSuccess && Failed.Error.Message.find("compiler callback rejected") != std::string::npos
			&& Returns(S, "value", 15), "compiler callback failure restores Session guard and preserves program");
		Reject = false;
		Calls = 0;
		Require(S.RegisterNative("host_probe", ENativeType::Void, {},
					[](const FNativeCallContext&, FValue&, std::string&)
					{
						return true;
					}),
			"register ordinary business callback");
		C.ScriptCompileExtensions.Bindings = nullptr;
		S.SetScriptCompileExtensions(C.ScriptCompileExtensions);
		const auto Isolated = S.LoadSource(CppScriptFixture::Source, Label);
		Require(!Isolated.bSuccess && Calls == 0, "business binding catalog is not implicitly exposed to compiler scripts");
	}
	void TestPaths(const std::filesystem::path& Directory, EExecutionBackend Backend,
		const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Entry = Directory / "main.vs";
		const auto Label = PathUtil::ToUtf8(Entry);
		{
			std::ofstream F(Entry);
			F << CppScriptFixture::Source;
		}
		int Dependency = 7;
		FEngineConfig C;
		VeroScript::Test::Configure(C);
		C.ExecutionBackend = Backend;
		C.JitBackendFactory = Factory;
		C.bUseBytecodeCache = true;
		const auto CacheRoot = PathUtil::ToUtf8(Directory / "cache");
		C.BytecodeCacheDirectory = CacheRoot;
		C.StdlibSearchPaths = {VEROSCRIPT_CPP_STDLIB_DIR};
		C.ScriptCompileExtensions.Roots = {{PathUtil::ToUtf8(Directory / "builder.vs"), {}}};
		C.ScriptCompileExtensions.TimeoutMilliseconds = 20000;
		C.ScriptCompileExtensions.Configuration = {{"amount", "5"}};
		C.SourceOverlays = {{C.ScriptCompileExtensions.Roots[0].Path, CppScriptFixture::Extension}};
		C.CompileProcessing.ReadDependency = [&](std::string_view, std::string& Text)
		{
			Text = std::to_string(Dependency);
			return true;
		};
		std::ostringstream Out, Err;
		FStreamEngineHost H(Out, Err);
		for (const bool File : {false, true})
		{
			const auto Checked = File ? VeroScript::Test::FEngine::CheckFile(Label, H, C) : VeroScript::Test::FEngine::CheckSource(CppScriptFixture::Source, Label, H, C);
			Require(Checked.bSuccess, "Engine Check script extensions: " + Checked.Error.Message);
			std::filesystem::file_time_type Stamp;
			const auto Cache = PathUtil::FromUtf8(VeroScriptBytecodeIO::GetCachePathForScript(Label, CacheRoot));
			for (unsigned I = 0; I < 2; ++I)
			{
				Out.str("");
				const auto R = File ? VeroScript::Test::FEngine::RunFile(Label, H, C) : VeroScript::Test::FEngine::RunSource(CppScriptFixture::Source, Label, H, ERunMode::Script, C);
				Require(R.bSuccess && Out.str() == "15\n", "Engine execution with cross-phase script state: " + R.Error.Message);
				std::error_code E;
				if (I == 0)
				{
					std::filesystem::last_write_time(
						Cache, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24), E);
					Stamp = std::filesystem::last_write_time(Cache, E);
				}
				else
				{
					Require(std::filesystem::last_write_time(Cache, E) == Stamp && !E,
						"script extension bytecode cache hit");
				}
			}
		}
		Dependency = 11;
		Out.str("");
		Require(VeroScript::Test::FEngine::RunFile(Label, H, C).bSuccess && Out.str() == "19\n", "script dependency invalidates bytecode cache");
		C.ScriptCompileExtensions.Configuration["amount"] = "9";
		Out.str("");
		Require(VeroScript::Test::FEngine::RunFile(Label, H, C).bSuccess && Out.str() == "23\n", "script configuration invalidates bytecode cache");
		auto Imported = C;
		Imported.ScriptCompileExtensions.Roots.clear();
		const std::string ImportSource = "import compiler \"builder.vs\";\n" + std::string(CppScriptFixture::Source);
		Out.str("");
		const auto ImportRun = VeroScript::Test::FEngine::RunSource(ImportSource, Label, H, ERunMode::Script, Imported);
		Require(ImportRun.bSuccess && Out.str() == "23\n", "Engine source-selected compile extension: " + ImportRun.Error.Message);
		Imported.ScriptCompileExtensions.bAllowSourceImports = false;
		Require(!VeroScript::Test::FEngine::CheckSource(ImportSource, Label, H, Imported).bSuccess, "Engine rejects disabled compiler imports");
		VeroScriptSession S(H);
		VeroScript::Test::Configure(S);
		S.ApplyConfig(C);
		S.SetScriptCompileExtensions(Imported.ScriptCompileExtensions);
		Require(!S.LoadSource(ImportSource, Label).bSuccess, "Session rejects disabled compiler imports");
		Imported.ScriptCompileExtensions.bAllowSourceImports = true;
		S.SetScriptCompileExtensions(Imported.ScriptCompileExtensions);
		Require(S.LoadSource(ImportSource, Label).bSuccess && Returns(S, "value", 23), "Session source-selected compile extension");
		S.SetScriptCompileExtensions(C.ScriptCompileExtensions);
		Require(S.LoadSource(CppScriptFixture::Source, Label).bSuccess && Returns(S, "value", 23), "Session source script extension loading");
		Require(S.LoadFile(Label).bSuccess, "Session file script extension loading");
		std::unique_ptr<FBytecodeModule> M;
		Require(S.CompileFile(Label, M).bSuccess && M && M->SourceMetadata &&
				std::any_of(M->SourceMetadata->Nodes.begin(), M->SourceMetadata->Nodes.end(),
					[](const auto& N)
					{
						return N.Name == "Generated" && N.Origin.ProcessorId == "cpp.script";
					}),
			"script generated metadata survives Session compilation");
		S.SetHotReloadEnabled(true);
		Require(S.Run(ERunMode::Eval).bSuccess && S.SetGlobal("kept", std::int32_t{10}).bSuccess, "initialize script generated hot reload state");
		Dependency = 13;
		Require(S.Reload().bSuccess && S.Run(ERunMode::Eval).bSuccess && Returns(S, "value", 34), "script generated reload preserves globals");
		auto Budget = C.ScriptCompileExtensions;
		Budget.MaxInstructions = 1;
		S.SetScriptCompileExtensions(Budget);
		Require(!S.Reload().bSuccess && Returns(S, "value", 34), "failed extension reload preserves active program");
		S.SetScriptCompileExtensions(C.ScriptCompileExtensions);
		auto Cancel = std::make_shared<FExecutionControl>();
		Cancel->RequestCancel();
		C.ExecutionControl = Cancel;
		Require(!VeroScript::Test::FEngine::CheckSource(CppScriptFixture::Source, Label, H, C).bSuccess, "Engine CheckSource inherits cancelled runtime control");
		Require(!VeroScript::Test::FEngine::CheckFile(Label, H, C).bSuccess, "Engine CheckFile inherits cancelled runtime control");
		Require(!VeroScript::Test::FEngine::RunSource(CppScriptFixture::Source, Label, H, ERunMode::Script, C).bSuccess, "Engine RunSource cancelled extension compilation");
		Require(!VeroScript::Test::FEngine::RunFile(Label, H, C).bSuccess, "Engine RunFile cancelled extension compilation");
		C.ScriptCompileExtensions.ExecutionControl = std::make_shared<FExecutionControl>();
		Require(VeroScript::Test::FEngine::CheckSource(CppScriptFixture::Source, Label, H, C).bSuccess, "Engine explicit compile control overrides cancelled runtime control");
		// A read-modify-write of unrelated options must not freeze an inherited token.
		S.SetExecutionControl(std::make_shared<FExecutionControl>());
		auto RoundTrip = S.GetScriptCompileExtensions();
		RoundTrip.Configuration["amount"] = "9";
		S.SetScriptCompileExtensions(std::move(RoundTrip));
		S.SetExecutionControl(Cancel);
		Require(!S.CompileFile(Label, M).bSuccess && !M && Returns(S, "value", 34) == false, "Session inherited cancellation rejects compilation and execution");
		Cancel->Reset();
		Require(Returns(S, "value", 34), "cancelled compilation leaves previous Session executable");
		auto Explicit = C.ScriptCompileExtensions;
		Explicit.ExecutionControl = Cancel;
		S.SetScriptCompileExtensions(Explicit);
		auto Next = std::make_shared<FExecutionControl>();
		Next->RequestCancel();
		S.SetExecutionControl(Next);
		Require(S.CompileFile(Label, M).bSuccess, "explicit Session compile control remains independent during compilation");
	}
}
int main(int Argc, char** Argv)
{
	std::shared_ptr<const VeroScript::IJitBackendFactory> Factory;
	if (Argc > 1)
	{
		std::string Error;
		Factory = VeroScript::LoadJitBackend(Argv[1], Error);
		if (!Factory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	TestControls();
	const auto Directory = std::filesystem::temp_directory_path() / ("vero_cpp_script_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(Directory);
	TestCompilerCallbacks(Directory);
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (Backend == VeroScript::EExecutionBackend::Jit && !Factory)
		{
			continue;
		}

		const auto D = Directory / std::to_string(static_cast<unsigned>(Backend));
		std::filesystem::create_directories(D);
		TestPaths(D, Backend, Factory);
	}
	std::error_code E;
	std::filesystem::remove_all(Directory, E);
	return Failures ? 1 : 0;
}
