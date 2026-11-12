// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropAnnotation.h"
#include "VeroScriptInteropCompileProcessing.h"
#include "VeroScriptInteropModuleEnvironment.h"
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace
{
	bool Expect(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAILED: " << Message << '\n';
		}

		return Condition;
	}
	vs_interop_string_view Text(const char* Value)
	{
		return {Value, std::strlen(Value)};
	}
	struct FProbe
	{
		vs_interop_session* Session = nullptr;
		bool ReportError = false;
		bool ReentryRejected = false;
		bool ReleaseReentryRejected = false;
		bool ConfigProbeArmed = false;
		bool FailConfigCopy = false;
		bool ConfigCopyReentryRejected = true;
		bool ConfigReleaseReentryRejected = true;
		bool ExecutionCopyProbeArmed = false;
		bool ExecutionReleaseProbeArmed = false;
		bool CheckingExecutionReentry = false;
		bool ExecutionReentryRejected = true;
		bool ExecutionFailureResultsValid = true;
		unsigned ExecutionReentryChecks = 0;
		unsigned ConfigCopyChecks = 0;
		unsigned ConfigReleaseChecks = 0;
	};
	struct FReleaseProbe
	{
		~FReleaseProbe()
		{
			Probe.ReleaseReentryRejected = vs_interop_session_set_annotation_service(Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
		}

		FProbe& Probe;
	};
	struct FConfigProbe
	{
		static void CheckExecutionReentry(FProbe& Probe, bool& Armed)
		{
			if (!Armed || Probe.CheckingExecutionReentry) return;
			Armed = false;
			Probe.CheckingExecutionReentry = true;
			vs_interop_run_result Result{};
			auto SeedResult = [&]
			{
				Result.struct_size = sizeof(Result);
				Result.success = Result.exit_requested = 1;
				Result.reserved_u8[0] = Result.reserved_u8[1] = 255;
				Result.exit_code = 99;
				Result.diagnostic_count = 123;
			};
			auto CheckResult = [&]
			{
				vs_interop_diagnostic Diagnostic{};
				Diagnostic.struct_size = sizeof(Diagnostic);
				Probe.ExecutionFailureResultsValid &= Result.struct_size == sizeof(Result) && !Result.success && !Result.exit_requested && Result.exit_code != 0 && !Result.reserved_u8[0] && !Result.reserved_u8[1] && Result.diagnostic_count == 1 && vs_interop_session_diagnostic_count(Probe.Session) == 1 && vs_interop_session_get_diagnostic(Probe.Session, 0, &Diagnostic) == VS_INTEROP_STATUS_OK && std::string_view(Diagnostic.message.data, Diagnostic.message.size).find("reentry") != std::string_view::npos;
			};
			SeedResult();
			Probe.ExecutionReentryRejected &= vs_interop_session_call_function(
			                                      Probe.Session, Text("PreservedFunction"), nullptr, 0, nullptr, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR;
			CheckResult();
			SeedResult();
			Probe.ExecutionReentryRejected &= vs_interop_session_run(
			                                      Probe.Session, VS_INTEROP_RUN_EVAL_ONCE, &Result) == VS_INTEROP_STATUS_ENGINE_ERROR;
			CheckResult();
			SeedResult();
			Probe.ExecutionReentryRejected &= vs_interop_session_load_source(
			                                      Probe.Session, Text("int Reentered(){return 9;}"), Text("reentered.vs"), &Result) == VS_INTEROP_STATUS_ENGINE_ERROR;
			CheckResult();
			Probe.ExecutionReentryRejected &= vs_interop_session_run(
			                                      Probe.Session, VS_INTEROP_RUN_EVAL_ONCE, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			Probe.ExecutionFailureResultsValid &= vs_interop_session_diagnostic_count(Probe.Session) == 1;
			++Probe.ExecutionReentryChecks;
			Probe.CheckingExecutionReentry = false;
		}

		FConfigProbe(FProbe& InProbe, std::shared_ptr<FReleaseProbe> InOwner)
		    : Probe(InProbe), Owner(std::move(InOwner))
		{
		}

		FConfigProbe(const FConfigProbe& Other) : Probe(Other.Probe), Owner(Other.Owner)
		{
			if (Probe.FailConfigCopy)
			{
				throw std::bad_alloc();
			}

			CheckExecutionReentry(Probe, Probe.ExecutionCopyProbeArmed);
			if (Probe.ConfigProbeArmed)
			{
				++Probe.ConfigCopyChecks;
				Probe.ConfigCopyReentryRejected &= vs_interop_session_set_annotation_service(Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			}
		}

		~FConfigProbe()
		{
			CheckExecutionReentry(Probe, Probe.ExecutionReleaseProbeArmed);
			if (Probe.ConfigProbeArmed)
			{
				++Probe.ConfigReleaseChecks;
				Probe.ConfigReleaseReentryRejected &= vs_interop_session_set_annotation_service(Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			}
		}

		void operator()() const
		{
		}

		FProbe& Probe;

		std::shared_ptr<FReleaseProbe> Owner;
	};
	struct FGetter final : VeroScript::IAnnotationProcessor
	{
		void Process(const VeroScript::FAnnotationContext& Context,
		    VeroScript::FAnnotationOutput& Output) override
		{
			Probe.ReentryRejected = vs_interop_session_set_annotation_service(Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			for (const auto Severity : {VeroScript::ECompileDiagnosticSeverity::Warning,
			         VeroScript::ECompileDiagnosticSeverity::Information, VeroScript::ECompileDiagnosticSeverity::Hint})
			{
				VeroScript::FScriptError Diagnostic;
				Diagnostic.Severity = Severity;
				Diagnostic.Message = "annotation note";
				Output.ReportDiagnostic(std::move(Diagnostic));
			}
			if (Probe.ReportError)
			{
				VeroScript::FScriptError Diagnostic;
				Diagnostic.Message = "annotation primary error";
				Output.ReportDiagnostic(std::move(Diagnostic));
			}
			else
			{
				Output.SubmitMethod(
				    Context.Owner->Id, VeroScript::EMemberAccess::Public, "int Read(){return value;}");
			}
		}

		explicit FGetter(FProbe& InProbe) : Probe(InProbe)
		{
		}

		FProbe& Probe;
	};

	struct FGeneralProbe
	{
		vs_interop_session* Session = nullptr;
		bool ReentryRejected = false, ReleaseRejected = false, ArmCopy = false, FailCopy = false;
		bool CopyRejected = true;
		int Releases = 0;
	};
	struct FGeneralCopy
	{
		explicit FGeneralCopy(FGeneralProbe& P) : Probe(P)
		{
		}

		FGeneralCopy(const FGeneralCopy& Other) : Probe(Other.Probe)
		{
			if (Probe.FailCopy)
			{
				throw std::bad_alloc{};
			}

			if (Probe.ArmCopy)
			{
				Probe.CopyRejected &= vs_interop_session_set_compile_processing_service(
				                          Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			}
		}

		void operator()() const
		{
		}

		FGeneralProbe& Probe;
	};
	struct FGeneralProcessor final : VeroScript::ICompileProcessor
	{
		void Process(const VeroScript::FCompileProcessingContext& C, VeroScript::FCompileProcessingOutput& O) override
		{
			Probe.ReentryRejected = vs_interop_session_set_compile_processing_service(Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "int PipelineValue(){return 42;}");
		}

		explicit FGeneralProcessor(FGeneralProbe& P) : Probe(P)
		{
		}

		FGeneralProbe& Probe;
	};
	bool TestGeneralProcessing()
	{
		using namespace VeroScript;
		FGeneralProbe Probe;
		struct FCleanup
		{
			~FCleanup()
			{
				vs_interop_compile_processing_service_release(Service);
				vs_interop_compile_processing_service_release(Retained);
				vs_interop_value_destroy(Value);
				if (Session)
				{
					vs_interop_session_set_compile_processing_service(Session, nullptr);
					vs_interop_session_destroy(Session);
				}
			}

			vs_interop_session* Session = nullptr;

			vs_interop_value* Value = nullptr;

			vs_interop_compile_processing_service* Service = nullptr;

			vs_interop_compile_processing_service* Retained = nullptr;
		} Cleanup;
		if (!Expect((vs_interop_get_compile_capabilities() & VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING) != 0,
		        "general processing discovery"))
		{
			return false;
		}

		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessingOptions Options;
		Options.Registry = Registry;
		if (!Expect(Interop::CreateCompileProcessingService(Options, &Cleanup.Service) ==
		                VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		            !Cleanup.Service,
		        "general registry must be frozen"))
		{
			return false;
		}

		auto Lifetime = std::shared_ptr<int>(new int(0),
		    [&](int* Data)
		{
			++Probe.Releases;
			Probe.ReleaseRejected = vs_interop_session_set_compile_processing_service(
			                            Probe.Session, nullptr) == VS_INTEROP_STATUS_ENGINE_ERROR;
			delete Data;
		});
		FCompileProcessorDesc Desc;
		Desc.ProcessorId = "interop.pipeline";
		Desc.Version = "1";
		Desc.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
		Desc.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
		Desc.Subscriptions = {{ECompileProcessingEvent::Module}};
		Desc.Factory = [&, Lifetime]
		{
			return std::make_unique<FGeneralProcessor>(Probe);
		};
		std::string Error;
		if (!Expect(Registry->Register(std::move(Desc), Error), "register native general processor")) return false;
		// A moved-from std::function may retain its target in libc++.
		Desc.Factory = {};
		Registry->Freeze();
		Options.CheckCancelled = FGeneralCopy(Probe);
		if (!Expect(
		        Interop::CreateCompileProcessingService(Options, &Cleanup.Service) == VS_INTEROP_STATUS_OK &&
		            vs_interop_compile_processing_service_retain(Cleanup.Service, &Cleanup.Retained) ==
		                VS_INTEROP_STATUS_OK &&
		            vs_interop_session_create(nullptr, &Cleanup.Session) == VS_INTEROP_STATUS_OK,
		        "create/retain general service"))
		{
			return false;
		}

		Probe.Session = Cleanup.Session;
		if (!Expect(vs_interop_session_set_compile_processing_service(Cleanup.Session, Cleanup.Retained) ==
		            VS_INTEROP_STATUS_OK,
		        "attach general service"))
		{
			return false;
		}

		vs_interop_compile_processing_service_release(Cleanup.Service);
		Cleanup.Service = nullptr;
		vs_interop_compile_processing_service_release(Cleanup.Retained);
		Cleanup.Retained = nullptr;
		Registry.reset();
		Options = {};
		Lifetime.reset();
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		Probe.ArmCopy = true;
		if (!Expect(vs_interop_session_apply_config(Cleanup.Session, &Config) == VS_INTEROP_STATUS_OK &&
		            Probe.CopyRejected,
		        "ApplyConfig preserves general service and rejects copy reentry"))
		{
			return false;
		}

		Probe.FailCopy = true;
		const auto Failed = vs_interop_session_apply_config(Cleanup.Session, &Config);
		Probe.FailCopy = false;
		if (!Expect(Failed == VS_INTEROP_STATUS_OUT_OF_MEMORY &&
		            vs_interop_session_apply_config(Cleanup.Session, &Config) == VS_INTEROP_STATUS_OK,
		        "general service copy failure/retry"))
		{
			return false;
		}

		vs_interop_status Foreign = VS_INTEROP_STATUS_OK;
		std::thread Thread(
		    [&]
		{
			Foreign = vs_interop_session_set_compile_processing_service(Cleanup.Session, nullptr);
		});
		Thread.join();
		if (!Expect(Foreign == VS_INTEROP_STATUS_WRONG_THREAD, "general service creator-thread guard"))
		{
			return false;
		}
		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		if (!Expect(vs_interop_value_create(&Cleanup.Value) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_load_source(Cleanup.Session,
		                Text("int ReadPipeline(){return PipelineValue();}"), Text("pipeline.vs"),
		                &Result) == VS_INTEROP_STATUS_OK &&
		            Probe.ReentryRejected &&
		            vs_interop_session_call_function(Cleanup.Session, Text("ReadPipeline"), nullptr, 0,
		                Cleanup.Value, &Result) == VS_INTEROP_STATUS_OK,
		        "generate and invoke native general processing output"))
		{
			return false;
		}

		int32_t Answer = 0;
		if (!Expect(
		        vs_interop_value_get_int32(Cleanup.Value, &Answer) == VS_INTEROP_STATUS_OK && Answer == 42,
		        "general processor result"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_set_compile_processing_service(Cleanup.Session, nullptr) ==
		                VS_INTEROP_STATUS_OK &&
		            Probe.Releases == 1 && Probe.ReleaseRejected,
		        "general service final release and reentry"))
		{
			return false;
		}

		return Expect(vs_interop_session_load_source(Cleanup.Session,
		                  Text("int ReadPipeline(){return PipelineValue();}"), Text("pipeline.vs"),
		                  &Result) == VS_INTEROP_STATUS_ENGINE_ERROR,
		    "clearing general service disables processing");
	}
}

namespace
{
	bool EnvironmentAnswer(const VeroScript::FNativeCallContext&, VeroScript::FValue& Out, std::string&)
	{
		Out = std::int32_t{42};
		return true;
	}
	vs_interop_callback_result VS_INTEROP_CALL LocalAnswer(vs_interop_call_context*, vs_interop_value* Out, void*)
	{
		return vs_interop_value_set_int32(Out, 7) == VS_INTEROP_STATUS_OK
		    ? VS_INTEROP_CALLBACK_OK
		    : VS_INTEROP_CALLBACK_ERROR;
	}
	struct FEnvironmentResource final : VeroScript::IResourceProvider
	{
		bool Read(const VeroScript::FResourceRequest& Request, const VeroScript::FResourceWork& Work,
		    VeroScript::FResourceData& Out, std::string& Error) const override
		{
			if (Work.CheckCancelled)
			{
				Work.CheckCancelled();
			}

			const std::string Source = "int resource_answer(){return 0;}";
			if (Request.ProviderId != "assets" || Request.ModuleId != "data" || Request.Group != "code" || Request.Path != "answer.vs" || Source.size() > Work.MaxBytes)
			{
				Error = "unexpected environment resource request";
				return false;
			}
			Out.Bytes.assign(Source.begin(), Source.end());
			Out.ContentIdentity = "interop.resource.1";
			Out.bExists = true;
			++*Reads;
			return true;
		}

		explicit FEnvironmentResource(std::shared_ptr<std::atomic<unsigned>> InReads) : Reads(std::move(InReads))
		{
		}

		std::shared_ptr<std::atomic<unsigned>> Reads;
	};
	struct FEnvironmentProcessor final : VeroScript::ICompileProcessor
	{
		void Process(const VeroScript::FCompileProcessingContext& C, VeroScript::FCompileProcessingOutput& O) override
		{
			VeroScript::FResourceData Data;
			if (!C.ReadResource || !C.ReadResource({"assets", "data", "code", "answer.vs"}, Data) ||
			    !Data.bExists)
			{
				throw std::runtime_error("environment compilation resource unavailable");
			}

			O.AddDeclaration(
			    {C.ModuleId, "", "", C.Snapshot}, std::string(Data.Bytes.begin(), Data.Bytes.end()));
		}
	};
	bool TestRejectedEnvironmentConfiguration()
	{
		using namespace VeroScript;
		for (const bool HadControl : {false, true})
		{
			bool RejectBackend = true;
			unsigned ResolverCalls = 0;
			struct FCleanup
			{
				~FCleanup()
				{
					vs_interop_value_destroy(Value);
					vs_interop_module_environment_release(Environment);
					vs_interop_session_destroy(Session);
				}

				vs_interop_session* Session = nullptr;
				vs_interop_module_environment* Environment = nullptr;
				vs_interop_value* Value = nullptr;
			} C;
			vs_interop_config Config{};
			Config.struct_size = sizeof(Config);
			Config.execution_backend = VS_INTEROP_BACKEND_VM;
			Config.enable_execution_control = HadControl ? 1 : 0;
			if (!Expect(vs_interop_session_create(nullptr, &C.Session) == VS_INTEROP_STATUS_OK &&
			            vs_interop_value_create(&C.Value) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK,
			        "configuration rejection fixture"))
			{
				return false;
			}

			FModuleEnvironmentDesc Description;
			Description.EntryOwnerId = "app";
			Description.Scopes = {{"app", {}}};
			constexpr const char* Entry = "veroscript-module:/app/configuration.vs";
			Description.Modules = {{"configuration.vs", "app", Entry, "app", "interop.configuration.1",
			    std::string("int answer(){return 73;}")}};
			Description.ExecutionBackends.Identity = "interop.configuration.resolver.1";
			Description.ExecutionBackends.Vm = [&](std::string_view,
			    std::shared_ptr<const IVmBackendFactory>& Factory, std::string& Error)
			{
				++ResolverCalls;
				Factory.reset();
				Error = RejectBackend ? "backend rejected for C configuration" : "";
				return !RejectBackend;
			};
			std::string Error;
			const auto Environment = FModuleEnvironment::Create(std::move(Description), Error);
			if (!Expect(Environment &&
			            Interop::CreateModuleEnvironment(Environment, &C.Environment) == VS_INTEROP_STATUS_OK,
			        "configuration rejection environment"))
			{
				return false;
			}
			Config.enable_execution_control = HadControl ? 0 : 1;
			const auto Rejected = vs_interop_session_apply_config_with_environment(C.Session, &Config, C.Environment);
			const auto Cancellation = vs_interop_session_request_cancel(C.Session);
			std::cout << "backend rejection status=" << Rejected << ", cancellation=" << Cancellation
			          << ", previous control=" << HadControl << '\n';
			char Message[256]{};
			uint64_t Required = 0;
			if (!Expect(Rejected == VS_INTEROP_STATUS_ENGINE_ERROR && ResolverCalls == 1 &&
			            Cancellation == (HadControl ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_INVALID_ARGUMENT) &&
			            vs_interop_session_copy_last_error(C.Session, Message, sizeof(Message), &Required) == VS_INTEROP_STATUS_OK &&
			            std::strstr(Message, "backend rejected for C configuration"),
			        "rejected backend reports failure and preserves the installed cancellation token"))
			{
				return false;
			}

			RejectBackend = false;
			if (!Expect(vs_interop_session_apply_config_with_environment(C.Session, &Config, C.Environment) == VS_INTEROP_STATUS_OK &&
			            ResolverCalls == 2,
			        "retry applies the environment after its backend becomes available"))
			{
				return false;
			}
			vs_interop_run_result Result{};
			Result.struct_size = sizeof(Result);
			int32_t Answer = 0;
			if (!Expect(vs_interop_session_load_file(C.Session, Text(Entry), &Result) == VS_INTEROP_STATUS_OK &&
			            vs_interop_session_call_function(C.Session, Text("answer"), nullptr, 0, C.Value, &Result) == VS_INTEROP_STATUS_OK &&
			            Result.success && vs_interop_value_get_int32(C.Value, &Answer) == VS_INTEROP_STATUS_OK && Answer == 73 &&
			            vs_interop_session_request_cancel(C.Session) ==
			                (HadControl ? VS_INTEROP_STATUS_INVALID_ARGUMENT : VS_INTEROP_STATUS_OK),
			        "successful retry publishes the environment and its requested control policy"))
			{
				return false;
			}
		}
		return true;
	}

	bool TestModuleEnvironment()
	{
		using namespace VeroScript;
		int Releases = 0;
		vs_interop_status ReleaseReentry = VS_INTEROP_STATUS_OK;
		vs_interop_config Config{};
		Config.struct_size = sizeof(Config);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		Config.enable_execution_control = 1;
		struct FCleanup
		{
			~FCleanup()
			{
				vs_interop_buffer_destroy(PackageBytes);
				vs_interop_buffer_destroy(Json);
				vs_interop_value_destroy(Value);
				vs_interop_module_environment_release(Handle);
				vs_interop_module_environment_release(Retained);
				vs_interop_session_destroy(Session);
			}

			vs_interop_session* Session = nullptr;

			vs_interop_module_environment* Handle = nullptr;

			vs_interop_module_environment* Retained = nullptr;

			vs_interop_value* Value = nullptr;

			vs_interop_buffer* Json = nullptr;

			vs_interop_buffer* PackageBytes = nullptr;
		} C;
		vs_interop_session_options SessionOptions{};
		SessionOptions.struct_size = sizeof(SessionOptions);
		SessionOptions.diagnostic_callback = [](vs_interop_string_view Message, void*)
		{
			std::cerr.write(Message.data, static_cast<std::streamsize>(Message.size));
			std::cerr << '\n';
		};
		if (!Expect((vs_interop_get_capabilities() & VS_INTEROP_CAP_MODULE_ENVIRONMENT) != 0,
		        "module environment capability") ||
		    !Expect(vs_interop_session_create(&SessionOptions, &C.Session) == VS_INTEROP_STATUS_OK &&
		            vs_interop_value_create(&C.Value) == VS_INTEROP_STATUS_OK,
		        "environment session"))
		{
			return false;
		}

		struct FOwner
		{
			~FOwner()
			{
				++Releases;
				Reentry = vs_interop_session_apply_config(Session, Config);
			}

			int& Releases;

			vs_interop_status& Reentry;

			vs_interop_session* Session;

			vs_interop_config* Config;
		};
		auto Catalog = std::make_shared<FHostBindingRegistry>();
		Catalog->SetRegistrationOwner({}, {"native", {}, true});
		std::string Error;
		if (!Expect(Catalog->RegisterNative(
		                "native_answer", ENativeType::Int, {}, EnvironmentAnswer, nullptr, Error),
		        "environment native"))
		{
			return false;
		}

		Catalog->SetRegistrationOwner({});
		FModuleEnvironmentDesc Description;
		auto ResourceReads = std::make_shared<std::atomic<unsigned>>(0);
		Description.Resources.push_back({{}, std::make_shared<FEnvironmentResource>(ResourceReads),
		    "assets", "native", "interop.resources.1", {"data"}});
		auto Processors = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Processor;
		Processor.ProcessorId = "interop.environment.resources";
		Processor.Version = "1";
		Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
		Processor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
		Processor.Subscriptions = {{ECompileProcessingEvent::Module}};
		Processor.Factory = []
		{
			return std::make_unique<FEnvironmentProcessor>();
		};
		if (!Expect(Processors->Register(std::move(Processor), Error), "environment resource processor"))
		{
			return false;
		}
		Processors->Freeze();
		Description.CompileProcessing.Registry = Processors;
		Description.Owners.push_back(std::shared_ptr<FOwner>(new FOwner{Releases, ReleaseReentry, C.Session, &Config}));
		Description.EntryOwnerId = "app";
		Description.Scopes = {{"app", {"native"}}, {"native", {}}};
		constexpr const char* Entry = "veroscript-module:/app/main.vs";
		Description.Modules = {{"app.vs", "app", Entry, "app", "interop.environment.1",
		    std::string("export int entry(){return native_answer()+resource_answer();} int main(){return entry();}")}};
		Description.Bindings = Catalog;
		Description.AvailableDependencies = {{"native", "1", "interop.abi", "interop.build", "interop.content"}};
		auto Environment = FModuleEnvironment::Create(std::move(Description), Error);
		if (!Expect(static_cast<bool>(Environment), Error.c_str())) return false;
		std::weak_ptr<const FModuleEnvironment> Weak = Environment;
		if (!Expect(Interop::CreateModuleEnvironment({}, &C.Handle) == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		            !C.Handle &&
		            Interop::CreateModuleEnvironment(Environment, nullptr) ==
		                VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		            vs_interop_module_environment_retain(nullptr, &C.Retained) ==
		                VS_INTEROP_STATUS_INVALID_ARGUMENT &&
		            !C.Retained,
		        "environment creator argument validation"))
		{
			return false;
		}

		if (!Expect(Interop::CreateModuleEnvironment(Environment, &C.Handle) == VS_INTEROP_STATUS_OK &&
		            vs_interop_module_environment_retain(C.Handle, &C.Retained) == VS_INTEROP_STATUS_OK,
		        "environment independent handles"))
		{
			return false;
		}

		Environment.reset();
		Catalog.reset();
		vs_interop_status Foreign = VS_INTEROP_STATUS_OK;
		std::thread Thread(
		    [&]
		{
			Foreign = vs_interop_session_apply_config_with_environment(C.Session, &Config, C.Handle);
		});
		Thread.join();
		if (!Expect(Foreign == VS_INTEROP_STATUS_WRONG_THREAD, "foreign environment configuration")) return false;
		if (!Expect(vs_interop_session_apply_config_with_environment(C.Session, &Config, C.Handle) ==
		            VS_INTEROP_STATUS_OK,
		        "apply immutable environment"))
		{
			return false;
		}

		vs_interop_module_environment_release(C.Handle);
		C.Handle = nullptr;
		vs_interop_module_environment_release(C.Retained);
		C.Retained = nullptr;
		if (!Expect(!Weak.expired() && !Releases, "session retains environment after handle release")) return false;
		if (!Expect(vs_interop_session_export_bindings_json(C.Session, &C.Json) == VS_INTEROP_STATUS_OK,
		        "environment reflection uses the selected catalog"))
		{
			return false;
		}

		const auto Json = vs_interop_buffer_view(C.Json);
		if (!Expect(
		        std::string_view(reinterpret_cast<const char*>(Json.data), Json.size).find("native_answer") !=
		            std::string_view::npos,
		        "environment native is visible in reflection"))
		{
			return false;
		}

		vs_interop_host_function_desc Function{};
		Function.struct_size = sizeof(Function);
		Function.name = Text("hidden");
		Function.return_type.struct_size = sizeof(Function.return_type);
		Function.return_type.type = VS_INTEROP_NATIVE_INT32;
		Function.callback = LocalAnswer;
		if (!Expect(vs_interop_session_register_host_function(C.Session, &Function) ==
		            VS_INTEROP_STATUS_ENGINE_ERROR,
		        "published catalog rejects mutation with an engine status"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK,
		        "ordinary configuration preserves the selected environment"))
		{
			return false;
		}

		vs_interop_run_result Result{};
		Result.struct_size = sizeof(Result);
		if (!Expect(vs_interop_session_load_file(C.Session, Text(Entry), &Result) == VS_INTEROP_STATUS_OK &&
		            Result.success,
		        "C loads mapped memory source"))
		{
			return false;
		}

		if (!Expect(ResourceReads->load() != 0,
		        "C compilation uses the environment resource provider and processor"))
		{
			return false;
		}
		if (const auto* Backend = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
		{
			if (!Expect(vs_interop_session_set_jit_backend(C.Session, Text(Backend)) == VS_INTEROP_STATUS_OK,
			        "environment external JIT backend"))
			{
				return false;
			}

			Config.execution_backend = VS_INTEROP_BACKEND_JIT;
			if (!Expect(vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK,
			        "environment configuration preserves backend"))
			{
				return false;
			}
		}
		if (!Expect(
		        vs_interop_session_run(C.Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK &&
		            Result.success && Result.exit_code == 42,
		        "environment native execution"))
		{
			return false;
		}

		auto Rejected = Config;
		Rejected.enable_execution_control = 0;
		if (!Expect(vs_interop_session_apply_config_with_environment(C.Session, &Rejected, nullptr) ==
		                VS_INTEROP_STATUS_ENGINE_ERROR &&
		            vs_interop_session_request_cancel(C.Session) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_call_function(C.Session, Text("entry"), nullptr, 0, C.Value,
		                &Result) == VS_INTEROP_STATUS_ENGINE_ERROR,
		        "loaded environment replacement preserves cancellation on rejection"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_call_function(
		                C.Session, Text("entry"), nullptr, 0, C.Value, &Result) == VS_INTEROP_STATUS_OK,
		        "configuration retry restores execution without removing the environment"))
		{
			return false;
		}

		vs_interop_package_build_options Package{};
		Package.struct_size = sizeof(Package);
		Package.kind = VS_INTEROP_PACKAGE_APPLICATION;
		Package.package_name = Text("interop-environment");
		if (!Expect(vs_interop_session_build_package_file(
		                C.Session, Text(Entry), &Package, &C.PackageBytes, &Result) == VS_INTEROP_STATUS_OK &&
		            Result.success,
		        "environment package build"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_clear(C.Session) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_apply_config_with_environment(C.Session, &Config, nullptr) ==
		                VS_INTEROP_STATUS_OK &&
		            Weak.expired() && Releases == 1 && ReleaseReentry == VS_INTEROP_STATUS_ENGINE_ERROR,
		        "environment removal releases owners under the activity guard"))
		{
			return false;
		}

		if (!Expect(vs_interop_session_register_native(C.Session, Text("local_answer"), &Function.return_type,
		                nullptr, 0, LocalAnswer, nullptr) == VS_INTEROP_STATUS_OK,
		        "local registry available after environment removal"))
		{
			return false;
		}

		FModuleEnvironmentDesc Conflicting;
		Conflicting.EntryOwnerId = "app";
		Conflicting.Scopes = {{"app", {}}};
		Conflicting.Bindings = std::make_shared<FHostBindingRegistry>();
		auto Other = FModuleEnvironment::Create(std::move(Conflicting), Error);
		if (!Expect(Other && Interop::CreateModuleEnvironment(Other, &C.Handle) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_apply_config_with_environment(C.Session, &Rejected, C.Handle) ==
		                VS_INTEROP_STATUS_ENGINE_ERROR &&
		            vs_interop_session_request_cancel(C.Session) == VS_INTEROP_STATUS_OK,
		        "local binding conflict preserves execution control before loading"))
		{
			return false;
		}

		FModuleEnvironmentDesc HostOnly;
		HostOnly.EntryOwnerId = "app";
		HostOnly.Scopes = {{"app", {}}};
		HostOnly.Modules = {{"host.vs", "app", Entry, "app", "interop.hostonly.1",
		    std::string("int main(){return local_answer();}")}};
		auto HostEnvironment = FModuleEnvironment::Create(std::move(HostOnly), Error);
		Config.execution_backend = VS_INTEROP_BACKEND_VM;
		if (!Expect(HostEnvironment &&
		            Interop::CreateModuleEnvironment(HostEnvironment, &C.Retained) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_apply_config_with_environment(C.Session, &Config, C.Retained) ==
		                VS_INTEROP_STATUS_OK &&
		            vs_interop_session_load_file(C.Session, Text(Entry), &Result) == VS_INTEROP_STATUS_OK &&
		            vs_interop_session_run(C.Session, VS_INTEROP_RUN_SCRIPT, &Result) ==
		                VS_INTEROP_STATUS_OK &&
		            Result.exit_code == 7,
		        "environment without a catalog preserves local C native bindings"))
		{
			return false;
		}

		std::cout << "installed native module environment SDK passed\n";
		return true;
	}
}

int main()
{
	using namespace VeroScript;
	if (!TestRejectedEnvironmentConfiguration()) return 1;
	if (!TestGeneralProcessing()) return 1;
	if (!TestModuleEnvironment()) return 1;
	FProbe Probe;
	struct FCleanup
	{
		~FCleanup()
		{
			vs_interop_value_destroy(Value);
			vs_interop_annotation_service_release(Retained);
			vs_interop_annotation_service_release(Service);
			if (Session)
			{
				// Release probes may call the session API, so clear their service
				// while the public session handle is still valid, including on failure.
				vs_interop_session_set_annotation_service(Session, nullptr);
				vs_interop_session_destroy(Session);
			}
		}

		vs_interop_session* Session = nullptr;

		vs_interop_annotation_service* Service = nullptr;

		vs_interop_annotation_service* Retained = nullptr;

		vs_interop_value* Value = nullptr;
	} Cleanup;
	if (!Expect((vs_interop_get_compile_capabilities() & VS_INTEROP_COMPILE_CAP_ANNOTATION_PROCESSING) != 0,
	        "compile capability discovery"))
	{
		return 1;
	}

	auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
	FAnnotationProcessingOptions Options;
	Options.Registry = Registry;
	if (!Expect(Interop::CreateAnnotationProcessingService(Options, &Cleanup.Service) ==
	                VS_INTEROP_STATUS_INVALID_ARGUMENT &&
	            !Cleanup.Service,
	        "unfrozen registry rejection"))
	{
		return 1;
	}

	std::string Error;
	if (!Expect(Registry->Register(
	                {"@Getter", "sdk.getter", "1", "", AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
	                    [&]
	{
		return std::make_unique<FGetter>(Probe);
	}},
	                Error),
	        "register getter"))
	{
		return 1;
	}

	Registry->Freeze();
	// Allocate directly: avoid destruction of a temporary release probe.
	Options.CheckCancelled = FConfigProbe(Probe, std::shared_ptr<FReleaseProbe>(new FReleaseProbe{Probe}));
	if (!Expect(
	        Interop::CreateAnnotationProcessingService(Options, &Cleanup.Service) == VS_INTEROP_STATUS_OK &&
	            vs_interop_annotation_service_retain(Cleanup.Service, &Cleanup.Retained) ==
	                VS_INTEROP_STATUS_OK,
	        "create and retain native service"))
	{
		return 1;
	}

	if (!Expect(
	        vs_interop_session_create(nullptr, &Cleanup.Session) == VS_INTEROP_STATUS_OK, "create C session"))
	{
		return 1;
	}

	Probe.Session = Cleanup.Session;
	if (!Expect(vs_interop_session_set_annotation_service(Cleanup.Session, Cleanup.Retained) ==
	            VS_INTEROP_STATUS_OK,
	        "attach retained service"))
	{
		return 1;
	}

	vs_interop_annotation_service_release(Cleanup.Service);
	vs_interop_annotation_service_release(Cleanup.Retained);
	Cleanup.Service = Cleanup.Retained = nullptr;
	Registry.reset();
	Options = {};
	vs_interop_config Config{};
	Config.struct_size = sizeof(Config);
	Config.execution_backend = VS_INTEROP_BACKEND_VM;
	vs_interop_run_result InitialResult{};
	InitialResult.struct_size = sizeof(InitialResult);
	if (!Expect(vs_interop_session_load_source(Cleanup.Session, Text("int PreservedFunction(){return 7;}"),
	                Text("preserved.vs"), &InitialResult) == VS_INTEROP_STATUS_OK,
	        "load program before configuration"))
	{
		return 1;
	}

	Probe.ConfigProbeArmed = true;
	Probe.ExecutionCopyProbeArmed = Probe.ExecutionReleaseProbeArmed = true;
	const auto ConfigStatus = vs_interop_session_apply_config(Cleanup.Session, &Config);
	Probe.ConfigProbeArmed = false;
	if (!Expect(ConfigStatus == VS_INTEROP_STATUS_OK && Probe.ConfigCopyChecks != 0 &&
	            Probe.ConfigReleaseChecks != 0 && Probe.ConfigCopyReentryRejected &&
	            Probe.ConfigReleaseReentryRejected,
	        "configuration rejects service copy/release reentry and preserves annotation service"))
	{
		return 1;
	}

	uint8_t HasPreserved = 0;
	if (!Expect(Probe.ExecutionReentryRejected && Probe.ExecutionFailureResultsValid &&
	            Probe.ExecutionReentryChecks == 2 &&
	            vs_interop_session_has_function(
	                Cleanup.Session, Text("PreservedFunction"), 0, &HasPreserved) == VS_INTEROP_STATUS_OK &&
	            HasPreserved,
	        "configuration rejects execution/load reentry from service copies and destructors"))
	{
		return 1;
	}

	char ConfigError[256]{};
	uint64_t ConfigRequired = 0;
	if (!Expect(vs_interop_session_copy_last_error(Cleanup.Session, ConfigError, sizeof(ConfigError),
	                &ConfigRequired) == VS_INTEROP_STATUS_OK &&
	            ConfigRequired == 1,
	        "successful configuration clears nested rejection error"))
	{
		return 1;
	}

	vs_interop_source_overlay InvalidOverlay{};
	InvalidOverlay.path = {nullptr, 1};
	auto InvalidConfig = Config;
	InvalidConfig.source_overlays = &InvalidOverlay;
	InvalidConfig.source_overlay_count = 1;
	const auto CopyChecksBefore = Probe.ConfigCopyChecks;
	const auto ReleaseChecksBefore = Probe.ConfigReleaseChecks;
	Probe.ConfigProbeArmed = true;
	const auto InvalidStatus = vs_interop_session_apply_config(Cleanup.Session, &InvalidConfig);
	Probe.ConfigProbeArmed = false;
	if (!Expect(InvalidStatus == VS_INTEROP_STATUS_INVALID_ARGUMENT &&
	            vs_interop_session_copy_last_error(Cleanup.Session, ConfigError, sizeof(ConfigError),
	                &ConfigRequired) == VS_INTEROP_STATUS_OK &&
	            std::strcmp(ConfigError, "invalid source overlay") == 0 &&
	            Probe.ConfigCopyChecks == CopyChecksBefore &&
	            Probe.ConfigReleaseChecks == ReleaseChecksBefore,
	        "invalid configuration preserves its error without invoking service functors"))
	{
		return 1;
	}

	Probe.FailConfigCopy = true;
	const auto FailedConfigStatus = vs_interop_session_apply_config(Cleanup.Session, &Config);
	Probe.FailConfigCopy = false;
	if (!Expect(FailedConfigStatus == VS_INTEROP_STATUS_OUT_OF_MEMORY &&
	            vs_interop_session_apply_config(Cleanup.Session, &Config) == VS_INTEROP_STATUS_OK,
	        "configuration contains service-copy failure and permits retry"))
	{
		return 1;
	}

	vs_interop_status ForeignSet = VS_INTEROP_STATUS_OK, ForeignQuery = VS_INTEROP_STATUS_OK;
	uint32_t ForeignSeverity = 123;
	std::thread Foreign(
	    [&]
	{
		ForeignSet = vs_interop_session_set_annotation_service(Cleanup.Session, nullptr);
		ForeignQuery = vs_interop_session_get_diagnostic_severity(Cleanup.Session, 0, &ForeignSeverity);
	});
	Foreign.join();
	if (!Expect(ForeignSet == VS_INTEROP_STATUS_WRONG_THREAD &&
	            ForeignQuery == VS_INTEROP_STATUS_WRONG_THREAD && ForeignSeverity == 123,
	        "foreign-thread configuration/query rejection"))
	{
		return 1;
	}

	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	const auto Source = Text("struct Item{@Getter int value=42;};int ReadGenerated(){Item x=Item();return x.Read();}");
	if (!Expect(vs_interop_session_load_source(Cleanup.Session, Source, Text("native-sdk.vs"), &Result) ==
	                VS_INTEROP_STATUS_OK &&
	            Result.success && Result.diagnostic_count == 3 && Probe.ReentryRejected,
	        "generation retains non-error diagnostics and rejects callback configuration"))
	{
		return 1;
	}

	const uint32_t Severities[] = {VS_INTEROP_DIAGNOSTIC_SEVERITY_WARNING,
	    VS_INTEROP_DIAGNOSTIC_SEVERITY_INFORMATION, VS_INTEROP_DIAGNOSTIC_SEVERITY_HINT};
	for (uint64_t I = 0; I < 3; ++I)
	{
		uint32_t Severity = 123;
		if (!Expect(vs_interop_session_get_diagnostic_severity(Cleanup.Session, I, &Severity) ==
		                VS_INTEROP_STATUS_OK &&
		            Severity == Severities[I],
		        "diagnostic severity mapping"))
		{
			return 1;
		}
	}
	uint32_t Unchanged = 123;
	if (!Expect(vs_interop_session_get_diagnostic_severity(Cleanup.Session, 3, &Unchanged) ==
	                VS_INTEROP_STATUS_INVALID_ARGUMENT &&
	            Unchanged == 123,
	        "invalid severity output preserved"))
	{
		return 1;
	}

	if (!Expect(vs_interop_value_create(&Cleanup.Value) == VS_INTEROP_STATUS_OK &&
	            vs_interop_session_call_function(Cleanup.Session, Text("ReadGenerated"), nullptr, 0,
	                Cleanup.Value, &Result) == VS_INTEROP_STATUS_OK &&
	            Result.success,
	        "invoke generated method"))
	{
		return 1;
	}

	int32_t Answer = 0;
	if (!Expect(vs_interop_value_get_int32(Cleanup.Value, &Answer) == VS_INTEROP_STATUS_OK && Answer == 42,
	        "generated method result"))
	{
		return 1;
	}

	Probe.ReportError = true;
	if (!Expect(vs_interop_session_load_source(Cleanup.Session, Source, Text("native-sdk.vs"), &Result) ==
	                VS_INTEROP_STATUS_ENGINE_ERROR &&
	            !Result.success && Result.diagnostic_count == 4,
	        "annotation error fails compilation"))
	{
		return 1;
	}

	uint32_t ErrorSeverity = 123;
	char ErrorText[256]{};
	uint64_t Required = 0;
	if (!Expect(vs_interop_session_get_diagnostic_severity(Cleanup.Session, 3, &ErrorSeverity) ==
	                VS_INTEROP_STATUS_OK &&
	            ErrorSeverity == VS_INTEROP_DIAGNOSTIC_SEVERITY_ERROR &&
	            vs_interop_session_copy_last_error(
	                Cleanup.Session, ErrorText, sizeof(ErrorText), &Required) == VS_INTEROP_STATUS_OK &&
	            std::strcmp(ErrorText, "annotation primary error") == 0,
	        "primary error follows earlier notes"))
	{
		return 1;
	}

	if (!Expect(vs_interop_session_set_annotation_service(Cleanup.Session, nullptr) == VS_INTEROP_STATUS_OK &&
	            Probe.ReleaseReentryRejected,
	        "service clearing rejects release-callback reentry"))
	{
		return 1;
	}

	uint64_t EmptyRequired = 0;
	if (!Expect(vs_interop_session_copy_last_error(
	                Cleanup.Session, ErrorText, sizeof(ErrorText), &EmptyRequired) == VS_INTEROP_STATUS_OK &&
	            EmptyRequired == 1,
	        "successful service clearing clears stale error"))
	{
		return 1;
	}

	if (!Expect(vs_interop_session_load_source(Cleanup.Session, Source, Text("native-sdk.vs"), &Result) ==
	                VS_INTEROP_STATUS_ENGINE_ERROR &&
	            !Result.success,
	        "clearing disables generated method"))
	{
		return 1;
	}

	std::cout << "installed native annotation SDK passed\n";
	return 0;
}
