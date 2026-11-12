// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Consume only the independently distributed Core and C++ SDK packages.
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptHost.h"
#include "VeroScriptEngine.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#include "VeroScriptSessionDebug.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptCppAot.h"
#include "VeroScriptCppBindGen.h"
#include "VeroScriptCppFrontend.h"
#include "VeroScriptCppToolOutput.h"
#include "VeroScriptCppTranspiler.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptMacro.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptProgramPreparation.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtensionLoader.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace
{
	class FSdkConsoleHost final : public VeroScript::IEngineHost
	{
	public:
		//~ Begin VeroScript::IEngineHost Interface
		void OnPrintLine(std::string_view Line) override
		{
			Text.append(Line);
			Text += '\n';
		}
		void OnDiagnosticLine(std::string_view Line) override
		{
			Error.append(Line);
		}
		VeroScript::Console::FInfo QueryConsole() override
		{
			return {true, true, true, true, 80, 25};
		}
		bool ExecuteConsole(
			VeroScript::Console::EOperation Operation, std::int32_t First, std::int32_t) override
		{
			if (Operation == VeroScript::Console::EOperation::ResetStyle)
			{
				++Resets;
			}

			if (Operation == VeroScript::Console::EOperation::SetCursorVisible && First == 1)
			{
				++CursorRestores;
			}

			return true;
		}
		bool HasConsoleTextOutput() const override
		{
			return true;
		}
		bool WriteConsoleText(std::string_view Value) override
		{
			Text.append(Value);
			return true;
		}
		//~ End VeroScript::IEngineHost Interface
		std::string Text, Error;
		int Resets = 0, CursorRestores = 0;
	};
	class FSdkGenerator final : public VeroScript::IAnnotationProcessor
	{
	public:
		void Process(const VeroScript::FAnnotationContext& Context, VeroScript::FAnnotationOutput& Output) override
		{
			Output.SubmitMethod(Context.Declaration.Id, VeroScript::EMemberAccess::Public,
				"int generated() const { return 42; }");
		}
	};
	class FSdkCompileProcessor final : public VeroScript::ICompileProcessor
	{
	public:
		void Process(const VeroScript::FCompileProcessingContext& C, VeroScript::FCompileProcessingOutput& O) override
		{
			if (C.Phase == VeroScript::ECompileProcessingPhase::RawSource)
			{
				bLoaded = true;
				const auto P = C.Source->Text.find("SDK_CUSTOM");
				if (P != std::string::npos)
				{
					VeroScript::FSourceRange R;
					R.SourceId = C.Source->Id;
					R.Begin = P;
					R.End = P + 10;
					O.EditSource(R, "struct");
				}
			}
			else
			{
				if (!bLoaded)
				{
					throw std::runtime_error("compile processor state lost");
				}

				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "processed" && !D.bHasBody)
					{
						O.CompleteBody(D.Id, C.Snapshot, "{return 19;}");
					}
				}
			}
		}
	private:
		bool bLoaded = false;
	};
	class FSdkDebugBridge final : public VeroScript::ISessionDebugBridge
	{
	public:
		//~ Begin VeroScript::ISessionDebugBridge Interface
		void ProgramChanged(const VeroScript::FSessionDebugProgramView& Program) noexcept override
		{
			bHasProgram = Program.Module != nullptr && Program.Bindings != nullptr;
		}
		std::shared_ptr<VeroScript::VeroScriptDebugger> BeginExecution(
			const VeroScript::FSessionDebugExecutionContext&) noexcept override
		{
			Debugger->SetRunMode(VeroScript::EDebugRunMode::StepIn);
			return Debugger;
		}
		void EndExecution() noexcept override
		{
			++EndedCalls;
		}
		void Detached() noexcept override
		{
			Debugger->SetStopCallback({});
		}
		//~ End VeroScript::ISessionDebugBridge Interface
		std::shared_ptr<VeroScript::VeroScriptDebugger> Debugger =
			std::make_shared<VeroScript::VeroScriptDebugger>();
		int EndedCalls = 0;
		bool bHasProgram = false;
	};
}

