// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptEngine.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

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
	using FCallback = std::function<void(const FCompileProcessingContext&, FCompileProcessingOutput&)>;
	struct FProcessor final : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			++Calls[static_cast<unsigned>(C.Phase)];
			if (C.Phase >= ECompileProcessingPhase::Types && (Calls[0] != 1 || Calls[1] != 1))
			{
				throw std::runtime_error("source processor state was lost");
			}

			Callback(C, O);
		}

		explicit FProcessor(FCallback C) : Callback(std::move(C))
		{
		}

		FCallback Callback;

		std::array<unsigned, 5> Calls{};
	};
	std::shared_ptr<const FCompileProcessorRegistry> Registry(FCallback Callback, bool Cacheable = true)
	{
		auto R = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc D;
		D.ProcessorId = "cpp.general";
		D.Version = "1";
		D.Phases = 31;
		D.Operations = CompileOperationMask(ECompileOperation::EditSource) | CompileOperationMask(ECompileOperation::AddDeclaration) | CompileOperationMask(ECompileOperation::CompleteBody);
		D.Subscriptions = {{ECompileProcessingEvent::Module}};
		D.Cacheable = Cacheable;
		D.Factory = [Callback = std::move(Callback)]
		{
			return std::make_unique<FProcessor>(Callback);
		};
		std::string Error;
		if (!R->Register(std::move(D), Error))
		{
			throw std::runtime_error(Error);
		}

		R->Freeze();
		return R;
	}
	bool Returns(VeroScriptSession& S, const char* Name, int Expected)
	{
		FValue V;
		return S.CallFunction(Name, {}, &V).bSuccess && std::get_if<std::int32_t>(&V) && std::get<std::int32_t>(V) == Expected;
	}
	const std::string Source = "CUSTOM Original{};int kept=ACTIVE;int Pending();"
	                           "int sample(){Generated g=Generated();return Original::Read()+Pending()+kept+g.value;}"
	                           "void main(){print(sample());}";
	void TestHostPaths(const std::filesystem::path& Directory, EExecutionBackend Backend,
	    const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const auto Entry = Directory / "processing.vs";
		{
			std::ofstream F(Entry);
			F << Source;
		}
		const auto Label = PathUtil::ToUtf8(Entry);
		const auto CacheRoot = PathUtil::ToUtf8(Directory / "cache");
		int Dependency = 21;
		std::array<unsigned, 5> Calls{};
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.ExecutionBackend = Backend;
		Config.JitBackendFactory = Factory;
		Config.bUseBytecodeCache = true;
		Config.BytecodeCacheDirectory = CacheRoot;
		Config.CompileProcessing.ReadDependency = [&](std::string_view, std::string& Text)
		{
			Text = std::to_string(Dependency);
			return true;
		};
		Config.CompileProcessing.Registry = Registry(
		    [&](const auto& C, auto& O)
		{
			++Calls[static_cast<unsigned>(C.Phase)];
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				const auto P = C.Source->Text.find("CUSTOM");
				if (P != std::string::npos)
				{
					FSourceRange R;
					R.SourceId = C.Source->Id;
					R.Begin = P;
					R.End = P + 6;
					O.EditSource(R, "class");
				}
			}
			else if (C.Phase == ECompileProcessingPhase::ActiveSource)
			{
				const auto P = C.Source->Text.find("ACTIVE");
				if (P != std::string::npos)
				{
					FSourceRange R;
					R.SourceId = C.Source->Id;
					R.Begin = P;
					R.End = P + 6;
					O.EditSource(R, "1");
				}
			}
			else if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "struct Generated{int value=2;};");
			}

			else if (C.Phase == ECompileProcessingPhase::Members)
			{
				std::string Text;
				if (!C.ReadDependency("cpp-input", Text))
				{
					throw std::runtime_error("missing dependency");
				}

				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Original" && D.Target == EAnnotationTarget::Class)
					{
						O.AddDeclaration({C.ModuleId, "", D.Id, C.Snapshot},
						    "public:static int Read(){return " + Text + ";}");
					}
				}

				FScriptError Note;
				Note.Message = "general processing note";
				Note.Severity = ECompileDiagnosticSeverity::Warning;
				O.ReportDiagnostic(std::move(Note));
			}
			else if (C.Phase == ECompileProcessingPhase::Bodies)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.Name == "Pending" && !D.bHasBody)
					{
						O.CompleteBody(D.Id, C.Snapshot, "{return 3;}");
					}
				}
			}
		});
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		for (const bool File : {false, true})
		{
			const auto Check = File ? VeroScript::Test::FEngine::CheckFile(Label, Host, Config)
			                        : VeroScript::Test::FEngine::CheckSource(Source, Label, Host, Config);
			Require(Check.bSuccess && Check.Diagnostics.size() == 1, "Engine Check processing: " + Check.Error.Message);
			std::filesystem::file_time_type Stamp;
			for (unsigned Run = 0; Run < 2; ++Run)
			{
				const auto Before = Calls;
				Out.str("");
				const auto R = File ? VeroScript::Test::FEngine::RunFile(Label, Host, Config)
				                    : VeroScript::Test::FEngine::RunSource(Source, Label, Host, ERunMode::Script, Config);
				Require(R.bSuccess && Out.str() == "27\n" && R.Diagnostics.size() == 1,
				    "Engine Run processing: " + R.Error.Message);
				for (unsigned P = 0; P < 5; ++P)
				{
					Require(Calls[P] == Before[P] + 1, "one invocation per phase, including cache hits");
				}

				const auto Cache =
				    PathUtil::FromUtf8(VeroScriptBytecodeIO::GetCachePathForScript(Label, CacheRoot));
				std::error_code E;
				if (Run == 0)
				{
					std::filesystem::last_write_time(
					    Cache, std::filesystem::file_time_type::clock::now() - std::chrono::hours(24), E);
					Stamp = std::filesystem::last_write_time(Cache, E);
				}
				else
				{
					Require(std::filesystem::last_write_time(Cache, E) == Stamp && !E,
					    "general processing reuses bytecode cache");
				}
			}
		}
		Dependency = 42;
		Out.str("");
		Require(VeroScript::Test::FEngine::RunFile(Label, Host, Config).bSuccess && Out.str() == "48\n", "dependency changes expire bytecode cache");
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Require(Session.GetCompileProcessing().Registry == Config.CompileProcessing.Registry, "ApplyConfig retains processing options");
		Require(Session.LoadSource(Source, Label).bSuccess, "Session LoadSource");
		Require(Session.LoadFile(Label).bSuccess, "Session LoadFile");
		std::unique_ptr<FBytecodeModule> M;
		Require(Session.CompileFile(Label, M).bSuccess && M && M->SourceMetadata &&
		        std::any_of(M->SourceMetadata->Nodes.begin(), M->SourceMetadata->Nodes.end(),
		            [](const auto& N)
		{
			return N.Name == "Generated" && N.Origin.ProcessorId == "cpp.general";
		}),
		    "Session CompileFile preserves generated source metadata");
		Session.SetHotReloadEnabled(true);
		Require(Session.Run(ERunMode::Eval).bSuccess && Session.SetGlobal("kept", std::int32_t{10}).bSuccess, "initialize Session globals");
		Dependency = 50;
		Require(Session.Reload().bSuccess && Session.Run(ERunMode::Eval).bSuccess && Returns(Session, "sample", 65), "processing hot reload migrates state");
		auto Budget = Config.CompileProcessing;
		Budget.MaxOperations = 0;
		Session.SetCompileProcessing(Budget);
		Require(!Session.Reload().bSuccess && Returns(Session, "sample", 65), "failed processing reload preserves active program");
		Session.SetCompileProcessing(Config.CompileProcessing);
		const auto Uncacheable = PathUtil::ToUtf8(Directory / "uncacheable");
		Config.BytecodeCacheDirectory = Uncacheable;
		Config.CompileProcessing.Registry = Registry(
		    [](const auto&, auto&)
		{
		},
		    false);
		Require(
		    VeroScript::Test::FEngine::RunSource("void main(){}", Label, Host, ERunMode::Script, Config).bSuccess &&
		        !std::filesystem::exists(Directory / "uncacheable"),
		    "noncacheable processing skips persistent storage");
	}
	void TestCallbacks()
	{
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		VeroScriptSession S(Host);
		VeroScript::Test::Configure(S);
		Require(S.LoadSource("int old(){return 42;}", "old.vs").bSuccess, "load previous program");
		bool Invoked = false;
		FCompileProcessingOptions P;
		P.Registry = Registry(
		    [&](const auto& C, auto& O)
		{
			if (C.Phase != ECompileProcessingPhase::RawSource) return;
			Invoked = true;
			Require(!Returns(S, "old", 42), "source callback cannot execute Session");
			S.Clear();
			S.SetCompileProcessing({});
			Require(S.GetCompileProcessing().Registry == P.Registry,
			    "source callback cannot mutate Session config");
			FScriptError Note;
			Note.Message = "source processing note";
			Note.Severity = ECompileDiagnosticSeverity::Warning;
			O.ReportDiagnostic(std::move(Note));
			FScriptError E;
			E.Message = "reject replacement";
			O.ReportDiagnostic(std::move(E));
		});
		S.SetCompileProcessing(P);
		const auto Failed = S.LoadSource("void main(){}", "new.vs");
		Require(!Failed.bSuccess && Invoked && Returns(S, "old", 42), "source processing failure preserves program");
		Require(Failed.Error.Severity == ECompileDiagnosticSeverity::Error &&
		        Failed.Error.Message == "reject replacement" && Failed.Diagnostics.size() == 2,
		    "source processing notes do not replace the primary error");
		P = {};
		P.CheckCancelled = []
		{
			throw FAnalysisCancelled{};
		};
		S.SetCompileProcessing(P);
		bool Cancelled = false;
		try
		{
			S.LoadSource("void main(){}", "cancel.vs");
		}
		catch (const FAnalysisCancelled&)
		{
			Cancelled = true;
		}
		S.SetCompileProcessing({});
		Require(Cancelled && Returns(S, "old", 42), "cancellation restores compilation guard");
		Require(S.LoadSource("int next(){return 7;}", "next.vs").bSuccess && Returns(S, "next", 7), "Session reusable after cancellation");
	}
	void TestMetadata(const std::filesystem::path& Directory)
	{
		const auto Cache = PathUtil::ToUtf8(Directory / "metadata-cache");
		const auto Label = PathUtil::ToUtf8(Directory / "metadata.vs");
		const std::string Text = "/// Type docs\n@Tag struct Documented{};void main(){print(type<Documented>().annotation_count());print(type<Documented>().documentation());}";
		FEngineConfig C;
		VeroScript::Test::Configure(C);
		C.ExecutionBackend = EExecutionBackend::Vm;
		C.bUseBytecodeCache = true;
		C.BytecodeCacheDirectory = Cache;
		std::ostringstream Out, Err;
		FStreamEngineHost H(Out, Err);
		for (unsigned I = 0; I < 3; ++I)
		{
			C.CompileProcessing.CommentRetention = I == 1 ? ECommentRetention::NoComments : ECommentRetention::Full;
			Out.str("");
			const auto R = VeroScript::Test::FEngine::RunSource(Text, Label, H, ERunMode::Script, C);
			Require(R.bSuccess && Out.str() == (I == 1 ? "1\n\n" : "1\nType docs\n"), "runtime metadata and retention cache key: " + R.Error.Message);
		}
	}
	void TestPackageBudgets(const std::filesystem::path& Directory)
	{
		// Charge baked source edits and replayed declarations/bodies exactly once,
		// including when a consumer also generates source and a function body.
		const auto ProducerFile = Directory / "budget-producer.vs";
		const auto PackageFile = Directory / "ProcessingBudget.vslib";
		{
			std::ofstream F(ProducerFile);
			F << "CUSTOM T Build<T>(T x);";
		}
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		VeroScriptSession Producer(Host);
		VeroScript::Test::Configure(Producer);
		FCompileProcessingOptions Processing;
		Processing.Registry = Registry(
		    [](const auto& C, auto& O)
		{
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				FSourceRange R;
				R.SourceId = C.Source->Id;
				R.Begin = 0;
				R.End = 6;
				O.EditSource(R, "export");
			}
			else if (C.Phase == ECompileProcessingPhase::Types)
			{
				O.AddDeclaration(
				    {C.ModuleId, "", "", C.Snapshot}, "export struct Generated<T>{T value;};");
			}

			else if (C.Phase == ECompileProcessingPhase::Bodies)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "Build")
					{
						O.CompleteBody(D.Id, C.Snapshot,
						    "{Generated<T> g=Generated<T>();g.value=x;return g.value;}");
					}
				}
			}
		});
		Producer.SetCompileProcessing(Processing);
		std::unique_ptr<FBytecodeModule> Module;
		const auto Compiled = Producer.CompileFile(PathUtil::ToUtf8(ProducerFile), Module);
		Require(Compiled.bSuccess && Module, "package budget producer: " + Compiled.Error.Message);
		if (!Compiled.bSuccess || !Module) return;
		FPackageBuildOptions Build;
		Build.Kind = EPackageKind::Library;
		Build.PackageName = "ProcessingBudget";
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		const bool Built = BuildPackage(*Module, Build, Bytes, Error);
		Require(Built, "build processing budget package: " + Error);
		if (!Built) return;
		FPackageData Package;
		const bool Read = ReadPackage(FByteView(Bytes), Package, Error);
		Require(Read && Package.Module, "read processing budget package: " + Error);
		if (!Read || !Package.Module) return;
		std::size_t ReplayCount = 0, ReplayBytes = 0;
		for (const auto& M : Package.Module->PackageTemplateModules)
		{
			for (const auto& O : M.CompileGeneratedOperations)
			{
				++ReplayCount;
				ReplayBytes += O.Source.size();
			}
		}

		Require(ReplayCount == 3, "package retains source edit, declaration and body operations");
		{
			std::ofstream F(PackageFile, std::ios::binary);
			F.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
		}
		const std::string Import = "import \"" + PackageFile.generic_string() + "\";";
		const auto Label = PathUtil::ToUtf8(Directory / "budget-consumer.vs");
		const auto Run =
		    [&](FEngineConfig Config, const std::string& Source, bool Success, const std::string& Name)
		{
			Config.ExecutionBackend = EExecutionBackend::Vm;
			Config.bUseBytecodeCache = false;
			Out.str("");
			const auto R = VeroScript::Test::FEngine::RunSource(Import + Source, Label, Host, ERunMode::Script, Config);
			Require(Success ? R.bSuccess && Out.str() == "4\n" : !R.bSuccess && R.Error.Message.find("budget exceeded") != std::string::npos,
			    Name + ": " + R.Error.Message);
		};
		const std::string Pure = "void main(){print(Build<int>(4));}";
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.CompileProcessing.MaxOperations = ReplayCount;
		Config.CompileProcessing.MaxSourceBytes = ReplayBytes;
		Run(Config, Pure, true, "exact replay budget accepts all package operations");
		--Config.CompileProcessing.MaxOperations;
		Run(Config, Pure, false, "replay operation count includes baked source edits");
		++Config.CompileProcessing.MaxOperations;
		--Config.CompileProcessing.MaxSourceBytes;
		Run(Config, Pure, false, "replay source bytes enforce exact boundary");
		Config = {};
		Config.AnnotationProcessing.MaxOperations = ReplayCount - 1;
		Run(Config, Pure, false, "legacy operation limit applies without producer registry");
		Config = {};
		Config.AnnotationProcessing.MaxSourceBytes = ReplayBytes - 1;
		Run(Config, Pure, false, "legacy source byte limit applies without producer registry");

		const std::string Body = "{return 3;}";
		Config = {};
		Config.CompileProcessing.Registry = Registry(
		    [&](const auto& C, auto& O)
		{
			if (C.Phase == ECompileProcessingPhase::RawSource)
			{
				const auto P = C.Source->Text.find("ACTIVE");
				if (P != std::string::npos)
				{
					FSourceRange R;
					R.SourceId = C.Source->Id;
					R.Begin = P;
					R.End = P + 6;
					O.EditSource(R, "1");
				}
			}
			else if (C.Phase == ECompileProcessingPhase::Bodies)
			{
				for (const auto& D : C.Declarations)
				{
					if (D.ModulePath == C.ModuleId && D.Name == "Pending")
					{
						O.CompleteBody(D.Id, C.Snapshot, Body);
					}
				}
			}
		});
		const std::string Mixed = "int kept=ACTIVE;int Pending();void main(){print(Build<int>(kept)+Pending());}";
		Config.CompileProcessing.MaxOperations = ReplayCount + 2;
		Config.CompileProcessing.MaxSourceBytes = ReplayBytes + 1 + Body.size();
		Run(Config, Mixed, true, "exact combined budget accepts source, package replay and body generation");
		--Config.CompileProcessing.MaxOperations;
		Run(Config, Mixed, false, "live body generation respects reserved package operations");
		++Config.CompileProcessing.MaxOperations;
		--Config.CompileProcessing.MaxSourceBytes;
		Run(Config, Mixed, false, "live body generation respects reserved package source bytes");
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
	const auto Directory = std::filesystem::temp_directory_path() /
	    ("veroscript_cpp_processing_" +
	        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(Directory);
	for (const auto Backend : {VeroScript::EExecutionBackend::Vm, VeroScript::EExecutionBackend::Mixed, VeroScript::EExecutionBackend::Jit})
	{
		if (Backend == VeroScript::EExecutionBackend::Jit && !Factory)
		{
			continue;
		}

		const auto BackendDirectory = Directory / std::to_string(static_cast<unsigned>(Backend));
		std::filesystem::create_directories(BackendDirectory);
		TestHostPaths(BackendDirectory, Backend, Factory);
	}
	TestCallbacks();
	TestMetadata(Directory);
	TestPackageBudgets(Directory);
	std::error_code E;
	std::filesystem::remove_all(Directory, E);
	return Failures ? 1 : 0;
}
