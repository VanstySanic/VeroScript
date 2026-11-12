// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAnalyzer.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCSharpAot.h"
#include "VeroScriptComplete.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCppAot.h"
#include "VeroScriptHover.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptInteropAnnotation.h"
#include "VeroScriptPythonAot.h"
#include "VeroScriptPackage.h"
#include "VeroScriptRename.h"
#include "VeroScriptWorkspace.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
using namespace VeroScript;
int main(int Argc, char** Argv)
{
	int Failures = 0;
	const auto Check = [&](bool Yes, const std::string& What)
	{
		if (!Yes)
		{
			++Failures;
			std::cerr << What << '\n';
		}
	};
	struct FGenerate : IAnnotationProcessor
	{
		void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
		{
			O.SubmitMethod(C.Owner->Id, EMemberAccess::Public,
			    (C.Owner->TypeParameters.empty() ? "virtual " : "") + C.Declaration.TypeName +
			        " Get() const { return value; }");
			FScriptError D;
			D.Message = "processor warning";
			D.Severity = ECompileDiagnosticSeverity::Warning;
			O.ReportDiagnostic(std::move(D));
		}
	};
	auto R = std::make_shared<FAnnotationProcessorRegistry>();
	std::string Error;
	Check(R->Register({"@Generate", "integration.generator", "1", "",
	                      AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
	                      []
	{
		return std::make_unique<FGenerate>();
	}},
	          Error),
	    Error);
	R->Freeze();
	FAnnotationProcessingOptions P;
	P.Registry = R;
	const std::string Source =
	    "class Base { public: @Generate int value=21; }; class Child : public Base { public: int Get() "
	    "const override {return 42;} }; import \"decl.vs\"; import \"impl.vs\"; "
	    "void main(){Child x=Child(); Base b=x; print(b.Get()); Box<int> n(13); "
	    "print(n.Get()+n.Extra()); print(API::Echo<string>(\"ok\"));print(Transform(1));}";
	FCodegenOptions C;
	C.AnnotationProcessing = P;
	FCppCodegenBackend Cpp;
	FCSharpCodegenBackend Cs;
	FPythonCodegenBackend Py;
	const auto Dir = Argc > 1 ? std::filesystem::path(Argv[1]) : std::filesystem::path(".");
	std::filesystem::create_directories(Dir);
	{
		std::ofstream Out(Dir / "decl.vs");
		Out << "using Number=int; export int Transform(Number value); export struct Box<T>{ @Generate T value; Box(T "
		       "x){value=x;} int Extra() const; }; namespace "
		       "API{template<T> export T Echo(T value);}";
	}
	{
		std::ofstream Out(Dir / "impl.vs");
		Out << "import \"decl.vs\"; using OtherNumber=string; int helper(){return 2;} int ::Transform(int input){return "
		       "input+helper();} int Box<T>::Extra() const{return helper();} template<T> T "
		       "API::Echo(T input){return input;}";
	}
	{
		std::ofstream Out(Dir / "aot-bounds-decl.vs");
		Out << "namespace Contract{export interface BoundIface{};}"
		       "export struct BoundValue:public Contract::BoundIface{int value=11;};"
		       "namespace API{using Constraint=Contract::BoundIface;"
		       "template<T> export T BoundEcho(T value) where T:Constraint;}";
	}
	{
		std::ofstream Out(Dir / "aot-bounds-impl.vs");
		Out << "import \"aot-bounds-decl.vs\";template<T> T API::BoundEcho(T input){return input;}";
	}
	std::string CppSource = Source;
	CppSource.insert(CppSource.rfind('}'), "print(API::BoundEcho<BoundValue>(BoundValue()).value);");
	CppSource += "import \"aot-bounds-decl.vs\";import \"aot-bounds-impl.vs\";";
	{
		std::ofstream Out(Dir / "overload-decl.vs");
		Out << "export class Overloaded{public:int Get();string Get() const;};";
	}
	{
		std::ofstream Out(Dir / "overload-impl.vs");
		Out << "import \"overload-decl.vs\"; int Overloaded::Get(){return 7;} "
		       "string Overloaded::Get() const{return \"const\";}";
	}
	std::string PythonSource = Source;
	const auto Replace = [](std::string& Text, const std::string& From, const std::string& To)
	{
		std::size_t Pos = 0;
		while ((Pos = Text.find(From, Pos)) != std::string::npos)
		{
			Text.replace(Pos, From.size(), To);
			Pos += To.size();
		}
	};
	Replace(PythonSource, "decl.vs", "python-decl.vs");
	Replace(PythonSource, "impl.vs", "python-impl.vs");
	Replace(PythonSource, "Box<int>", "Box");
	Replace(PythonSource, "Echo<string>", "Echo");
	{
		std::ofstream Out(Dir / "python-decl.vs");
		Out << "using Number=int; export int Transform(Number value); export struct Box{@Generate int value; Box(int "
		       "x){value=x;} int Extra() const;}; namespace API{export string Echo(string value);}";
	}
	{
		std::ofstream Out(Dir / "python-impl.vs");
		Out << "import \"python-decl.vs\"; using OtherNumber=string; int helper(){return 2;} int ::Transform(int "
		       "input){return input+helper();} int Box::Extra() const{return helper();} string API::Echo(string "
		       "input){return input;}";
	}
	const auto SourceLabel = (std::filesystem::absolute(Dir) / "main.vs").generic_string();
	for (auto* Backend :
	    {static_cast<ICodegenBackend*>(&Cpp), static_cast<ICodegenBackend*>(&Cs), static_cast<ICodegenBackend*>(&Py)})
	{
		FCodegenResult Result;
		if (Backend == &Py)
		{
			FCodegenResult Unsupported;
			Check(!Py.EmitFromSource(Source, SourceLabel, C, Unsupported) &&
			        Unsupported.Error.find("open generic") != std::string::npos,
			    "Python preserves explicit open-generic AOT rejection");
		}
		if (Backend == &Cs)
		{
			FCodegenResult Unsupported;
			Check(!Cs.EmitFromSource(CppSource, SourceLabel, C, Unsupported) &&
			        Unsupported.Error.find("interface lowering") != std::string::npos,
			    "C# preserves its explicit interface AOT restriction after inherited-bound preparation");
		}
		// C++ can emit these native overload declarations. Their script call
		// semantics are covered by the core preparation/VM test; C# and Python
		// keep their existing explicit overload restrictions.
		const auto BackendSource = Backend == &Py ? PythonSource : Backend == &Cpp ? CppSource + "import \"overload-decl.vs\";import \"overload-impl.vs\";"
		                                                                           : Source;
		const bool Emitted = Backend->EmitFromSource(BackendSource, SourceLabel, C, Result);
		Check(Emitted, std::string(Backend->GetName()) + ": " + Result.Error);
		if (Result.bSuccess)
		{
			std::ofstream Out(Dir / ("generated" + std::string(Backend->GetFileExtension())), std::ios::binary);
			Out << Result.Output;
		}
	}
	using namespace VeroScript::Tooling;
	FAnalyzeOptions A;
	A.AnnotationProcessing = P;
	auto Analysis = VeroScriptAnalyzer::AnalyzeSource(Source, SourceLabel, A);
	Check(Analysis.bSuccess, "tooling compiles generated virtual method");
	Check(Analysis.GeneratedDocuments.size() == 2, "tooling exposes virtual source documents");
	Check(!Analysis.Diagnostics.empty(), "tooling preserves nonfatal processor diagnostics");
	{
		FWorkspaceOptions Options;
		Options.AnnotationProcessing = P;
		VeroScriptWorkspace Workspace;
		Workspace.SetOptions(Options);
		Workspace.OpenDocument(SourceLabel, Source);
		for (const auto& Name : {"Transform", "Extra", "Echo"})
		{
			const auto Offset = std::string_view(Name) == "Echo" ? Source.find("Echo<string>") : Source.rfind(Name);
			const FTextPosition Position{1, static_cast<unsigned>(Offset + 1)};
			const auto Declaration = Workspace.NavigateDocument(SourceLabel, Position);
			const auto Implementation = Workspace.ImplementationDocument(SourceLabel, Position);
			Check(Declaration.bSuccess && Declaration.Definitions.size() == 1 &&
			        std::filesystem::path(Declaration.Definitions.front().FileLabel).filename() == "decl.vs",
			    std::string("navigation retains the declaration of ") + Name);
			Check(Implementation.bSuccess && Implementation.Definitions.size() == 1 &&
			        std::filesystem::path(Implementation.Definitions.front().FileLabel).filename() == "impl.vs",
			    std::string("implementation navigation locates the source body of ") + Name);
		}
		const auto Generated = Workspace.ImplementationDocument(
		    SourceLabel, {1, static_cast<unsigned>(Source.find("b.Get") + 3)});
		Check(Generated.bSuccess &&
		        std::any_of(Generated.Definitions.begin(), Generated.Definitions.end(),
		            [](const auto& Hit)
		{
			return Hit.FileLabel.rfind("veroscript-generated:", 0) == 0;
		}),
		    "implementation lookup retains the generated default virtual body");
		const auto DeclPath = (std::filesystem::absolute(Dir) / "nav-decl.vs").generic_string();
		const auto ImplPath = (std::filesystem::absolute(Dir) / "nav-impl.vs").generic_string();
		const auto MainPath = (std::filesystem::absolute(Dir) / "nav-main.vs").generic_string();
		Workspace.OpenDocument(DeclPath,
		    "export class NavBase {public:virtual int Read();};\n"
		    "export class NavChild:public NavBase {public:int Read() override;};\n");
		Workspace.OpenDocument(ImplPath,
		    "import \"nav-decl.vs\";\n#define RESULT 2\n"
		    "int NavBase::Read(){return 1;}\nint NavChild::Read(){return RESULT;}\n");
		const std::string NavSource =
		    "import \"nav-decl.vs\";import \"nav-impl.vs\";void main(){NavBase v=NavChild();print(v.Read());}";
		Workspace.OpenDocument(MainPath, NavSource);
		const auto Overrides = Workspace.ImplementationDocument(
		    MainPath, {1, static_cast<unsigned>(NavSource.rfind("Read") + 1)});
		Check(Overrides.bSuccess && Overrides.Definitions.size() == 2 &&
		        std::all_of(Overrides.Definitions.begin(), Overrides.Definitions.end(),
		            [&](const auto& Hit)
		{
			return Hit.FileLabel == ImplPath &&
			    (Hit.DefinitionRange.StartLine == 3 || Hit.DefinitionRange.StartLine == 4);
		}),
		    "implementation lookup locates base/override bodies using each module's physical source map");
		Workspace.UpdateDocument(DeclPath,
		    "#define DECLBASE(Name) export class Name {public:virtual int Read();}\n"
		    "DECLBASE(NavBase);\nexport class NavChild:public NavBase {public:int Read() override;};\n");
		const auto MacroOverrides = Workspace.ImplementationDocument(
		    MainPath, {1, static_cast<unsigned>(NavSource.rfind("Read") + 1)});
		Check(MacroOverrides.bSuccess && MacroOverrides.Definitions.size() == 2,
		    "imported macro declarations do not create duplicate/ambiguous implementation owners");
		VeroScriptWorkspace PlainWorkspace;
		PlainWorkspace.OpenDocument(DeclPath,
		    "using Number=int;export class NavBase {public:virtual int Read(Number value);};"
		    "export class NavChild:public NavBase {public:int Read(Number value) override;};");
		PlainWorkspace.OpenDocument(ImplPath,
		    "import \"nav-decl.vs\";int NavBase::Read(int input){return input;}"
		    "int NavChild::Read(int input){return input+1;}");
		const std::string AliasSource =
		    "import \"nav-decl.vs\";import \"nav-impl.vs\";void main(){NavBase v=NavChild();print(v.Read(0));}";
		PlainWorkspace.OpenDocument(MainPath, AliasSource);
		const auto AliasOverrides = PlainWorkspace.ImplementationDocument(
		    MainPath, {1, static_cast<unsigned>(AliasSource.rfind("Read") + 1)});
		Check(AliasOverrides.bSuccess && AliasOverrides.Definitions.size() == 2,
		    "implementation index preserves the prepared signature when declaration aliases are resolved");
	}
	const std::string CompletionSource =
	    "class Base { public: @Generate int value=21; }; void main(){Base x=Base(); x.;}";
	FCompletionOptions Co;
	Co.AnnotationProcessing = P;
	const auto Offset = CompletionSource.find("x.;") + 2;
	auto Complete = VeroScriptComplete::CompleteAtSource(CompletionSource, {1, static_cast<unsigned>(Offset + 1)},
	    "integration.vs", Co);
	bool Found = false;
	for (const auto& Item : Complete.Items)
	{
		if (Item.Label == "Get")
		{
			Found = true;
		}
	}

	Check(Complete.bSuccess && Found, "completion discovers generated method");
	{
		struct FVersioned : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				std::string Revision;
				Context.ReadDependency("revision", Revision);
				Output.SubmitMethod(Context.Owner->Id, EMemberAccess::Public,
				    "int Generated" + Revision + "(){return value;}");
			}
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		Check(Registry->Register({"@Generate", "integration.versioned", "1", "",
		                             AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		                             []
		{
			return std::make_unique<FVersioned>();
		}},
		          Error),
		    Error);
		Registry->Freeze();
		FWorkspaceOptions Options;
		Options.AnnotationProcessing.Registry = Registry;
		int Revision = 1;
		Options.AnnotationProcessing.ReadDependency = [&](std::string_view, std::string& Content)
		{
			Content = std::to_string(Revision);
			return true;
		};
		VeroScriptWorkspace Workspace;
		Workspace.SetOptions(Options);
		const auto Path = (std::filesystem::absolute(Dir) / "workspace.vs").generic_string();
		const std::string Document = "class Base{public:@Generate int value=21;};";
		Workspace.OpenDocument(Path, Document);
		const auto Has = [](const FWorkspaceSymbolsResult& Result, std::string_view Name)
		{
			return Result.bSuccess &&
			    std::any_of(Result.Symbols.begin(), Result.Symbols.end(),
			        [&](const auto& Symbol)
			{
				return Symbol.Name == Name;
			});
		};
		Check(Has(Workspace.WorkspaceSymbols(), "Generated1"), "workspace indexes generated symbols");
		Revision = 2;
		const auto Updated = Workspace.WorkspaceSymbols();
		Check(Has(Updated, "Generated2") && !Has(Updated, "Generated1"),
		    "workspace symbols refresh when a processor dependency changes");
		FHostBindingRegistry Bindings;
		FWorkspaceSymbolsOptions WithBindings;
		WithBindings.Bindings = &Bindings;
		Check(Has(Workspace.WorkspaceSymbols(WithBindings), "Generated2"),
		    "explicit host bindings retain workspace annotation processors");
		FWorkspaceSymbolsOptions ExplicitProcessor;
		ExplicitProcessor.AnnotationProcessing = P;
		Check(Has(Workspace.WorkspaceSymbols(ExplicitProcessor), "Get"),
		    "explicit query processors override workspace processors independently of bindings");
		VeroScriptWorkspace PlainWorkspace;
		PlainWorkspace.OpenDocument(Path, Document);
		Check(!Has(PlainWorkspace.WorkspaceSymbols(), "Get"), "ordinary symbol index remains unchanged");
		Check(Has(PlainWorkspace.WorkspaceSymbols(ExplicitProcessor), "Get"),
		    "an explicit processor query does not reuse the ordinary symbol cache");
	}
	FRenameOptions Ro;
	{
		bool Cancel = false;
		unsigned CompletedSteps = 0;
		struct FCancelProcessor : IAnnotationProcessor
		{
			void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
			{
				Cancel = true;
				for (unsigned I = 0; I < 3; ++I)
				{
					Context.CheckCancelled();
					++Steps;
				}
				Output.SubmitMethod(Context.Owner->Id, EMemberAccess::Public, "int Read(){return value;}");
			}

			FCancelProcessor(bool& InCancel, unsigned& InSteps) : Cancel(InCancel), Steps(InSteps)
			{
			}

			bool& Cancel;

			unsigned& Steps;
		};
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		Check(Registry->Register({"@CancelProbe", "integration.cancel", "1", "",
		                             AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		                             [&]
		{
			return std::make_unique<FCancelProcessor>(Cancel, CompletedSteps);
		}},
		          Error),
		    Error);
		Registry->Freeze();
		FWorkspaceOptions Options;
		Options.AnnotationProcessing.Registry = Registry;
		VeroScriptWorkspace Workspace;
		Workspace.SetOptions(Options);
		const std::string Document = "struct Box{@CancelProbe int value=1;};void main(){Box b=Box();print(b.Read());}";
		const auto Path = (std::filesystem::absolute(Dir) / "cancel-implementation.vs").generic_string();
		Workspace.OpenDocument(Path, Document);
		FWorkspaceSymbolsOptions Query;
		Query.ShouldCancel = [&]
		{
			return Cancel;
		};
		const auto Result = Workspace.ImplementationDocument(
		    Path, {1, static_cast<unsigned>(Document.rfind("Read") + 1)}, Query);
		Check(Result.bCancelled && Result.Definitions.empty() && CompletedSteps == 0,
		    "implementation cancellation reaches annotation processing before returning partial results");
		Cancel = false;
		Query.ShouldCancel = {};
		const auto Recovered = Workspace.ImplementationDocument(
		    Path, {1, static_cast<unsigned>(Document.rfind("Read") + 1)}, Query);
		Check(Recovered.bSuccess && !Recovered.bCancelled && Recovered.Definitions.size() == 1 &&
		        CompletedSteps == 3,
		    "cancelled implementation queries restore control and can be retried");
		CompletedSteps = 0;
		FAnalysisControlScope OuterControl(
		    []
		{
			return true;
		});
		const auto Inherited =
		    Workspace.ImplementationDocument(Path, {1, static_cast<unsigned>(Document.rfind("Read") + 1)});
		Check(Inherited.bCancelled && Inherited.Definitions.empty() && CompletedSteps == 0,
		    "implementation queries preserve ambient cancellation without invoking processors");
	}
	Ro.AnnotationProcessing = P;
	const auto Call = Source.rfind("Get()");
	auto Rename = VeroScriptRename::CanRenameAtSource(Source, {1, static_cast<unsigned>(Call + 1)}, SourceLabel, Ro);
	Check(!Rename.bCanRename, "rename refuses edits to generated declarations");
	vs_interop_annotation_service* Service = nullptr;
	Check(Interop::CreateAnnotationProcessingService(P, &Service) == VS_INTEROP_STATUS_OK,
	    "native annotation service creation");
	vs_interop_session* Session = nullptr;
	Check(vs_interop_session_create(nullptr, &Session) == VS_INTEROP_STATUS_OK, "session creation");
	Check(vs_interop_session_set_annotation_service(Session, Service) == VS_INTEROP_STATUS_OK, "service attachment");
	vs_interop_annotation_service_release(Service);
	Service = nullptr;
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	vs_interop_string_view Code{Source.data(), Source.size()}, Label{SourceLabel.data(), SourceLabel.size()};
	Check(vs_interop_session_load_source(Session, Code, Label, &Result) == VS_INTEROP_STATUS_OK && Result.success,
	    "interop compilation retains service after handle release");
	std::uint32_t Severity = 0;
	Check(vs_interop_session_get_diagnostic_severity(Session, 0, &Severity) == VS_INTEROP_STATUS_OK && Severity == 1,
	    "interop exposes warning severity");
	Check(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success,
	    "interop executes generated virtual and generic methods");
	if (const char* Backend = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"); Backend && *Backend)
	{
		Check(vs_interop_session_set_jit_backend(Session, {Backend, std::strlen(Backend)}) == VS_INTEROP_STATUS_OK,
		    "annotation integration selects the native backend");
		Check(vs_interop_session_run(Session, VS_INTEROP_RUN_SCRIPT, &Result) == VS_INTEROP_STATUS_OK && Result.success,
		    "selected native backend preserves generated member execution");
	}
	Check(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK, "session destruction");
	{
		int Checks = 0;
		FAnnotationProcessingOptions Control;
		Control.CheckCancelled = [&]
		{
			++Checks;
		};
		vs_interop_annotation_service* ControlService = nullptr;
		Check(Interop::CreateAnnotationProcessingService(Control, &ControlService) == VS_INTEROP_STATUS_OK,
		    "preparation control service without processors");
		Session = nullptr;
		Check(vs_interop_session_create(nullptr, &Session) == VS_INTEROP_STATUS_OK, "control session creation");
		Check(vs_interop_session_set_annotation_service(Session, ControlService) == VS_INTEROP_STATUS_OK,
		    "control service attachment");
		vs_interop_annotation_service_release(ControlService);
		const std::string PlainSource = "void main(){}";
		Check(vs_interop_session_load_source(Session, {PlainSource.data(), PlainSource.size()}, Label, &Result) == VS_INTEROP_STATUS_OK && Result.success && Checks > 0,
		    "source preparation honors control without a processor registry");
		const auto File = (std::filesystem::absolute(Dir) / "control.vs").generic_string();
		{
			std::ofstream Out(File);
			Out << PlainSource;
		}
		Checks = 0;
		Check(vs_interop_session_load_file(Session, {File.data(), File.size()}, &Result) == VS_INTEROP_STATUS_OK && Result.success && Checks > 0,
		    "file preparation honors control without a processor registry");
		FCompileOptions ProducerOptions = VeroScript::Test::MakeCompileOptions();
		ProducerOptions.AnnotationProcessing = P;
		auto Producer = VeroScriptCompile::CompileSource(
		    "export class Budget<T>{public:@Generate T value;};", "budget-package.vs", ProducerOptions);
		std::vector<std::uint8_t> Bytes;
		FPackageBuildOptions PackageOptions;
		PackageOptions.Kind = EPackageKind::Library;
		PackageOptions.PackageName = "Budget";
		const bool Packaged = Producer.bSuccess && BuildPackage(*Producer.Module, PackageOptions, Bytes, Error);
		Check(Packaged, "prepare package for processor-free budget verification: " + Error);
		if (Packaged)
		{
			const auto PackageFile = std::filesystem::absolute(Dir) / "Budget.vslib";
			{
				std::ofstream Out(PackageFile, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
			}
			const std::string Consumer = "import \"Budget.vslib\"; void main(){Budget<int> b=Budget<int>();print(b.Get());}";
			const vs_interop_string_view ConsumerCode{Consumer.data(), Consumer.size()};
			Check(vs_interop_session_load_source(Session, ConsumerCode, Label, &Result) == VS_INTEROP_STATUS_OK && Result.success, "generated package loads without producer processors");
			FAnnotationProcessingOptions Budget;
			Budget.MaxOperations = 0;
			ControlService = nullptr;
			Check(Interop::CreateAnnotationProcessingService(Budget, &ControlService) == VS_INTEROP_STATUS_OK,
			    "processor-free generation budget service");
			Check(vs_interop_session_set_annotation_service(Session, ControlService) == VS_INTEROP_STATUS_OK,
			    "generation budget attachment");
			vs_interop_annotation_service_release(ControlService);
			Check(vs_interop_session_load_source(Session, ConsumerCode, Label, &Result) !=
			            VS_INTEROP_STATUS_OK &&
			        !Result.success,
			    "source package replay honors custom generation budgets without processors");
			{
				std::ofstream Out(File);
				Out << Consumer;
			}
			Check(vs_interop_session_load_file(Session, {File.data(), File.size()}, &Result) !=
			            VS_INTEROP_STATUS_OK &&
			        !Result.success,
			    "file package replay honors custom generation budgets without processors");
		}
		Check(vs_interop_session_destroy(Session) == VS_INTEROP_STATUS_OK, "control session destruction");
	}
	return Failures ? 1 : 0;
}
