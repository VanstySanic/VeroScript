// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptAsmJit.h"
#include "../../../../../VeroScript/Private/Tests/VeroScriptTestJitCapabilities.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptVm.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace
{
	using namespace VeroScript;
	int Failures = 0;
	void Expect(bool Condition, const std::string& Message)
	{
		if (!Condition)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}

	class FGenerator final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
		{
			if (Context.Phase == ECompileProcessingPhase::Types)
			{
				for (const auto& Declaration : Context.Declarations)
				{
					if (Declaration.ModulePath == Context.ModuleId && Declaration.Name == "Replaced" &&
						Declaration.Target == EAnnotationTarget::Class)
					{
						Output.ReplaceDeclaration(Declaration.Id, Context.Snapshot,
							"export class Replaced<T>{public:T value;int New(int arg){return arg+1;}};");
					}
				}

				bool HasCard = false;
				for (const auto& Declaration : Context.Declarations)
				{
					if (Declaration.ModulePath == Context.ModuleId && Declaration.Name == "Card" &&
						Declaration.Target == EAnnotationTarget::Class)
					{
						HasCard = true;
					}
				}

				if (!HasCard) return;
				Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
					"export class Generated<T> {public:T value;T get(){return value;}};");
				Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
					"/// Builder docs\nexport class CardBuilder {public:Card value=Card();"
					"CardBuilder Text(string text){value.text=text;return this;}"
					"Card Build(){return value;}};");
			}
			else if (Context.Phase == ECompileProcessingPhase::Members)
			{
				for (const auto& Declaration : Context.Declarations)
				{
					if (Declaration.ModulePath != Context.ModuleId)
					{
						continue;
					}

					const FCompileScope Scope{Context.ModuleId, "", Declaration.Id, Context.Snapshot};
					if (Declaration.Target == EAnnotationTarget::Class && Declaration.Name == "Card")
					{
						Output.AddDeclaration(
							Scope, "public:static CardBuilder Create(){return CardBuilder();}");
					}

					if (Declaration.Target == EAnnotationTarget::Class && Declaration.Name == "Base")
					{
						Output.AddDeclaration(Scope, "public:virtual int Helper();");
					}
				}
			}
			else
			{
				for (const auto& Declaration : Context.Declarations)
				{
					if (Declaration.ModulePath == Context.ModuleId && Declaration.Name == "Helper" &&
						!Declaration.bHasBody)
					{
						Output.CompleteBody(Declaration.Id, Context.Snapshot, "{return 3;}");
					}

					if (Declaration.ModulePath == Context.ModuleId && Declaration.Name == "Compute" &&
						Declaration.bHasBody)
					{
						Output.ReplaceBody(Declaration.Id, Context.Snapshot, "{ // new body\nreturn 81;}");
					}
				}
			}
		}
	};

	FCompileProcessingOptions Processing()
	{
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Desc;
		Desc.ProcessorId = "jit.processing.regression";
		Desc.Version = "0.1.1";
		Desc.Phases = CompilePhaseMask(ECompileProcessingPhase::Types)
			| CompilePhaseMask(ECompileProcessingPhase::Members) | CompilePhaseMask(ECompileProcessingPhase::Bodies);
		Desc.Operations = CompileOperationMask(ECompileOperation::AddDeclaration)
			| CompileOperationMask(ECompileOperation::ReplaceDeclaration)
			| CompileOperationMask(ECompileOperation::CompleteBody) | CompileOperationMask(ECompileOperation::ReplaceBody);
		Desc.Subscriptions.push_back({ECompileProcessingEvent::Module});
		Desc.Factory = []
		{
			return std::make_unique<FGenerator>();
		};
		std::string Error;
		Expect(Registry->Register(std::move(Desc), Error), "register processor: " + Error);
		Registry->Freeze();
		FCompileProcessingOptions Options;
		Options.Registry = Registry;
		return Options;
	}

	void Execute(const FBytecodeModule& Module, const std::string& Expected,
		const std::shared_ptr<const IJitBackendFactory>& Factory, const std::string& Name,
		bool ShouldFail = false)
	{
		for (auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
			std::string Error;
			Expect(Vm.SetJitBackendFactory(Factory, Error), Name + ": install backend: " + Error);
			Vm.SetExecutionBackend(Backend);
			FJitPolicy Policy;
			Policy.MixedHotCallThreshold = 1;
			Policy.MaxCompileAttempts = 0;
			Policy.MaxCachedEntries = 0;
			Policy.EagerCompileBudget = 256;
			Vm.SetJitPolicy(Policy);
			const bool Success = Vm.Execute(Module, ERunMode::Script);
			const std::string Label = Name + ": " + ExecutionBackendName(Backend);
			Expect(Success != ShouldFail, Label + ": execute: " + Vm.GetError().Message);
			Expect(ShouldFail ? Vm.GetError().Message.find(Expected) != std::string::npos : Output.str() == Expected,
				Label + ": result: " + Output.str() + Vm.GetError().Message);
			if (Backend != EExecutionBackend::Vm)
			{
				const auto& Stats = Vm.GetJitStats();
				const bool ScalarLeavesOnly = Factory->GetDescription().bNativeCode &&
				    !Test::SupportsAsmJitControlFlow(Factory->GetDescription());
				Expect(ScalarLeavesOnly ? Stats.ControlledNativeExecutions == 0 && Stats.NativeCfgEntries == 0
				    : Stats.CompiledEntries > 0 && Stats.Dispatches > 0,
				    Label + ": backend execution matches available control-flow support");
				#if VEROSCRIPT_HAS_ASMJIT && (VEROSCRIPT_ASMJIT_X64 || VEROSCRIPT_ASMJIT_A64)
				// Every source calls a scalar probe before its feature checks, proving
				// that requesting JIT has not silently tested an interpreter-only run.
				Expect(Stats.NativeLeafEntries + Stats.NativeCfgEntries > 0, Label + ": native code emitted");
				#endif
			}
		}
	}

	void Verify(const FCompileResult& Compiled, const std::string& Expected,
		const std::vector<std::shared_ptr<const IJitBackendFactory>>& Factories,
		const std::string& Name, bool ShouldFail = false)
	{
		Expect(Compiled.bSuccess, Name + ": compile: " + Compiled.Error.Message);
		if (!Compiled.bSuccess) return;
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		Expect(VeroScriptBytecodeIO::SerializeBytecode(*Compiled.Module, 0, Bytes, Error), Name + ": serialize: " + Error);
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), std::nullopt, Error);
		Expect(bool(Loaded), Name + ": deserialize: " + Error);
		for (std::size_t Index = 0; Index < Factories.size(); ++Index)
		{
			const auto Label = Name + (Index == 0 ? ": static" : ": dynamic");
			Execute(*Compiled.Module, Expected, Factories[Index], Label, ShouldFail);
			if (Loaded)
			{
				Execute(*Loaded, Expected, Factories[Index], Label + ": VSBC", ShouldFail);
			}
		}
	}

	void VerifyRetainedSnapshot(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		const std::string Functions =
			"class Value{};annotation_info get(){return type<Value>().annotation(0);}"
			"string read(annotation_info a){return a.name();}";
		auto Old = VeroScriptCompile::CompileSource("@Old " + Functions, "snapshot.vs");
		auto Current = VeroScriptCompile::CompileSource("@Current " + Functions, "snapshot.vs");
		Expect(Old.bSuccess && Current.bSuccess, "compile snapshot generations");
		if (!Old.bSuccess || !Current.bSuccess) return;
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string Error;
		Expect(Vm.SetJitBackendFactory(Factory, Error), "snapshot factory: " + Error);
		Vm.SetExecutionBackend(EExecutionBackend::Jit);
		Vm.SetProgramGeneration(1);
		FValue Retained;
		Expect(Vm.ExecuteFreeFunction(*Old.Module, "get", {}, nullptr, nullptr, &Retained),
			"retain old descriptor: " + Vm.GetError().Message);
		Vm.SetProgramGeneration(2);
		FValue Result;
		Expect(Vm.ExecuteFreeFunction(*Current.Module, "read", {Retained}, nullptr, nullptr, &Result),
			"query old descriptor in new generation: " + Vm.GetError().Message);
		const auto* Name = std::get_if<std::string>(&Result);
		Expect(Name && *Name == "@Old", "descriptor keeps its source snapshot across JIT generations");
		Expect(Vm.GetJitStats().Dispatches > 0, "retained descriptor query entered backend");
	}
}

