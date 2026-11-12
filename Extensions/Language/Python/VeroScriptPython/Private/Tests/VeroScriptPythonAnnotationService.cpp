// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Native SDK producer used only by Python binding integration tests.
#include "VeroScriptInteropAnnotation.h"
#include "VeroScriptInteropCompileProcessing.h"
#include "VeroScriptInteropModuleEnvironment.h"

#if defined(_WIN32)
#define VS_PYTHON_TEST_EXPORT __declspec(dllexport)
#else
#define VS_PYTHON_TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
	class FGeneralProcessor final : public VeroScript::ICompileProcessor
	{
	public:
		void Process(const VeroScript::FCompileProcessingContext& Context,
		    VeroScript::FCompileProcessingOutput& Output) override
		{
			Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
			    "int NativeGenerated(){return 37;}");
		}
	};
	class FGetter final : public VeroScript::IAnnotationProcessor
	{
	public:
		void Process(const VeroScript::FAnnotationContext& Context, VeroScript::FAnnotationOutput& Output) override
		{
			Output.SubmitMethod(Context.Owner->Id, VeroScript::EMemberAccess::Public,
			    "int GeneratedValue() const { return value; }");
			VeroScript::FScriptError Diagnostic;
			Diagnostic.Message = "Python native annotation diagnostic";
			Diagnostic.Severity = Severity;
			Output.ReportDiagnostic(std::move(Diagnostic));
		}

		explicit FGetter(const VeroScript::ECompileDiagnosticSeverity InSeverity) : Severity(InSeverity)
		{
		}

	private:
		VeroScript::ECompileDiagnosticSeverity Severity;
	};
}

extern "C" VS_PYTHON_TEST_EXPORT vs_interop_annotation_service* VS_INTEROP_CALL
CreatePythonAnnotationService(const std::uint32_t Severity)
{
	if (Severity > VS_INTEROP_DIAGNOSTIC_SEVERITY_HINT)
	{
		return nullptr;
	}
	try
	{
		auto Registry = std::make_shared<VeroScript::FAnnotationProcessorRegistry>();
		VeroScript::FAnnotationProcessorDesc Descriptor;
		Descriptor.Name = "@PythonGenerated";
		Descriptor.ProcessorId = "python.test.native-getter";
		Descriptor.Version = "1";
		Descriptor.Targets = VeroScript::AnnotationTargetMask(VeroScript::EAnnotationTarget::Field);
		Descriptor.Operations = static_cast<std::uint8_t>(VeroScript::EAnnotationOperation::AddMethod);
		Descriptor.Factory = [Severity]
		{
			return std::make_unique<FGetter>(static_cast<VeroScript::ECompileDiagnosticSeverity>(Severity));
		};
		std::string Error;
		if (!Registry->Register(std::move(Descriptor), Error))
		{
			return nullptr;
		}
		Registry->Freeze();
		VeroScript::FAnnotationProcessingOptions Options;
		Options.Registry = std::move(Registry);
		vs_interop_annotation_service* Service = nullptr;
		return VeroScript::Interop::CreateAnnotationProcessingService(Options, &Service) == VS_INTEROP_STATUS_OK
		    ? Service
		    : nullptr;
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" VS_PYTHON_TEST_EXPORT void VS_INTEROP_CALL
ReleasePythonAnnotationService(vs_interop_annotation_service* Service)
{
	vs_interop_annotation_service_release(Service);
}

extern "C" VS_PYTHON_TEST_EXPORT vs_interop_compile_processing_service* VS_INTEROP_CALL
CreatePythonCompileProcessingService()
{
	try
	{
		using namespace VeroScript;
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Descriptor;
		Descriptor.ProcessorId = "python.test.general";
		Descriptor.Version = "1";
		Descriptor.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
		Descriptor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
		FCompileSubscription Subscription;
		Subscription.Event = ECompileProcessingEvent::Module;
		Descriptor.Subscriptions = {Subscription};
		Descriptor.Factory = []
		{
			return std::make_unique<FGeneralProcessor>();
		};
		std::string Error;
		if (!Registry->Register(std::move(Descriptor), Error)) return nullptr;
		Registry->Freeze();
		FCompileProcessingOptions Options;
		Options.Registry = Registry;
		vs_interop_compile_processing_service* Service = nullptr;
		return Interop::CreateCompileProcessingService(Options, &Service) == VS_INTEROP_STATUS_OK ? Service
		                                                                                          : nullptr;
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" VS_PYTHON_TEST_EXPORT void VS_INTEROP_CALL
ReleasePythonCompileProcessingService(vs_interop_compile_processing_service* Service)
{
	vs_interop_compile_processing_service_release(Service);
}

extern "C" VS_PYTHON_TEST_EXPORT vs_interop_module_environment* VS_INTEROP_CALL
CreatePythonModuleEnvironment(const std::uint32_t OwnBindings)
{
	try
	{
		using namespace VeroScript;
		FModuleEnvironmentDesc Descriptor;
		Descriptor.EntryOwnerId = "app";
		Descriptor.Scopes = {{"app", {"library"}}, {"library", {}}};
		if (OwnBindings)
		{
			Descriptor.Bindings = std::make_shared<FHostBindingRegistry>();
		}

		Descriptor.Modules = {{"library.vs", "library.api", "veroscript-module:/python/library.vs", "library",
		    "python-test-v1", std::string("export int answer(){return 42;}")}};
		std::string Error;
		auto Environment = FModuleEnvironment::Create(std::move(Descriptor), Error);
		vs_interop_module_environment* Handle = nullptr;
		return Environment && Interop::CreateModuleEnvironment(Environment, &Handle) == VS_INTEROP_STATUS_OK
		    ? Handle
		    : nullptr;
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" VS_PYTHON_TEST_EXPORT void VS_INTEROP_CALL
ReleasePythonModuleEnvironment(vs_interop_module_environment* Environment)
{
	vs_interop_module_environment_release(Environment);
}