int RunSessionConsumer(const int ArgumentCount, const std::string& BackendPath)
{
	using namespace VeroScript;
	const CppTools::FOutputFileNameLess NameLess;
	if (NameLess("Shared", "shared") || NameLess("shared", "Shared") || !NameLess("a", "B"))
	{
		std::cerr << "Packaged output filename comparison failed\n";
		return 1;
	}
	CppTools::FFilePathSet Paths;
	if (!Paths.Insert("cpp-sdk-path-index-probe") || !Paths.Contains("./cpp-sdk-path-index-probe"))
	{
		std::cerr << "Packaged file identity index failed\n";
		return 1;
	}

	const std::string HostDeclarations = "struct HostValue { int value; };"
		"enum class SdkFlags : unsigned int {All=~0u,Wrap=0xffffffffu+1u};"
		"enum SdkLoose {First=1u,Second=(First-2)>0};";
	const CppFrontend::FParseResult Parsed = CppFrontend::ParseSource(
		HostDeclarations, "<cpp-sdk-frontend>");
	if (!Parsed.Succeeded())
	{
		std::cerr << "Packaged C++ frontend failed\n";
		return 1;
	}
	std::vector<std::int64_t> EnumValues;
	std::size_t FailedEnumIndex = 0;
	std::string EnumError;
	if (Parsed.Unit.Enums.size() != 2 || !Parsed.Unit.Enums[0].bHasExplicitUnderlyingType
		|| Parsed.Unit.Enums[1].bHasExplicitUnderlyingType
		|| !CppFrontend::EvaluateEnumValues(Parsed.Unit.Enums[0], EnumValues, FailedEnumIndex, EnumError)
		|| EnumValues != std::vector<std::int64_t>{4294967295LL, 0}
		|| !CppFrontend::EvaluateEnumValues(Parsed.Unit.Enums[1], EnumValues, FailedEnumIndex, EnumError)
		|| EnumValues != std::vector<std::int64_t>{1, 1})
	{
		std::cerr << "Packaged C++ enum evaluation failed: " << EnumError << '\n';
		return 1;
	}
	const CppBindGen::FBindGenResult Bindings = CppBindGen::GenerateBindingsFromSource(
		HostDeclarations, "<cpp-sdk-bindgen>");
	if (!Bindings.Succeeded() || Bindings.CatalogJson.empty())
	{
		std::cerr << "Packaged C++ binding generator failed\n";
		return 1;
	}
	const CppTranspiler::FTranspileResult Transpiled = CppTranspiler::TranspileSource(
		HostDeclarations + "int add(int left, int right) { return left + right; }", "<cpp-sdk-transpiler>");
	if (!Transpiled.Succeeded() || Transpiled.Source.empty())
	{
		std::cerr << "Packaged C++ transpiler failed\n";
		return 1;
	}
	std::string GeneratedCpp;
	std::string AotError;
	if (!EmitCppFromSource(
			"int main() { return 0; }", "<cpp-sdk-aot>", GeneratedCpp, AotError)
		|| GeneratedCpp.empty())
	{
		std::cerr << "Packaged C++ AOT emitter failed: " << AotError << '\n';
		return 1;
	}

	std::ostringstream Output;
	std::ostringstream Diagnostics;
	FStreamEngineHost Host(Output, Diagnostics);
	{
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "sdk.app";
		Description.Scopes = {{"sdk.app", {"sdk.library"}}, {"sdk.library", {}}};
		Description.Modules = {{"sdk/library.vs", "sdk.library", "veroscript-module:/sdk/library.vs",
			"sdk.library", "sdk.module.1", std::string("export int module_value(){return 42;}")}};
		std::string Error;
		FEngineConfig Config;
		VeroScript::BytecodeVm::Configure(Config);
		Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Description), Error);
		if (!Config.ModuleEnvironment || BuildNativeExtensionAbi().empty())
		{
			std::cerr << "Packaged module/native headers failed: " << Error << '\n';
			return 1;
		}
		VeroScriptSession EnvironmentSession(Host);
		VeroScript::BytecodeVm::Configure(EnvironmentSession);
		EnvironmentSession.ApplyConfig(Config);
		const std::string Source = "import \"sdk/library.vs\"; int main(){return module_value();}";
		if (!EnvironmentSession.LoadSource(Source, "<sdk-module>").bSuccess)
		{
			std::cerr << "Packaged C++ module environment load failed\n";
			return 1;
		}
		const auto SessionResult = EnvironmentSession.Run();
		const auto EngineResult = VeroScriptEngine::RunSource(Source, "<sdk-module>", Host, ERunMode::Script, Config);
		if (!SessionResult.bSuccess || SessionResult.ExitCode != 42 || !EngineResult.bSuccess ||
			EngineResult.ExitCode != 42)
		{
			std::cerr << "Packaged C++ module environment execution failed\n";
			return 1;
		}
		FCodegenOptions Codegen;
		Codegen.ModuleEnvironment = Config.ModuleEnvironment;
		FCodegenResult Generated;
		FCppCodegenBackend Backend;
		if (!Backend.EmitFromSource(Source, "<sdk-module>", Codegen, Generated) || Generated.Output.empty())
		{
			std::cerr << "Packaged C++ module environment AOT failed: " << Generated.Error << '\n';
			return 1;
		}
	}
	VeroScriptSession Session(Host);
	VeroScript::BytecodeVm::Configure(Session);
	FEngineConfig MacroConfig;
	VeroScript::BytecodeVm::Configure(MacroConfig);
	MacroConfig.SourceOverlays = {{"cpp-sdk-macros.vs",
		"#define_export SDK_SQUARE(x) ((x)*(x))\n"}};
	auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
	std::string RegistryError;
	// CaseSensitive applies to annotation names; source spelling is retained.
	if (!Session.GetBindingRegistry().RegisterAnnotation(
			"@SdkGenerate", "SDK generator", RegistryError, "", {}, AnnotationAllTargets, false))
	{
		std::cerr << RegistryError << '\n';
		return 1;
	}
	if (!Registry->Register(
			{"@SdkGenerate", "sdk.generator", "1", "", AnnotationTargetMask(EAnnotationTarget::Struct), 1, 0,
				[]
				{
					return std::make_unique<FSdkGenerator>();
				}},
			RegistryError))
	{
		std::cerr << RegistryError << '\n';
		return 1;
	}
	Registry->Freeze();
	MacroConfig.AnnotationProcessing.Registry = Registry;
	auto CompileRegistry = std::make_shared<FCompileProcessorRegistry>();
	FCompileProcessorDesc CompileDesc;
	CompileDesc.ProcessorId="sdk.compile";
	CompileDesc.Version="1";
	CompileDesc.Phases=CompilePhaseMask(ECompileProcessingPhase::RawSource)|CompilePhaseMask(ECompileProcessingPhase::Bodies);
	CompileDesc.Operations=CompileOperationMask(ECompileOperation::EditSource)|CompileOperationMask(ECompileOperation::CompleteBody);
	CompileDesc.Subscriptions = {{ECompileProcessingEvent::Module}};
	CompileDesc.Factory = []
	{
		return std::make_unique<FSdkCompileProcessor>();
	};
	if (!CompileRegistry->Register(std::move(CompileDesc), RegistryError))
	{
		std::cerr << RegistryError << '\n';
		return 1;
	}
	CompileRegistry->Freeze();
	MacroConfig.CompileProcessing.Registry = CompileRegistry;
	Session.ApplyConfig(MacroConfig);
	Session.SetAnnotationProcessing(MacroConfig.AnnotationProcessing);
	if (Session.GetAnnotationProcessing().Registry != Registry) return 1;
	Session.SetCompileProcessing(MacroConfig.CompileProcessing);
	if (Session.GetCompileProcessing().Registry != CompileRegistry) return 1;
	FCodegenOptions CodegenOptions;
	CodegenOptions.AnnotationProcessing = MacroConfig.AnnotationProcessing;
	CodegenOptions.CompileProcessing = MacroConfig.CompileProcessing;
	CodegenOptions.Bindings = &Session.GetBindingCatalog();
	FCppCodegenBackend CppBackend;
	FCodegenResult Generated;
	if (!CppBackend.EmitFromSource("@sDKgENERATE SDK_CUSTOM Generated {}; int processed(); void main(){Generated x=Generated();print(x.generated()+processed());}",
		"<cpp-sdk-generated-aot>", CodegenOptions, Generated) || Generated.Output.empty())
	{
		std::cerr << "Packaged annotation AOT failed: " << Generated.Error << '\n';
		return 1;
	}
	{
		FEngineConfig ScriptConfig;
		VeroScript::BytecodeVm::Configure(ScriptConfig);
		const auto Root = VeroScript::PathUtil::ExecutableDirectory() + "/sdk-compiler.vs";
		ScriptConfig.ScriptCompileExtensions.Roots = {{Root, {}}};
		ScriptConfig.ScriptCompileExtensions.Configuration = {{"result", "29"}};
		ScriptConfig.SourceOverlays = {{Root, R"VS(
import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 auto ds=ctx.declarations();for(int i=0;i<len(ds);++i)if(ds[i].name()=="script_result")
  out.complete_body(ds[i].id(),"{return "+ctx.configuration("result").value()+";}");
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
 std::compiler::processor_definition d=std::compiler::processor_definition();d.id="sdk.script";d.version="1";
 d.phases=array<std::compiler::phase>{std::compiler::phase::bodies};
 d.operations=array<std::compiler::operation>{std::compiler::operation::complete_body};
 d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);
})VS"}};
		VeroScriptSession ScriptSession(Host);
		VeroScript::BytecodeVm::Configure(ScriptSession);
		ScriptSession.ApplyConfig(ScriptConfig);
		ScriptSession.SetScriptCompileExtensions(ScriptConfig.ScriptCompileExtensions);
		if (ScriptSession.GetScriptCompileExtensions().Roots.size() != 1) return 1;
		if (ArgumentCount > 1)
		{
			std::string Error;
			if (!ScriptSession.SetJitBackendPath(BackendPath, Error))
			{
				std::cerr << Error << '\n';
				return 1;
			}
			ScriptSession.SetExecutionBackend(EExecutionBackend::Jit);
		}
		const std::string Script = "int script_result();void main(){print(script_result());}";
		const auto Loaded = ScriptSession.LoadSource(Script, "<cpp-sdk-script-extension>");
		if (!Loaded.bSuccess)
		{
			std::cerr << "Packaged script compilation failed: " << Loaded.Error.Message << '\n';
			return 1;
		}
		FValue Value;
		const auto Called = ScriptSession.CallFunction("script_result", {}, &Value);
		if (!Called.bSuccess || !std::get_if<std::int32_t>(&Value) || std::get<std::int32_t>(Value) != 29)
		{
			std::cerr << "Packaged generated function failed: " << Called.Error.Message << '\n';
			return 1;
		}
		FCodegenOptions ScriptCodegen;
		ScriptCodegen.ScriptCompileExtensions = ScriptConfig.ScriptCompileExtensions;
		ScriptCodegen.ScriptCompileExtensions.VmBackendFactory = ScriptSession.GetVmExecutionDomain()->GetFactory();
		ScriptCodegen.SourceOverlays = ScriptConfig.SourceOverlays;
		if (!CppBackend.EmitFromSource(Script, "<cpp-sdk-script-aot>", ScriptCodegen, Generated) ||
			Generated.Output.empty())
		{
			std::cerr << "Packaged script extension AOT failed: " << Generated.Error << '\n';
			return 1;
		}
	}
	if (ArgumentCount > 1)
	{
		std::string Error;
		if (!Session.SetJitBackendPath(BackendPath, Error))
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}

	FJitPolicy Policy;
	Policy.MaxCompileAttempts = 8;
	Policy.MaxCachedEntries = 8;
	Policy.EagerCompileBudget = 8;
	Policy.bReuseSessionCode = true;
	Session.SetJitPolicy(Policy);
	Session.SetExecutionBackend(ArgumentCount > 1 ? EExecutionBackend::Jit : EExecutionBackend::Mixed);

	FSessionJitCacheLimits Limits;
	Session.SetJitCacheLimits(Limits);
	if (Session.GetJitCacheLimits().MaxNativeBytes != 8u * 1024u * 1024u
		|| Session.GetJitCacheLimits().MaxRetainedPayloadBytes != 32u * 1024u * 1024u)
	{
		std::cerr << "Unexpected packaged C++ session-cache defaults\n";
		return 1;
	}

	const FRunResult Load = Session.LoadSource(
		"export import \"cpp-sdk-macros.vs\";\n#define_export SDK_ENTRY(x) SDK_SQUARE(x)\n"
		"@sDKgENERATE SDK_CUSTOM Generated {}; int processed(); int generated(){type<Generated> info=type<Generated>();"
		"assert(info.annotation(0).matches_name(\"@SDKGENERATE\") && !info.annotation(0).definition().case_sensitive());"
		"Generated x=Generated();return x.generated();}"
		"int square(int value) { return SDK_ENTRY(value); } void main() {}",
		"<cpp-sdk-consumer>");
	if (!Load.bSuccess)
	{
		std::cerr << "C++ SDK load failed:\n"
				  << Diagnostics.str();
		return 1;
	}
	const auto Program = Session.GetDebugProgram();
	std::string MacroError;
	if (!Program.Module || Program.Module->PublicMacros.size() != 2
		|| !ValidatePublicMacros(Program.Module->PublicMacros, MacroError))
	{
		std::cerr << "Packaged C++ SDK lost the owned public macro interface: " << MacroError << '\n';
		return 1;
	}

	FValue ReturnValue;
	if (!Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| std::get_if<std::int32_t>(&ReturnValue) == nullptr
		|| std::get<std::int32_t>(ReturnValue) != 49)
	{
		std::cerr << "C++ SDK cold call failed:\n"
				  << Diagnostics.str();
		return 1;
	}
	const FSessionJitCacheStats ColdStats = Session.GetJitCacheStats();
	if (ArgumentCount <= 1 && (Session.GetJitBackendFactory()
		|| ColdStats.CompileAttempts != 0 || ColdStats.RetainedPayloadBytes != 0
		|| ColdStats.bResourceLimitReached))
	{
		std::cerr << "Pure C++ SDK unexpectedly selected a concrete backend\n";
		return 1;
	}
	if (ArgumentCount > 1 && (!Session.GetJitBackendFactory()
		|| ColdStats.CompileAttempts == 0 || ColdStats.RetainedPayloadBytes == 0
		|| ColdStats.bResourceLimitReached))
	{
		std::cerr << "Explicit C++ SDK JIT backend did not populate the session cache\n";
		return 1;
	}

	ReturnValue = {};
	if (!Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| std::get_if<std::int32_t>(&ReturnValue) == nullptr
		|| std::get<std::int32_t>(ReturnValue) != 49
		|| Session.GetLastJitStats().CompileAttempts != 0)
	{
		std::cerr << "Pure C++ SDK repeat call failed\n";
		return 1;
	}
	const FSessionJitCacheStats WarmStats = Session.GetJitCacheStats();
	if (WarmStats.CompileAttempts != ColdStats.CompileAttempts
		|| WarmStats.RetainedPayloadBytes != ColdStats.RetainedPayloadBytes
		|| WarmStats.NativeBytes != ColdStats.NativeBytes)
	{
		std::cerr << "C++ SDK cache accounting changed on a warm call\n";
		return 1;
	}
	if (!Session.CallFunction("generated", {}, &ReturnValue).bSuccess
		|| std::get_if<std::int32_t>(&ReturnValue) == nullptr || std::get<std::int32_t>(ReturnValue) != 42)
	{
		std::cerr << "Packaged annotation Session failed\n" << Diagnostics.str();
		return 1;
	}
	if (!Session.CallFunction("processed", {}, &ReturnValue).bSuccess
		|| !std::get_if<std::int32_t>(&ReturnValue) || std::get<std::int32_t>(ReturnValue)!=19)
	{
		std::cerr << "Packaged compile processing Session failed\n" << Diagnostics.str();
		return 1;
	}
	const auto StreamRun = VeroScriptEngine::RunSource(
		"int main() { return 42; }", "<cpp-sdk-stream>", Output, Diagnostics,
		ERunMode::Script, {}, {}, nullptr, {}, {}, Session.GetExecutionBackend(),
		{}, {}, true, false, Session.GetJitBackendFactory(), Session.GetVmExecutionDomain()->GetFactory());
	if (!StreamRun.bSuccess || StreamRun.ExitCode != 42)
	{
		std::cerr << "Packaged C++ stream backend selection failed\n" << Diagnostics.str();
		return 1;
	}

	auto Bridge = std::make_shared<FSdkDebugBridge>();
	bool bEdited = false;
	Bridge->Debugger->SetStopCallback([&](VeroScriptDebugger& Active, EDebugStopReason)
	{
		bEdited = Active.SetLocal("value", "9").bOk;
		Active.SetRunMode(EDebugRunMode::Continue);
	});
	if (!Session.SetDebugBridge(Bridge) || !Bridge->bHasProgram
		|| !Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| std::get_if<std::int32_t>(&ReturnValue) == nullptr
		|| std::get<std::int32_t>(ReturnValue) != 81 || !bEdited
		|| Bridge->EndedCalls != 1 || Session.GetLastJitStats().CompileAttempts != 0
		|| !Session.SetDebugBridge(nullptr))
	{
		std::cerr << "Packaged C++ live Session debugger failed\n" << Diagnostics.str();
		return 1;
	}
	if (!Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| std::get<std::int32_t>(ReturnValue) != 49)
	{
		std::cerr << "Packaged C++ Session failed after debugger removal\n";
		return 1;
	}
	Policy.Diagnostics = {8, 32, false};
	Session.SetJitPolicy(Policy);
	if (!Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| std::get_if<std::int32_t>(&ReturnValue) == nullptr
		|| std::get<std::int32_t>(ReturnValue) != 49)
	{
		std::cerr << "Packaged C++ diagnostic call failed\n" << Diagnostics.str();
		return 1;
	}
	const FJitDiagnostics SavedDiagnostics = Session.GetLastJitDiagnostics();
	bool FoundSquare = false;
	for (const auto& Function : SavedDiagnostics.Functions)
	{
		if (Function.Name.find("square") != std::string::npos)
		{
			FoundSquare = Function.Calls == 1 && Function.Returned == 1;
		}
	}
	Policy.Diagnostics = {};
	Session.SetJitPolicy(Policy);
	if (!FoundSquare || SavedDiagnostics.bTimingEnabled
		|| !Session.CallFunction("square", {std::int32_t{7}}, &ReturnValue).bSuccess
		|| !Session.GetLastJitDiagnostics().Functions.empty())
	{
		std::cerr << "Packaged C++ diagnostic snapshot did not survive VM destruction or clear on disable\n";
		return 1;
	}
	FEngineConfig DelegateConfig;
	VeroScript::BytecodeVm::Configure(DelegateConfig);
	DelegateConfig.ExecutionBackend = ArgumentCount > 1 ? EExecutionBackend::Jit : EExecutionBackend::Vm;
	DelegateConfig.JitBackendFactory = Session.GetJitBackendFactory();
	for (const char* Module : {"delegate.vs", "core.vs"})
	{
		const std::string Source = std::string("import \"") + Module + "\";\n"
			"int twice(int value){return value*2;}\n"
			"void main(){delegate<int,int> single=delegate<int,int>();single.bind(twice);"
			"print(single.execute(21));multicast_delegate<int> multiple=multicast_delegate<int>();"
			"multiple.add(fn(int value)->void {print(value);});multiple.broadcast(7);}\n";
		Output.str("");
		Diagnostics.str("");
		const auto Result = VeroScriptEngine::RunSource(Source, "<cpp-sdk-delegate>", Host, ERunMode::Script, DelegateConfig);
		if (!Result.bSuccess || Output.str() != "42\n7\n")
		{
			std::cerr << "Packaged delegate import failed: " << Module << '\n' << Diagnostics.str();
			return 1;
		}
	}
	FSdkConsoleHost Terminal;
	const auto ConsoleResult = VeroScriptEngine::RunSource(R"VS(void main(){
        assert(std::console::supports_color() && std::console::supports_cursor());
        assert(std::console::supports_clear() && std::console::is_output_terminal());
        assert(std::console::width()==80 && std::console::height()==25);
        assert(std::console::set_color("cyan","default"));
        assert(std::console::set_cursor_visible(false));
        std::console::write("exact"); print("line"); std::console::flush();
    })VS", "<cpp-sdk-console>", Terminal, ERunMode::Script, DelegateConfig);
	if (!ConsoleResult.bSuccess || Terminal.Text!="exactline\n" || Terminal.Resets!=1 || Terminal.CursorRestores!=1)
	{
		std::cerr << "Packaged console host integration failed\n" << Terminal.Error;
		return 1;
	}
	std::cout << "C++ SDK session-cache and debugger consumer passed\n";
	return 0;
}

#if defined(_WIN32)
int wmain(const int ArgumentCount, wchar_t* const Arguments[])
{
	const std::string BackendPath = ArgumentCount > 1
		? VeroScript::PathUtil::ToUtf8(std::filesystem::path(Arguments[1])) : std::string{};
	return RunSessionConsumer(ArgumentCount, BackendPath);
}
#else
int main(const int ArgumentCount, char* const Arguments[])
{
	return RunSessionConsumer(ArgumentCount, ArgumentCount > 1 ? Arguments[1] : "");
}
#endif
