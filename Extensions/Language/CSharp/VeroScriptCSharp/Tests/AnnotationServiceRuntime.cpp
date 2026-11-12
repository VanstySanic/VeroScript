// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropAnnotation.h"
#include "VeroScriptInteropCompileProcessing.h"
#include "VeroScriptInteropModuleEnvironment.h"
#include <atomic>
#include <memory>

#if defined(_WIN32)
#define VSC_TEST_EXPORT __declspec(dllexport)
#else
#define VSC_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
	std::atomic<std::uint32_t> EnvironmentReleases{0};
	struct FEnvironmentOwner
	{
		~FEnvironmentOwner()
		{
			++EnvironmentReleases;
		}
	};
	class FGeneralGenerate final : public VeroScript::ICompileProcessor
	{
	public:
		void Process(const VeroScript::FCompileProcessingContext& Context,
		    VeroScript::FCompileProcessingOutput& Output) override
		{
			Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
			    "int managed_generated(){return 42;}");
		}
	};
	class FGenerate final : public VeroScript::IAnnotationProcessor
	{
	public:
		void Process(const VeroScript::FAnnotationContext& Context, VeroScript::FAnnotationOutput& Output) override
		{
			Output.SubmitMethod(Context.Owner->Id, VeroScript::EMemberAccess::Public,
			    "int generated() const { return value + 1; }");
			VeroScript::FScriptError Diagnostic;
			Diagnostic.Message = "C# annotation diagnostic";
			Diagnostic.MessageId = "CSHARP_ANNOTATION_TEST";
			Diagnostic.Severity = static_cast<VeroScript::ECompileDiagnosticSeverity>(Severity);
			Output.ReportDiagnostic(std::move(Diagnostic));
		}

		explicit FGenerate(const std::uint32_t InSeverity) : Severity(InSeverity)
		{
		}

	private:
		std::uint32_t Severity;
	};
}

extern "C"
{
	VSC_TEST_EXPORT std::uint32_t VS_INTEROP_CALL vsc_test_compile_processing_create(
	    vs_interop_compile_processing_service** Out)
	{
		if (!Out) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*Out = nullptr;
		try
		{
			auto Registry = std::make_shared<VeroScript::FCompileProcessorRegistry>();
			VeroScript::FCompileProcessorDesc Desc;
			Desc.ProcessorId = "csharp.test.general";
			Desc.Version = "1";
			Desc.Phases = VeroScript::CompilePhaseMask(VeroScript::ECompileProcessingPhase::Members);
			Desc.Operations = VeroScript::CompileOperationMask(VeroScript::ECompileOperation::AddDeclaration);
			Desc.Subscriptions = {{VeroScript::ECompileProcessingEvent::Module}};
			Desc.Factory = []
			{
				return std::make_unique<FGeneralGenerate>();
			};
			std::string Error;
			if (!Registry->Register(std::move(Desc), Error)) return VS_INTEROP_STATUS_ENGINE_ERROR;
			Registry->Freeze();
			VeroScript::FCompileProcessingOptions Options;
			Options.Registry = std::move(Registry);
			return VeroScript::Interop::CreateCompileProcessingService(Options, Out);
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}
	VSC_TEST_EXPORT void VS_INTEROP_CALL vsc_test_compile_processing_release(vs_interop_compile_processing_service* Service)
	{
		vs_interop_compile_processing_service_release(Service);
	}
	VSC_TEST_EXPORT std::uint32_t VS_INTEROP_CALL vsc_test_module_environment_create(
	    std::uint32_t Mode, vs_interop_module_environment** Out)
	{
		if (!Out) return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		*Out = nullptr;
		try
		{
			VeroScript::FModuleEnvironmentDesc Desc;
			Desc.EntryOwnerId = "app";
			Desc.Scopes = {{"app", {"library"}}, {"library", {}}};
			Desc.Modules = {
			    {"answer.vs", "answer", "veroscript-module:/library/answer.vs", "library", "csharp.answer.1",
			        std::string("export int environment_answer(){return 42;}")},
			    {"main.vs", "entry", "veroscript-module:/app/main.vs", "app", "csharp.entry.1",
			        std::string("import \"answer.vs\"; export int read_environment(){return environment_answer();} int main(){return read_environment();}")}};
			Desc.Owners.push_back(std::make_shared<FEnvironmentOwner>());
			if (Mode == 2)
			{
				Desc.ExecutionBackends.Identity = "csharp.test.configuration-rejection.1";
				Desc.ExecutionBackends.Vm = [](std::string_view,
				    std::shared_ptr<const VeroScript::IVmBackendFactory>& Factory, std::string& Error)
				{
					Factory.reset();
					Error = "C# test VM resolver rejected configuration";
					return false;
				};
			}
			else if (Mode != 0)
			{
				Desc.Bindings = std::make_shared<VeroScript::FHostBindingRegistry>();
			}

			std::string Error;
			auto Environment = VeroScript::FModuleEnvironment::Create(std::move(Desc), Error);
			if (!Environment) return VS_INTEROP_STATUS_ENGINE_ERROR;
			return VeroScript::Interop::CreateModuleEnvironment(Environment, Out);
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}
	VSC_TEST_EXPORT void VS_INTEROP_CALL vsc_test_module_environment_release(vs_interop_module_environment* Environment)
	{
		vs_interop_module_environment_release(Environment);
	}
	VSC_TEST_EXPORT std::uint32_t VS_INTEROP_CALL vsc_test_module_environment_releases()
	{
		return EnvironmentReleases.load();
	}
	// The extension owns the returned service until the managed test releases it.
	VSC_TEST_EXPORT std::uint32_t VS_INTEROP_CALL vsc_test_annotation_create(
	    const std::uint32_t Severity, vs_interop_annotation_service** Out)
	{
		if (!Out || Severity > VS_INTEROP_DIAGNOSTIC_SEVERITY_HINT)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*Out = nullptr;
		try
		{
			auto Registry = std::make_shared<VeroScript::FAnnotationProcessorRegistry>();
			std::string Error;
			if (!Registry->Register(
			        {"@CsGenerate", "csharp.test.generate", "1", "",
			            VeroScript::AnnotationTargetMask(VeroScript::EAnnotationTarget::Field),
			            static_cast<std::uint8_t>(VeroScript::EAnnotationOperation::AddMethod), 0,
			            [Severity]
			{
				return std::make_unique<FGenerate>(Severity);
			}},
			        Error))
			{
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			Registry->Freeze();
			VeroScript::FAnnotationProcessingOptions Options;
			Options.Registry = std::move(Registry);
			return VeroScript::Interop::CreateAnnotationProcessingService(Options, Out);
		}
		catch (...)
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
	}
	VSC_TEST_EXPORT void VS_INTEROP_CALL vsc_test_annotation_release(vs_interop_annotation_service* Service)
	{
		vs_interop_annotation_service_release(Service);
	}
}
