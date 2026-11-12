// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptEngine.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"
#include "VeroScriptSourceMetadata.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace
{
	using namespace VeroScript;
	unsigned Failures = 0;
	bool Require(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			++Failures;
			std::cerr << Message << '\n';
		}
		return Condition;
	}
	class FTempDirectory
	{
	public:
		FTempDirectory()
		{
			const auto Stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			Path = std::filesystem::temp_directory_path() / ("veroscript_cpp_annotations_" + std::to_string(Stamp));
			std::filesystem::create_directories(Path);
		}
		~FTempDirectory()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
		std::filesystem::path Path;
	};
	using FProcess = std::function<void(const FAnnotationContext&, FAnnotationOutput&)>;
	struct FProcessor final : IAnnotationProcessor
	{

		void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
		{
			ProcessCallback(Context, Output);
		}

		explicit FProcessor(FProcess InProcess) : ProcessCallback(std::move(InProcess))
		{
		}
		FProcess ProcessCallback;
	};
	std::shared_ptr<const FAnnotationProcessorRegistry> Registry(FProcess Process, const bool Cacheable = true)
	{
		auto Result = std::make_shared<FAnnotationProcessorRegistry>();
		FAnnotationProcessorDesc Desc;
		Desc.Name = "@Generate";
		Desc.ProcessorId = "cpp.integration";
		Desc.Version = "1";
		Desc.Targets = AnnotationTargetMask(EAnnotationTarget::Struct);
		Desc.Operations = static_cast<std::uint8_t>(EAnnotationOperation::AddMethod);
		Desc.Factory = [Process = std::move(Process)]
		{
			return std::make_unique<FProcessor>(Process);
		};
		Desc.Cacheable = Cacheable;
		std::string Error;
		if (!Result->Register(std::move(Desc), Error))
		{
			throw std::runtime_error(Error);
		}

		Result->Freeze();
		return Result;
	}
	void ReportNotes(FAnnotationOutput& Output)
	{
		for (const auto Severity : {ECompileDiagnosticSeverity::Warning,
		         ECompileDiagnosticSeverity::Information, ECompileDiagnosticSeverity::Hint})
		{
			FScriptError Diagnostic;
			Diagnostic.Message = "cpp annotation note";
			Diagnostic.Severity = Severity;
			Output.ReportDiagnostic(std::move(Diagnostic));
		}
	}
	bool HasNotes(const FRunResult& Result)
	{
		return Result.bSuccess && Result.Diagnostics.size() == 3 && Result.Diagnostics[0].Severity == ECompileDiagnosticSeverity::Warning && Result.Diagnostics[1].Severity == ECompileDiagnosticSeverity::Information && Result.Diagnostics[2].Severity == ECompileDiagnosticSeverity::Hint;
	}
	bool CallReturns(VeroScriptSession& Session, const char* Name, const int Expected)
	{
		FValue Value;
		return Session.CallFunction(Name, {}, &Value).bSuccess && std::get_if<std::int32_t>(&Value) && std::get<std::int32_t>(Value) == Expected;
	}
	const std::string AnnotationSource = "@Generate struct Generated {}; int kept=1;"
	                                     "int sample(){Generated x=Generated();return x.Read()+kept;} void main(){print(sample());}";

	void TestAnnotationNamePolicy(const std::filesystem::path& Directory,
	    const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const std::string Source = R"vs(
@gEnErAtE @Strict @Unknown struct Generated {};
int sample(){
 type<Generated> t=type<Generated>();
 assert(t.has_annotation("GENERATE") && !t.has_annotation("strict") && !t.has_annotation("unknown"));
 assert(len(t.find_annotations("@GENERATE"))==1 && len(t.find_annotations("GENERATE"))==0);
 assert(t.annotation(0).name()=="@gEnErAtE" && t.annotation(0).matches_name("@GENERATE"));
 assert(!t.annotation(0).definition().case_sensitive() && t.annotation(1).definition().case_sensitive());
 Generated x=Generated();return x.Read();
}
void main(){print(sample());}
)vs";
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		std::string Error;
		if (!Require(
		        Catalog->RegisterAnnotation("@Generate", "", Error, "", {}, AnnotationAllTargets, false) &&
		            Catalog->RegisterAnnotation("@Strict", "", Error),
		        "register annotation name policies"))
		{
			return;
		}

		const auto Entry = Directory / "annotation-names.vs";
		{
			std::ofstream File(Entry);
			File << Source;
			File.close();
			if (!Require(bool(File), "write annotation policy source")) return;
		}
		const auto Label = PathUtil::ToUtf8(Entry);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}

			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			const auto CacheDirectory = PathUtil::ToUtf8(Directory / "name-policy-cache");
			FEngineConfig Config;
			VeroScript::Test::Configure(Config);
			Config.ExecutionBackend = Backend;
			Config.JitBackendFactory = Factory;
			Config.BytecodeCacheDirectory = CacheDirectory;
			Config.bUseBytecodeCache = true;
			Config.CompileProcessing.AnnotationDefinitions = GetAnnotationDefinitions(*Catalog);
			Config.AnnotationProcessing.Registry = Registry(
			    [](const auto& Context, auto& Generated)
			{
				Generated.SubmitMethod(
				    Context.Declaration.Id, EMemberAccess::Public, "int Read(){return 42;}");
			});
			Require(VeroScript::Test::FEngine::CheckSource(Source, Label, Host, Config).bSuccess && VeroScript::Test::FEngine::CheckFile(Label, Host, Config).bSuccess, "Engine checks use annotation name policies");
			for (unsigned Run = 0; Run < 2; ++Run)
			{
				Output.str("");
				Require(VeroScript::Test::FEngine::RunSource(Source, Label, Host, ERunMode::Script, Config).bSuccess && VeroScript::Test::FEngine::RunFile(Label, Host, Config).bSuccess && Output.str() == "42\n42\n",
				    "Engine source/file and cached execution share annotation matching and reflection");
			}
			// Local Session registration must reach compilation without explicitly copied definitions.
			Config.CompileProcessing.AnnotationDefinitions.clear();
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.ApplyConfig(Config);
			Require(Session.GetBindingRegistry().RegisterAnnotation("@Generate", "", Error, "", {}, AnnotationAllTargets, false) && Session.GetBindingRegistry().RegisterAnnotation("@Strict", "", Error), "register Session annotation policies");
			Require(Session.LoadSource(Source, Label).bSuccess && CallReturns(Session, "sample", 42) && Session.LoadFile(Label).bSuccess && CallReturns(Session, "sample", 42) && Session.Reload().bSuccess && CallReturns(Session, "sample", 42), "Session load and reload retain annotation name policies");

			FModuleEnvironmentDesc Environment;
			Environment.EntryOwnerId = "app";
			Environment.Scopes = {{"app", {}}};
			Environment.Modules = {{"names.vs", "names", "veroscript-module:/app/names.vs", "app", "names.1", Source}};
			Environment.Bindings = Catalog;
			Config.ModuleEnvironment = FModuleEnvironment::Create(std::move(Environment), Error);
			if (!Require(bool(Config.ModuleEnvironment), "create annotation policy environment"))
			{
				continue;
			}

			VeroScriptSession Published(Host);
			VeroScript::Test::Configure(Published);
			Published.ApplyConfig(Config);
			Require(Published.LoadFile("veroscript-module:/app/names.vs").bSuccess && CallReturns(Published, "sample", 42),
			    "environment-owned annotation policies reach Session processors and reflection");
			Output.str("");
			Require(VeroScript::Test::FEngine::RunFile("veroscript-module:/app/names.vs", Host, Config).bSuccess &&
			        Output.str() == "42\n",
			    "environment-owned annotation policies reach Engine processors and reflection");
			if (Failures)
			{
				std::cerr << Errors.str();
			}
		}

		// Keep the definition identity and source fixed: the policy itself must invalidate the cache.
		const auto CacheDirectory = PathUtil::ToUtf8(Directory / "policy-change-cache");
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = EExecutionBackend::Vm;
		Config.bUseBytecodeCache = true;
		Config.BytecodeCacheDirectory = CacheDirectory;
		Config.CompileProcessing.AnnotationDefinitions = GetAnnotationDefinitions(*Catalog);
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		const std::string Probe = "@gEnErAtE struct Item{};void main(){print(type<Item>().has_annotation(\"GENERATE\"));}";
		const auto Cache = PathUtil::FromUtf8(VeroScriptBytecodeIO::GetCachePathForScript(Label, Config.BytecodeCacheDirectory));
		std::filesystem::file_time_type Stamp;
		bool HasPreviousPolicy = false, PreviousPolicy = false;
		for (const bool Sensitive : {false, false, true, true, false})
		{
			Config.CompileProcessing.AnnotationDefinitions.front().CaseSensitive = Sensitive;
			Output.str("");
			Require(VeroScript::Test::FEngine::RunSource(Probe, Label, Host, ERunMode::Script, Config).bSuccess && Output.str() == (Sensitive ? "false\n" : "true\n"), "policy changes cannot reuse stale annotation metadata from bytecode cache");
			Require(std::filesystem::exists(Cache), "annotation policy probe exercises persistent bytecode caching");
			std::error_code CacheError;
			if (HasPreviousPolicy && PreviousPolicy == Sensitive)
			{
				Require(std::filesystem::last_write_time(Cache, CacheError) == Stamp && !CacheError,
				    "an unchanged annotation policy reuses the bytecode cache");
			}

			std::filesystem::last_write_time(
			    Cache, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24), CacheError);
			if (!CacheError)
			{
				Stamp = std::filesystem::last_write_time(Cache, CacheError);
			}

			Require(!CacheError, "record annotation policy cache timestamp");
			PreviousPolicy = Sensitive;
			HasPreviousPolicy = true;
		}
	}

	void TestHostPaths(const std::filesystem::path& Directory,
	    const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Entry = Directory / "annotations.vs";
		const auto Write = [&](const std::string& Text)
		{
			std::ofstream File(Entry);
			File << Text;
			File.close();
			return bool(File);
		};
		if (!Require(Write(AnnotationSource), "write annotation source")) return;
		const auto Label = PathUtil::ToUtf8(Entry);
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}

			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			int Dependency = 21;
			unsigned Calls = 0;
			FEngineConfig Config;
			VeroScript::Test::Configure(Config);
			Config.ExecutionBackend = Backend;
			Config.JitBackendFactory = Factory;
			Config.bUseBytecodeCache = true;
			const auto CacheDirectory = PathUtil::ToUtf8(Directory / "cache");
			Config.BytecodeCacheDirectory = CacheDirectory;
			Config.AnnotationProcessing.ReadDependency = [&](std::string_view, std::string& Content)
			{
				Content = std::to_string(Dependency);
				return true;
			};
			Config.AnnotationProcessing.Registry = Registry(
			    [&](const auto& Context, auto& Generated)
			{
				++Calls;
				std::string Content;
				if (!Context.ReadDependency("cpp-annotation-input", Content))
				{
					throw std::runtime_error("missing input");
				}

				Generated.SubmitMethod(
				    Context.Declaration.Id, EMemberAccess::Public, "int Read(){return " + Content + ";}");
				ReportNotes(Generated);
			});
			Require(HasNotes(VeroScript::Test::FEngine::CheckSource(AnnotationSource, Label, Host, Config)), "CheckSource retains annotation notes");
			Require(HasNotes(VeroScript::Test::FEngine::CheckFile(Label, Host, Config)), "CheckFile retains annotation notes");
			const auto Cache = PathUtil::FromUtf8(VeroScriptBytecodeIO::GetCachePathForScript(Label, Config.BytecodeCacheDirectory));
			for (const bool FromFile : {false, true})
			{
				std::filesystem::file_time_type Stamp;
				for (unsigned Run = 0; Run < 2; ++Run)
				{
					Output.str("");
					Errors.str("");
					const auto Before = Calls;
					const auto Result = FromFile ? VeroScript::Test::FEngine::RunFile(Label, Host, Config)
					                             : VeroScript::Test::FEngine::RunSource(AnnotationSource, Label, Host, ERunMode::Script, Config);
					Require(HasNotes(Result) && Output.str() == "22\n", "Engine run retains successful preparation diagnostics");
					Require(Calls == Before + 1, "one preparation per Engine run, including cached runs");
					Require(Errors.str().find("cpp annotation note") != std::string::npos, "Engine streams nonfatal diagnostics");
					std::error_code Error;
					if (Run == 0)
					{
						std::filesystem::last_write_time(Cache,
						    std::filesystem::file_time_type::clock::now() - std::chrono::hours(24), Error);
						if (!Error)
						{
							Stamp = std::filesystem::last_write_time(Cache, Error);
						}

						Require(!Error, "annotation cache persisted");
					}
					else
					{
						Require(std::filesystem::last_write_time(Cache, Error) == Stamp && !Error,
						    "annotation cache reused");
					}
				}
			}
			Dependency = 42;
			Output.str("");
			Require(HasNotes(VeroScript::Test::FEngine::RunFile(Label, Host, Config)) && Output.str() == "43\n",
			    "annotation dependency changes invalidate bytecode cache");
			const auto Failed = VeroScript::Test::FEngine::RunSource(
			    "@Generate struct Generated {}; int divisor=0; void main(){print(1/divisor);}", Label, Host, ERunMode::Script, Config);
			Require(!Failed.bSuccess && Failed.Diagnostics.size() == 4 && Failed.Diagnostics.front().Severity == ECompileDiagnosticSeverity::Warning && Failed.Diagnostics.back().Severity == ECompileDiagnosticSeverity::Error && !Failed.Error.Message.empty(), "runtime failure retains annotation notes and the primary execution error");
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.ApplyConfig(Config);
			Require(HasNotes(Session.LoadSource(AnnotationSource, Label)), "Session LoadSource retains annotation notes");
			Require(HasNotes(Session.LoadFile(Label)), "Session LoadFile retains annotation notes");
			Session.SetHotReloadEnabled(true);
			Require(Session.Run(ERunMode::Eval).bSuccess && Session.SetGlobal("kept", std::int32_t{10}).bSuccess,
			    "initialize annotation session state");
			Dependency = 50;
			Require(HasNotes(Session.Reload()) && Session.Run(ERunMode::Eval).bSuccess && CallReturns(Session, "sample", 60),
			    "annotation reload migrates globals and retains notes");
			auto Budget = Config.AnnotationProcessing;
			Budget.MaxOperations = 0;
			Session.SetAnnotationProcessing(Budget);
			Require(!Session.Reload().bSuccess && CallReturns(Session, "sample", 60), "failed annotation reload preserves active program");
			Session.SetAnnotationProcessing(Config.AnnotationProcessing);
			if (!Require(
			        Write(
			            "@Generate struct Generated {}; int sample(){Generated x=Generated();return x.Read();}"),
			        "write incompatible annotation replacement"))
			{
				return;
			}

			const auto Migration = Session.Reload();
			Require(!Migration.bSuccess && Migration.Diagnostics.size() == 4 && Migration.Diagnostics.front().Severity == ECompileDiagnosticSeverity::Warning && Migration.Diagnostics.back().Severity == ECompileDiagnosticSeverity::Error && Migration.Error.MessageId == "diag.hot_reload_state_migration_failed" && CallReturns(Session, "sample", 60), "failed migration retains preparation notes and the prior program");
			if (!Require(Write(AnnotationSource), "restore annotation source")) return;
			const auto UncacheableDirectory = PathUtil::ToUtf8(Directory / "uncacheable");
			Config.BytecodeCacheDirectory = UncacheableDirectory;
			Config.AnnotationProcessing.Registry = Registry(
			    [](const auto& Context, auto& Generated)
			{
				Generated.SubmitMethod(
				    Context.Declaration.Id, EMemberAccess::Public, "int Read(){return 7;}");
			},
			    false);
			Output.str("");
			Require(VeroScript::Test::FEngine::RunSource(AnnotationSource, Label, Host, ERunMode::Script, Config).bSuccess && Output.str() == "8\n" && !std::filesystem::exists(PathUtil::FromUtf8(Config.BytecodeCacheDirectory)),
			    "uncacheable annotation processors bypass persistent bytecode storage");
		}
	}
	void TestCompilationCallbacks(const std::filesystem::path& Directory)
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		const auto Path = Directory / "callback.vs";
		const auto Label = PathUtil::ToUtf8(Path);
		const auto Write = [&](const std::string& Text)
		{
			std::ofstream File(Path);
			File << Text;
			File.close();
			return bool(File);
		};
		bool ReentrantCallSucceeded = false;
		bool ReentrantConfigChanged = false;
		FAnnotationProcessingOptions Options;
		Options.Registry = Registry(
		    [&](const auto&, auto& Generated)
		{
			ReentrantCallSucceeded = CallReturns(Session, "old", 42);
			Session.Clear();
			const auto Before = Session.GetAnnotationProcessing().Registry;
			Session.SetAnnotationProcessing({});
			ReentrantConfigChanged = Session.GetAnnotationProcessing().Registry != Before;
			FScriptError Error;
			Error.Message = "reject replacement";
			Generated.ReportDiagnostic(std::move(Error));
		});
		for (unsigned Action = 0; Action < 4; ++Action)
		{
			Session.SetAnnotationProcessing({});
			if (!Require(Write("int old(){return 42;}") && Session.LoadFile(Label).bSuccess, "load prior callback program")) return;
			Session.SetAnnotationProcessing(Options);
			if (!Require(Write(AnnotationSource), "write replacement callback program")) return;
			ReentrantCallSucceeded = ReentrantConfigChanged = false;
			std::unique_ptr<FBytecodeModule> Compiled;
			const auto Result = Action == 0 ? Session.LoadSource(AnnotationSource, Label) : Action == 1 ? Session.LoadFile(Label)
			    : Action == 2                                                                           ? Session.CompileFile(Label, Compiled)
			                                                                                            : Session.Reload();
			Require(!Result.bSuccess, "reject annotation replacement");
			Require(!ReentrantCallSucceeded && !ReentrantConfigChanged && CallReturns(Session, "old", 42),
			    "compilation callbacks cannot mutate or execute Session");
		}
		Options = {};
		Options.CheckCancelled = []
		{
			throw FAnalysisCancelled{};
		};
		Session.SetAnnotationProcessing(Options);
		bool Cancelled = false;
		try
		{
			Session.LoadSource("int replacement(){return 7;}", "cancelled.vs");
		}
		catch (const FAnalysisCancelled&)
		{
			Cancelled = true;
		}
		Session.SetAnnotationProcessing({});
		Require(Cancelled && CallReturns(Session, "old", 42), "cancellation restores Session callback guards and prior program");
		Require(Session.LoadSource("int next(){return 7;}", "next.vs").bSuccess && CallReturns(Session, "next", 7),
		    "Session remains reusable after cancelled preparation");
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
	FTempDirectory Directory;
	TestAnnotationNamePolicy(Directory.Path, Factory);
	TestHostPaths(Directory.Path, Factory);
	TestCompilationCallbacks(Directory.Path);
	return Failures ? 1 : 0;
}