int main(int Argc, char** Argv)
{
	using namespace VeroScript;
	std::vector<std::shared_ptr<const IJitBackendFactory>> Factories{CreateAsmJitBackendFactory()};
	if (Argc > 1)
	{
		std::string Error;
		auto Factory = LoadJitBackend(Argv[1], Error);
		Expect(bool(Factory), "load matching standalone backend: " + Error);
		if (Factory)
		{
			Factories.push_back(std::move(Factory));
		}
	}
	// Keep a small CFG so the caller cannot inline away the native-code witness.
	const std::string Probe = "int native_probe(int n){for(int i=0;i<2;i++){n+=1;}return n-1;}\n";
	FCompileOptions ScriptOptions = VeroScript::Test::MakeCompileOptions();
	#if defined(VEROSCRIPT_TEST_EMULATOR)
	ScriptOptions.ScriptCompileExtensions.TimeoutMilliseconds = 20000;
	#endif
	ScriptOptions.SourceOverlays = {{"jit-script-extension.vs", R"vs(import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
if(ctx.phase()==std::compiler::phase::types)
out.add_declaration(ctx.top_scope(),"export class ScriptGenerated<T>{public:T value;T Get(){return value;}};");
else out.add_declaration(ctx.top_scope(),"int generated_probe(int n){return n*2+1;}");};}
export void register_extension(std::compiler::registration reg){
std::compiler::annotation_definition a=std::compiler::annotation_definition();a.name="@ScriptDoc";a.documentation="Script annotation";reg.register_annotation(a);
std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
std::compiler::processor_definition d=std::compiler::processor_definition();d.id="jit.script.generated";d.version="0.1.1";
d.phases=array<std::compiler::phase>{std::compiler::phase::types,std::compiler::phase::members};
d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);})vs"}};
	ScriptOptions.ScriptCompileExtensions.Roots = {{"jit-script-extension.vs", {}}};
	Verify(VeroScriptCompile::CompileSource(Probe +
		"@ScriptDoc class Original{};void main(){print(native_probe(41));print(generated_probe(5));"
		"ScriptGenerated<int> value=ScriptGenerated<int>();value.value=23;print(value.Get());"
		"print(type<Original>().annotation_count());}", "jit-script-business.vs", ScriptOptions),
		"42\n11\n23\n1\n", Factories, "script extension generated declarations and metadata");
	Verify(VeroScriptCompile::CompileSource(Probe+
		"class Value{};void main(){print(native_probe(41));source_node_info n=source_node_info();"
		"print(n.is_valid());print(n.parameter_count());print(len(n.source_metadata()));}","jit-invalid-source-node.vs"),
		"42\nfalse\n0\n0\n",Factories,"invalid source descriptor has no children");
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	Options.CompileProcessing = Processing();
	Verify(VeroScriptCompile::CompileSource(Probe +
		"/// Template docs\ntemplate/* > */</* < > */ T> class Typed{public:T value;};"
		"\n/// Static docs\npublic/* access note */static class Math{int answer(){return 42;}};"
		"\nclass Work{public:\n/// Method docs\nint Compute(){ // old body\nreturn 1;}};"
		"void main(){print(native_probe(41));print(type<Typed<int>>().documentation());print(type<Math>().documentation());"
		"Work w=Work();print(w.Compute());type<Work> t=type<Work>();"
		"for(int i=0;i<t.method_count();++i){method_info<Work> m(t.qualified_name,i);if(m.name()==\"Compute\"){"
		"print(m.documentation());print(m.comment_count());print(m.comment(1).text());}}}","jit-prefix-body-comments.vs",Options),
		"42\nTemplate docs\nStatic docs\n81\nMethod docs\n2\n new body\n",Factories,"declaration prefixes and replaced body comments");
	Verify(VeroScriptCompile::CompileSource(Probe +
		"class Card {public:string text;};"
		"class Base {public:int Read(){return Helper();}};"
		"class Derived:public Base {public:int Helper() override{return 9;}};"
		"void main(){print(native_probe(41));Card c=Card::Create().Text(\"Save\").Build();print(c.text);"
		"Base b=Derived();print(b.Read());Base original=Base();print(original.Read());"
		"Generated<int> g=Generated<int>();g.value=23;print(g.get());print(type<CardBuilder>().documentation());}", "jit-generated.vs", Options),
		"42\nSave\n9\n3\n23\nBuilder docs\n", Factories, "generated types, builder, virtual override and generic");

	// Imported modules get their own processing session; user definitions must
	// replace the generated default before backend vtables/signatures are built.
	Options.SourceOverlays = {{"jit-base.vs", "export class Card{public:string text;};export class Base{public:int Read(){return Helper();}};"},
		{"jit-implementation.vs", "import \"jit-base.vs\";int Base::Helper(){return 12;}"}};
	Verify(VeroScriptCompile::CompileSource(Probe +
		"import \"jit-base.vs\";import \"jit-implementation.vs\";"
		"void main(){print(native_probe(41));Base b=Base();print(b.Read());}", "jit-cross.vs", Options),
		"42\n12\n", Factories, "cross-file user implementation");

	FHostBindingRegistry Bindings;
	FHostAnnotationArgumentDesc Size;
	Size.Name = "size";
	Size.Documentation = "Size docs";
	FHostAnnotationArgumentDesc Group;
	Group.Name = "meta";
	Group.Children.push_back(Size);
	std::string Error;
	Expect(Bindings.RegisterAnnotation("@Meta", "Metadata", Error, "Annotation docs", {Group}), Error);
	FCompileOptions MetadataOptions = VeroScript::Test::MakeCompileOptions();
	MetadataOptions.Bindings = &Bindings;
	Expect(Bindings.RegisterAnnotation("@GuiWidget","",Error,"Widget docs",{},AnnotationAllTargets,false),Error);
	Verify(VeroScriptCompile::CompileSource(Probe+
		"@gUIwIDGET class Card{};void main(){print(native_probe(41));type<Card> t=type<Card>();"
		"print(t.has_annotation(\"GUIWIDGET\"));print(len(t.find_annotations(\"@guiwidget\")));"
		"print(t.annotation(0).name());print(t.annotation(0).definition().name());"
		"print(t.annotation(0).definition().case_sensitive());}","jit-annotation-names.vs",MetadataOptions),
		"42\ntrue\n1\n@gUIwIDGET\n@GuiWidget\nfalse\n",Factories,"configured annotation name policy");
	Verify(VeroScriptCompile::CompileSource(Probe +
		"/**/\n/// Type docs\n@Meta(meta=(size=4i64,enabled=true,ratio=1.25,signed_number=-7i64,unsigned_number=18_446_744_073_709_551_615u64),extra=6)\n"
		"class Value {public:/// Field docs\n@Field int item;@Method int f(int x){return x;}};"
		"void main(){print(native_probe(41));type<Value> t=type<Value>();print(t.annotation_count());"
		"annotation_info a=t.annotation(0);print(a.name());print(a.definition().documentation());"
		"annotation_argument_info meta=a.argument(0);print(meta.argument(0).int64_value()+1);"
		"print(meta.argument(1).bool_value());print(meta.argument(2).float_value()*2.0);"
		"print(meta.argument(3).int64_value());print(meta.argument(4).uint64_value());"
		"print(meta.argument(0).definition().documentation());"
		"annotation_definition_info d=a.definition();print(d.argument_count());print(len(d.arguments()));"
		"print(d.argument(0).argument_count());print(meta.argument(0).definition().argument_count());"
		"annotation_definition_info missing=meta.argument(1).definition();print(missing.is_valid());"
		"print(len(missing.documentation()));print(len(missing.name()));print(len(missing.provider()));"
		"print(missing.argument_count());print(len(missing.arguments()));print(t.documentation());"
		"print(t.comment(1).text());print(t.declaration().is_valid());"
		"field_info<Value> field(t.qualified_name,0);print(field.annotation(0).name());"
		"for(int i=0;i<t.method_count();++i){method_info<Value> m(t.qualified_name,i);"
		"if(m.name()==\"f\"){print(m.annotation(0).name());print(m.declaration().parameter_count());}}}",
		"jit-metadata.vs", MetadataOptions),
		"42\n1\n@Meta\nAnnotation docs\n5\ntrue\n2.5\n-7\n18446744073709551615\nSize docs\n1\n1\n1\n0\nfalse\n0\n0\n0\n0\n0\nType docs\n Type docs\ntrue\n@Field\n@Method\n1\n",
		Factories, "metadata scalar kinds, descriptors, comments and descriptions");

	MetadataOptions.CompileProcessing.CommentRetention = ECommentRetention::Documentation;
	Verify(VeroScriptCompile::CompileSource(Probe +
		"// Fallback docs\nclass Value { /// Scope note\n\nint item;};void main(){print(native_probe(41));"
		"type<Value> t=type<Value>();print(t.documentation());print(t.comment_count());print(t.comment(0).association());}",
		"jit-documentation.vs", MetadataOptions), "42\nFallback docs\n1\n1\n", Factories, "documentation fallback retention");
	MetadataOptions.CompileProcessing.CommentRetention = ECommentRetention::NoComments;
	Verify(VeroScriptCompile::CompileSource(Probe +
		"/// Hidden docs\n@Meta class Value{};void main(){print(native_probe(41));"
		"type<Value> t=type<Value>();print(t.annotation_count());print(t.comment_count());print(t.comments_available());}",
		"jit-retention.vs", MetadataOptions), "42\n1\n0\nfalse\n", Factories, "comment retention");
	Verify(VeroScriptCompile::CompileSource(Probe +
				   "@Meta(size=4) class Value{};void main(){native_probe(41);"
				   "type<Value> t=type<Value>();print(t.annotation(0).argument(99).name());}",
			   "jit-invalid-metadata.vs"),
		"annotation argument index out of range", Factories, "metadata error propagation", true);
	for (const auto& Factory : Factories)
	{
		VerifyRetainedSnapshot(Factory);
	}

	// An open generated type must replay from a library package without
	// registering or running its producer's processor on the consuming side.
	Options.SourceOverlays.clear();
	auto Library = VeroScriptCompile::CompileSource(
		"export class Card{public:string text;};export T Build<T>(T x){Generated<T> g=Generated<T>();g.value=x;return g.get();}"
		"export class Work<T>{public:T value;\n/// Method docs\nint Compute(){ // old body\nreturn 1;}};"
		"\n/// Replaced docs\nexport class Replaced<T>{public:T old;int Old(int former){ // removed type body\nreturn former;}};",
		"jit-library.vs", Options);
	Expect(Library.bSuccess, "compile generic producer: " + Library.Error.Message);
	if (Library.bSuccess)
	{
		FPackageBuildOptions Package;
		Package.Kind = EPackageKind::Library;
		Package.PackageName = "JitProcessing";
		Package.bStripDebugInfo = true;
		std::vector<std::uint8_t> Bytes;
		const bool Built = BuildPackage(*Library.Module, Package, Bytes, Error);
		Expect(Built, "build package: " + Error);
		if (Built)
		{
			const auto Directory = std::filesystem::temp_directory_path()
				/ ("vero-jit-processing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directory(Directory);
			const auto File = Directory / "JitProcessing.vslib";
			{
				std::ofstream Out(File, std::ios::binary);
				Out.write(
					reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			}
			Verify(
				VeroScriptCompile::CompileSource(Probe + "import \"" + File.generic_string() +
						"\";void main(){print(native_probe(41));print(Build<int>(23));Work<int> w=Work<int>();print(w.Compute());"
						"type<Work<int>> t=type<Work<int>>();for(int i=0;i<t.method_count();++i){method_info<Work<int>> m(t.qualified_name,i);"
						"if(m.name()==\"Compute\"){print(m.documentation());print(m.comment_count());array<source_node_info> nodes=m.declaration().source_metadata();"
						"for(int j=0;j<len(nodes);++j)if(nodes[j].kind()==\"comment\")print(nodes[j].comment(0).text());}}"
						"Replaced<int> r=Replaced<int>();r.value=7;print(r.value);print(r.New(8));print(type<Replaced<int>>().documentation());}",
					"jit-consumer.vs"),
				"42\n23\n81\nMethod docs\n2\n new body\n7\n9\nReplaced docs\n", Factories,
				"open generic package replay and invalidated declaration metadata");
			std::filesystem::remove(File);
			std::filesystem::remove(Directory);
		}
	}
	return Failures == 0 ? 0 : 1;
}
