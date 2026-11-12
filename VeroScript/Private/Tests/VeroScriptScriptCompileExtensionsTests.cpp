// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptPackage.h"
#include "VeroScriptVm.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptHostBindingCatalog.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace VeroScript;
namespace
{
	const char* Builder = R"vs(
import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback make_builder(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 if(ctx.phase()==std::compiler::phase::types)
  out.add_declaration(ctx.top_scope(),"class CardBuilder { public: Card value=Card(); CardBuilder Text(string s){value.text=s;return this;} Card Build(){return value;} };");
 else out.add_declaration(ctx.member_scope(),"public: static CardBuilder Create(){return CardBuilder();}");
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::annotation_definition a = std::compiler::annotation_definition(); a.name="@Builder"; a.documentation="builder documentation"; a.targets=array<std::compiler::target>{std::compiler::target::class_type}; reg.register_annotation(a);
 std::compiler::subscription s = std::compiler::subscription(); s.event=std::compiler::event::annotation; s.annotation_name="@Builder"; s.targets=array<std::compiler::target>{std::compiler::target::class_type};
 std::compiler::processor_definition d = std::compiler::processor_definition(); d.id="test.builder";d.version="0.1.1";d.phases=array<std::compiler::phase>{std::compiler::phase::types,std::compiler::phase::members};d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,make_builder);
}
)vs";
	bool Check(bool V, const char* Label)
	{
		if (!V)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}

		return V;
	}
	std::string Handler(
		std::string Body, std::string Phases, std::string Operations, std::string Globals = {})
	{
		return "import \"compiler.vs\";import \"string.vs\"; using "
			   "callback=fn(std::compiler::context,std::compiler::output)->void;" +
			Globals +
			"callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{" +
			Body +
			"};}"
			"export void register_extension(std::compiler::registration reg){std::compiler::subscription "
			"s=std::compiler::subscription();s.event=std::compiler::event::module;"
			"std::compiler::processor_definition "
			"d=std::compiler::processor_definition();d.id=\"test.handler\";d.version=\"0.1.1\";d.phases=array<std::"
			"compiler::phase>{" +
			Phases + "};d.operations=array<std::compiler::operation>{" + Operations +
			"};d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);}";
	}
	FCompileOptions SemanticCompileOptions()
	{
		// Semantic cases need room for cold compiler-service loading under emulation.
		FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
		Options.ScriptCompileExtensions.TimeoutMilliseconds = 20000;
		return Options;
	}
	FCompileResult CompileHandler(
		std::string Extension, std::string Source = "int main(){return 0;}", FCompileOptions O = SemanticCompileOptions())
	{
		O.SourceOverlays.push_back({"E:/VeroScriptTests/handler.vs", std::move(Extension)});
		O.ScriptCompileExtensions.Roots.push_back({"E:/VeroScriptTests/handler.vs", {}});
		return VeroScriptCompile::CompileSource(Source, "E:/VeroScriptTests/main.vs", O);
	}
} // namespace
int main()
{
	{
		auto Result = CompileHandler(Handler(
			"out.add_declaration(ctx.top_scope(),\"template<typename T,int N=3> struct GeneratedBuffer{T data[N];template<typename U> U echo(U value){return value;}};\");",
			"std::compiler::phase::types", "std::compiler::operation::add_declaration"),
			"int main(){GeneratedBuffer<int,4> value=GeneratedBuffer<int,4>();value.data[0]=7;print(value.echo(value.data[0]));return 0;}");
		if (!Check(Result.bSuccess, "script processor generates mixed type and method templates"))
		{
			std::cerr << Result.Error.Message << '\n';
			return 1;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		if (!Check(Vm.Execute(*Result.Module, ERunMode::Script) && Output.str() == "7\n", "script-generated mixed templates execute")) return 1;
	}

	{
		auto Generated = CompileHandler(Handler(
			"for(std::compiler::block_info block:ctx.blocks()){"
			"if(block.module_id()==ctx.module_id() && len(block.statements())>0){"
			"auto anchor=block.statements()[0];assert(anchor.range().source_id!=\"\");"
			"out.add_declaration(ctx.block_scope(block.scope_id(),anchor.id()),\"struct Generated{int value=7;};\");return;}}",
			"std::compiler::phase::types", "std::compiler::operation::add_declaration"),
			"int main(){Generated value=Generated();print(value.value);return 0;}");
		if (!Check(Generated.bSuccess, "script processors can generate a local type at a block boundary"))
		{
			std::cerr << Generated.Error.Message << '\n';
			return 1;
		}
		std::ostringstream Output;
		VeroScriptVm Machine(Output);
		VeroScript::Test::Configure(Machine);
		if (!Check(Machine.Execute(*Generated.Module, ERunMode::Script) && Output.str() == "7\n",
			"script-generated local type executes"))
		{
			std::cerr << Machine.GetError().Message << '\n';
			return 1;
		}
	}
	{
		auto Options = SemanticCompileOptions();
		auto EmptyAnnotation = CompileHandler(
			Handler("assert(!ctx.annotation().matches_name(\"\"));", "std::compiler::phase::types", ""),
			"int main(){return 0;}", Options);
		if (!Check(EmptyAnnotation.bSuccess, "invalid compilation annotations do not match an empty name"))
		{
			std::cerr << EmptyAnnotation.Error.Message << '\n';
			return 1;
		}
	}
	{
		std::string Extension = Builder;
		Extension.insert(Extension.find("a.name=\"@Builder\";"), "a.case_sensitive=false;");
		Extension.insert(Extension.find("std::compiler::subscription s"),
			"assert(!reg.find_annotation(\"@BUILDER\").case_sensitive());");
		auto Configured = CompileHandler(Extension,
			"@bUiLdEr class Card{public:string text;};int main(){Card c=Card::Create().Text(\"ok\").Build();assert(c.text==\"ok\");assert(type<Card>().has_annotation(\"BUILDER\"));return 0;}");
		if (!Check(Configured.bSuccess, "script registration exposes annotation name policy"))
		{
			std::cerr << Configured.Error.Message << '\n';
			return 1;
		}
		std::ostringstream Out;
		VeroScriptVm Machine(Out);
		VeroScript::Test::Configure(Machine);
		if (!Check(Machine.Execute(*Configured.Module, ERunMode::Script),
				"script processor dispatch and reflection share the name policy"))
		{
			std::cerr << Machine.GetError().Message << '\n';
			return 1;
		}
	}
	auto O = SemanticCompileOptions();
	O.SourceOverlays = {{"E:/VeroScriptTests/builder.vs", Builder}};
	auto C = VeroScriptCompile::CompileSource(
		"import compiler \"builder.vs\";\n@Builder class Card {public:string text;}; int main(){Card "
		"c=Card::Create().Text(\"Save\").Build();print(c.text);return 0;}",
		"E:/VeroScriptTests/main.vs", O);
	if (!C.bSuccess)
	{
		std::cerr << C.Error.Message << '\n';
		return 1;
	}
	std::ostringstream Output;
	VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
	Vm.SetExecutionBackend(EExecutionBackend::Vm);
	if (!Check(Vm.Execute(*C.Module, ERunMode::Script), "execute builder") ||
		!Check(Output.str().find("Save") != std::string::npos, "builder result"))
	{
		return 1;
	}

	bool Definition = false;
	for (const auto& D : C.PreparedProgram->GetSourceMetadata().Definitions)
	{
		if (D.Name == "@Builder")
		{
			Definition = D.Targets == 2 && D.Documentation == "builder documentation";
		}
	}

	if (!Check(Definition, "annotation description"))
	{
		return 1;
	}

	const auto Catalog = GetScriptCompileServiceCatalog();
	if (!Check(Catalog->FindType("std.compiler.context") != nullptr, "compiler analysis catalog"))
	{
		return 1;
	}

	auto Extension = CompileScriptCompileExtensionSource(Builder, "E:/VeroScriptTests/builder.vs", {});
	if (!Extension.bSuccess)
	{
		std::cerr << Extension.Error.Message << '\n';
		return 1;
	}
	FPackageBuildOptions PackageOptions;
	PackageOptions.Kind = EPackageKind::Library;
	PackageOptions.PackageName = "Builder";
	PackageOptions.bStripDebugInfo = true;
	PackageOptions.CompileExtension = FCompileExtensionManifest{};
	std::vector<std::uint8_t> Bytes;
	std::string Error;
	if (!BuildPackage(*Extension.Module, PackageOptions, Bytes, Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	FPackageData Package;
	if (!ReadPackage(FByteView(Bytes.data(), Bytes.size()), Package, Error))
	{
		std::cerr << Error << '\n';
		return 1;
	}
	if (!Check(Package.CompileExtension.has_value(), "extension package manifest"))
	{
		return 1;
	}

	// Malformed manifest lengths are ordinary package errors, not exceptions.
	const auto ValidExtensionBytes = Bytes;
	const auto ManifestOffset = 28 + PackageOptions.PackageName.size();
	try
	{
		for (const auto Length : {std::uint32_t{65537}, std::uint32_t{0xffffffff}})
		{
			auto Corrupt = ValidExtensionBytes;
			for (unsigned I = 0; I < 4; ++I)
			{
				Corrupt[ManifestOffset + 4 + I] = static_cast<std::uint8_t>(Length >> (I * 8));
			}

			if (!Check(
					!ReadPackage(FByteView(Corrupt.data(), Corrupt.size()), Package, Error) && !Error.empty(),
					"corrupt manifest length rejected"))
			{
				return 1;
			}
		}
		if (!Check(!ReadPackage(FByteView(Bytes.data(), ManifestOffset + 6), Package, Error),
				"truncated manifest rejected"))
		{
			return 1;
		}

		auto OversizedOptions = PackageOptions;
		OversizedOptions.CompileExtension->RegistrationEntry.assign(65537, 'x');
		std::vector<std::uint8_t> RejectedBytes;
		if (!Check(!BuildPackage(*Extension.Module, OversizedOptions, RejectedBytes, Error) &&
					RejectedBytes.empty(),
				"oversized manifest input rejected"))
		{
			return 1;
		}

	}
	catch (const std::exception& Exception)
	{
		std::cerr << "package validation escaped: " << Exception.what() << '\n';
		return 1;
	}
	auto Path = std::filesystem::temp_directory_path() / "veroscript_builder_extension_test.VsLiB";
	{
		std::ofstream F(Path, std::ios::binary);
		F.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
	}
	auto Packed = SemanticCompileOptions();
	Packed.ScriptCompileExtensions.Roots.push_back({Path.generic_string(), {}});
	auto FromPackage = VeroScriptCompile::CompileSource(
		"@Builder class Card {public:string text;}; int main(){Card "
		"c=Card::Create().Text(\"Package\").Build();print(c.text);return 0;}",
		"E:/VeroScriptTests/package_main.vs", Packed);
	std::filesystem::remove(Path);
	if (!FromPackage.bSuccess)
	{
		std::cerr << FromPackage.Error.Message << '\n';
		return 1;
	}
	std::ostringstream PackageOutput;
	VeroScriptVm PackageVm(PackageOutput);
		VeroScript::Test::Configure(PackageVm);
	if (!Check(PackageVm.Execute(*FromPackage.Module, ERunMode::Script), "execute package generated code") ||
		!Check(PackageOutput.str().find("Package") != std::string::npos, "package generated result"))
	{
		return 1;
	}

	// Binary helper changes must invalidate the bootstrap cache regardless of suffix case.
	const auto HelperPath = std::filesystem::temp_directory_path() /
		("veroscript_cached_helper_" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".VsLiB");
	const auto HandlerPath = HelperPath.parent_path() / (HelperPath.stem().generic_string() + ".vs");
	auto HelperOptions = SemanticCompileOptions();
	HelperOptions.SourceOverlays = {{HandlerPath.generic_string(),
		"import \"" + HelperPath.generic_string() + "\";" +
			Handler("out.add_declaration(ctx.top_scope(), GENERATED_METHOD);",
				"std::compiler::phase::members", "std::compiler::operation::add_declaration")}};
	HelperOptions.ScriptCompileExtensions.Roots = {{HandlerPath.generic_string(), {}}};
	for (const int Expected : {7, 7, 9})
	{
		auto Helper = VeroScriptCompile::CompileSource(
			"#define_export GENERATED_METHOD \"int generated(){return " + std::to_string(Expected) + ";}\"\n",
			"cached-helper.vs");
		FPackageBuildOptions HelperPackage;
		HelperPackage.Kind = EPackageKind::Library;
		HelperPackage.PackageName = "CachedHelper";
		HelperPackage.bStripDebugInfo = true;
		std::vector<std::uint8_t> HelperBytes;
		if (!Check(Helper.bSuccess && BuildPackage(*Helper.Module, HelperPackage, HelperBytes, Error),
				"build binary compiler helper"))
		{
			return 1;
		}

		{
			std::ofstream File(HelperPath, std::ios::binary);
			File.write(reinterpret_cast<const char*>(HelperBytes.data()), HelperBytes.size());
			if (!Check(File.good(), "write binary compiler helper"))
			{
				return 1;
			}
		}
		auto Generated = VeroScriptCompile::CompileSource(
			"int main(){return generated();}", "cached-helper-main.vs", HelperOptions);
		bool HasEntryDocument = false;
		if (Generated.Module && Generated.Module->SourceMetadata)
		{
			for (const auto& Document : Generated.Module->SourceMetadata->Documents)
			{
				HasEntryDocument |= Document.ModuleId == Generated.Module->EntryModulePath;
			}
		}

		VeroScriptVm HelperVm(Output);
		VeroScript::Test::Configure(HelperVm);
		if (!Check(Generated.bSuccess, Generated.Error.Message.c_str()) ||
			!Check(Generated.Module->EntryModulePath ==
						Generated.PreparedProgram->GetProgram().EntryModulePath &&
					HasEntryDocument,
				"package-seeded compilation preserves the source entry and metadata") ||
			!Check(HelperVm.Execute(*Generated.Module, ERunMode::Script) &&
					HelperVm.GetProcessExitCode() == Expected,
				"binary helper cache tracks mixed-case library updates"))
		{
			std::filesystem::remove(HelperPath);
			return 1;
		}
	}
	std::filesystem::remove(HelperPath);
	auto State = CompileHandler(Handler("calls+=1;if(ctx.phase()==std::compiler::phase::bodies && "
										"calls!=5)out.report_diagnostic(\"lost extension state\",0);",
		"std::compiler::phase::raw_source,std::compiler::phase::active_source,std::compiler::"
		"phase::types,std::compiler::phase::members,std::compiler::phase::bodies",
		"", "int calls=0;"));
	if (!State.bSuccess)
	{
		std::cerr << State.Error.Message << '\n';
		return 1;
	}
	auto Raw = CompileHandler(
		Handler(
			"int p=std::strings::find(ctx.source().text(),\"MAGIC\");if(p>=0){std::compiler::source_range "
			"r=std::compiler::source_range();r.source_id=ctx.module_id();r.begin=p;r.end=p+5;out.edit_source(r,"
			"\"7\");}",
			"std::compiler::phase::raw_source", "std::compiler::operation::edit_source"),
		"int main(){return MAGIC;}");
	if (!Raw.bSuccess)
	{
		std::cerr << Raw.Error.Message << '\n';
		return 1;
	}
	VeroScriptVm RawVm(Output);
		VeroScript::Test::Configure(RawVm);
	if (!Check(RawVm.Execute(*Raw.Module, ERunMode::Script) && RawVm.GetProcessExitCode() == 7,
			"raw source transform"))
	{
		return 1;
	}

	auto Denied =
		CompileHandler(Handler("int64 now=std::chrono::now();", "std::compiler::phase::bodies", ""));
	if (!Check(!Denied.bSuccess && Denied.Error.Message.find("non-deterministic") != std::string::npos,
			"clock permission"))
	{
		return 1;
	}

	auto Budget = SemanticCompileOptions();
	Budget.ScriptCompileExtensions.MaxInstructions = 500;
	auto Limited = CompileHandler(
		Handler("while(true){}", "std::compiler::phase::bodies", ""), "int main(){return 0;}", Budget);
	if (!Check(!Limited.bSuccess && Limited.Error.Message.find("instruction") != std::string::npos,
			"total instruction budget"))
	{
		return 1;
	}

	auto Dependencies = SemanticCompileOptions();
	Dependencies.CompileProcessing.ReadDependency = [](std::string_view P, std::string& S)
	{
		S = P.find("missing") != P.npos ? "" : "data";
		return !S.empty();
	};
	auto Read = CompileHandler(
		Handler(
			"auto a=std::compiler::read_dependency(\"data.txt\");auto b=ctx.read_dependency(\"missing.txt\");",
			"std::compiler::phase::bodies", ""),
		"int main(){return 0;}", Dependencies);
	if (!Read.bSuccess)
	{
		std::cerr << Read.Error.Message << '\n';
		return 1;
	}
	std::size_t ResourceCount = 0;
	for (const auto& D : Read.PreparedProgram->GetDependencies())
	{
		if (D.Path.find("data.txt") != D.Path.npos || D.Path.find("missing.txt") != D.Path.npos)
		{
			++ResourceCount;
		}
	}

	if (!Check(ResourceCount == 2, "resource dependency recording"))
	{
		return 1;
	}

	auto Metadata = CompileHandler(Handler(
		"if(ctx.phase()==std::compiler::phase::members){std::compiler::metadata "
		"m=std::compiler::metadata();std::compiler::annotation "
		"a=std::compiler::annotation();a.name=\"@Generated\";m.annotations_present=true;m.annotations=array<std::"
		"compiler::annotation>{a};out.set_metadata(ctx.find_declaration(ctx.declarations()[0].id()).id(),m);}",
		"std::compiler::phase::members", "std::compiler::operation::set_metadata"));
	if (!Metadata.bSuccess)
	{
		std::cerr << Metadata.Error.Message << '\n';
		return 1;
	}
	auto Generic = CompileHandler(
		Handler("out.add_declaration(ctx.top_scope(),\"template<T> class GeneratedBox {public:T value;};\");",
			"std::compiler::phase::types", "std::compiler::operation::add_declaration"),
		"int main(){GeneratedBox<int> b=GeneratedBox<int>();b.value=19;return b.value;}");
	if (!Generic.bSuccess)
	{
		std::cerr << Generic.Error.Message << '\n';
		return 1;
	}
	VeroScriptVm GenericVm(Output);
		VeroScript::Test::Configure(GenericVm);
	if (!Check(GenericVm.Execute(*Generic.Module, ERunMode::Script) && GenericVm.GetProcessExitCode() == 19,
			"generated generic type"))
	{
		return 1;
	}

	auto Virtual =
		CompileHandler(Handler("auto ds=ctx.declarations();for(int i=0;i<len(ds);i+=1){auto "
							   "d=ds[i];if(d.target()==std::compiler::target::method && !d.has_body() && "
							   "!d.is_pure_virtual())out.complete_body(d.id(),\"{return 1;}\");}",
						   "std::compiler::phase::bodies", "std::compiler::operation::complete_body"),
			"class Base{public:virtual int Get();};class Derived:public Base{public:int Get() "
			"override{return 23;}};int main(){Base b=Derived();return b.Get();}");
	if (!Virtual.bSuccess)
	{
		std::cerr << Virtual.Error.Message << '\n';
		return 1;
	}
	VeroScriptVm VirtualVm(Output);
		VeroScript::Test::Configure(VirtualVm);
	if (!Check(VirtualVm.Execute(*Virtual.Module, ERunMode::Script) && VirtualVm.GetProcessExitCode() == 23,
			"virtual generated default and override"))
	{
		return 1;
	}

	auto Cancel = SemanticCompileOptions();
	auto CleanupExtension = Handler("if(kept==nullptr)return;", "std::compiler::phase::bodies", "",
		"class Broken{public:~Broken(){int zero=0;int value=1/zero;}};");
	const std::string FactoryPrefix = "callback create(){return []";
	const auto FactoryOffset = CleanupExtension.find(FactoryPrefix);
	if (!Check(FactoryOffset != std::string::npos, "cleanup test callback factory"))
	{
		return 1;
	}

	CleanupExtension.replace(
		FactoryOffset, FactoryPrefix.size(), "callback create(){Broken kept=Broken();return [kept]");
	auto FailedCleanup = CompileHandler(CleanupExtension);
	if (!Check(!FailedCleanup.bSuccess &&
				FailedCleanup.Error.Message.find("compiler extension cleanup failed: division by zero") !=
					std::string::npos,
			"callable cleanup failures retain their diagnostic when persistent-state cleanup succeeds"))
	{
		std::cerr << FailedCleanup.Error.Message << '\n';
		return 1;
	}
	Cancel.ScriptCompileExtensions.ExecutionControl = std::make_shared<FExecutionControl>();
	Cancel.ScriptCompileExtensions.ExecutionControl->RequestCancel();
	auto Cancelled =
		CompileHandler(Handler("", "std::compiler::phase::bodies", ""), "int main(){return 0;}", Cancel);
	if (!Check(!Cancelled.bSuccess && Cancelled.Error.Message.find("cancel") != std::string::npos,
			"extension cancellation"))
	{
		return 1;
	}

	auto Late = SemanticCompileOptions();
	Late.SourceOverlays = {
		{"E:/VeroScriptTests/late.vs", "import compiler \"builder.vs\";export int value(){return 1;}"},
		{"E:/VeroScriptTests/builder.vs", Builder}};
	auto LateImport = VeroScriptCompile::CompileSource(
		"import \"late.vs\";int main(){return value();}", "E:/VeroScriptTests/main.vs", Late);
	if (!Check(!LateImport.bSuccess, "late extension discovery rejected"))
	{
		return 1;
	}

	auto Cycle = SemanticCompileOptions();
	Cycle.SourceOverlays = {
		{"E:/VeroScriptTests/a.vs",
			"import compiler \"b.vs\";export void register_extension(std::compiler::registration r){}"},
		{"E:/VeroScriptTests/b.vs",
			"import compiler \"a.vs\";export void register_extension(std::compiler::registration r){}"}};
	Cycle.ScriptCompileExtensions.Roots = {{"E:/VeroScriptTests/a.vs", {}}};
	auto Cyclic =
		VeroScriptCompile::CompileSource("int main(){return 0;}", "E:/VeroScriptTests/main.vs", Cycle);
	if (!Check(!Cyclic.bSuccess && Cyclic.Error.Message.find("cyclic") != std::string::npos,
			"extension cycle rejected"))
	{
		return 1;
	}

	O.ScriptCompileExtensions.bAllowSourceImports = false;
	auto Disabled = VeroScriptCompile::CompileSource(
		"import compiler \"builder.vs\"; int main(){return 0;}", "E:/VeroScriptTests/main.vs", O);
	if (!Check(!Disabled.bSuccess, "disabled source imports"))
	{
		return 1;
	}

	// Value descriptors must be independently mutable after copying.
	auto Values = CompileHandler(Handler(
		"std::compiler::source_range a=std::compiler::source_range();a.begin=2;"
		"std::compiler::source_range b=a;b.begin=5;if(a.begin!=2)out.report_diagnostic(\"aliased descriptor\",0);"
		"if(!ctx.source().is_valid()||len(ctx.source().line_offsets())==0)out.report_diagnostic(\"missing source "
		"query\",0);",
		"std::compiler::phase::bodies", ""));
	if (!Check(Values.bSuccess, Values.Error.Message.c_str()))
	{
		return 1;
	}

	auto CachedOptions = SemanticCompileOptions();
	CachedOptions.SourceOverlays = {
		{"E:/VeroScriptTests/cached.vs", Handler("", "std::compiler::phase::bodies", "")}};
	CachedOptions.ScriptCompileExtensions.Roots = {{"E:/VeroScriptTests/cached.vs", {}}};
	auto Cached = VeroScriptCompile::CompileSource(
		"int main(){return 0;}", "E:/VeroScriptTests/cache_main.vs", CachedOptions);
	if (!Check(Cached.bSuccess, Cached.Error.Message.c_str()))
	{
		return 1;
	}

	std::vector<FScriptError> CachedDiagnostics;
	auto Task = LoadScriptCompileExtensions(
		"int main(){return 0;}", "E:/VeroScriptTests/cache_main.vs", CachedOptions, CachedDiagnostics);
	if (!Check(Task && Task->MatchesPreparedProgram(*Cached.PreparedProgram),
			"prepared cache validates fresh registration, factories and dependencies"))
	{
		return 1;
	}

	if (!Check(Task->Close(CachedDiagnostics), "cache-hit task cleanup"))
	{
		return 1;
	}

	auto Expired = CompileHandler(
		Handler("if(len(saved)==0)saved=array<std::compiler::context>{ctx};else saved[0].phase();",
			"std::compiler::phase::raw_source,std::compiler::phase::active_source", "",
			"array<std::compiler::context> saved=array<std::compiler::context>{};"));
	if (!Check(!Expired.bSuccess && Expired.Error.Message.find("expired") != std::string::npos,
			"expired context rejected"))
	{
		return 1;
	}

	auto ExpiredScope = CompileHandler(Handler(
		"if(ctx.phase()==std::compiler::phase::types)saved=array<std::compiler::scope>{ctx.top_scope()};"
		"else out.add_declaration(saved[0],\"int generated(){return 1;}\");",
		"std::compiler::phase::types,std::compiler::phase::members",
		"std::compiler::operation::add_declaration",
		"array<std::compiler::scope> saved=array<std::compiler::scope>{};"));
	if (!Check(!ExpiredScope.bSuccess && ExpiredScope.Error.Message.find("expired") != std::string::npos,
			"expired scope rejected"))
	{
		return 1;
	}

	auto NamespaceOptions = SemanticCompileOptions();
	NamespaceOptions.SourceOverlays = O.SourceOverlays;
	auto Namespace = VeroScriptCompile::CompileSource(
		"import compiler \"builder.vs\";namespace Demo{@Builder class Card{public:string text;};}"
		"int main(){Demo::Card c=Demo::Card::Create().Text(\"Scoped\").Build();print(c.text);return 0;}",
		"E:/VeroScriptTests/main.vs", NamespaceOptions);
	if (!Check(Namespace.bSuccess, Namespace.Error.Message.c_str()))
	{
		return 1;
	}

	VeroScriptVm NamespaceVm(Output);
		VeroScript::Test::Configure(NamespaceVm);
	if (!Check(NamespaceVm.Execute(*Namespace.Module, ERunMode::Script), "namespace builder execution"))
	{
		return 1;
	}

	std::string AllReads =
		Handler("auto r=ctx.read_dependency(\"callback.txt\");", "std::compiler::phase::bodies", "",
			"std::compiler::dependency initial=std::compiler::read_dependency(\"init.txt\");");
	AllReads.replace(AllReads.find("callback create(){"), std::string("callback create(){").size(),
		"callback create(){auto f=std::compiler::read_dependency(\"factory.txt\");");
	AllReads.replace(AllReads.find("reg.register_processor(d,create);"),
		std::string("reg.register_processor(d,create);").size(),
		"auto r=std::compiler::read_dependency(\"registration.txt\");reg.register_processor(d,create);");
	auto Ledger = CompileHandler(AllReads, "int main(){return 0;}", Dependencies);
	if (!Check(Ledger.bSuccess, Ledger.Error.Message.c_str()))
	{
		return 1;
	}

	std::size_t Count = 0;
	for (const auto& D : Ledger.PreparedProgram->GetDependencies())
	{
		if (D.Path.find(".txt") != D.Path.npos)
		{
			++Count;
		}
	}

	if (!Check(Count == 4, "initialization, registration, factory and callback dependencies recorded"))
	{
		return 1;
	}

	auto VolatileOptions = SemanticCompileOptions();
	FHostBindingRegistry ExtraBindings;
	VolatileOptions.ScriptCompileExtensions.Bindings = &ExtraBindings;
	auto Volatile = CompileHandler(
		Handler("", "std::compiler::phase::bodies", ""), "int main(){return 0;}", VolatileOptions);
	if (!Check(Volatile.bSuccess && !Volatile.PreparedProgram->IsCacheable(),
			"non-deterministic extra services disable caching"))
	{
		return 1;
	}

	auto Open = CompileHandler(
		Handler("out.add_declaration(ctx.top_scope(),\"class Generated<T>{public:T value;};\");",
			"std::compiler::phase::types", "std::compiler::operation::add_declaration"),
		"export T Build<T>(T value){Generated<T> g=Generated<T>();g.value=value;return g.value;}");
	if (!Check(Open.bSuccess, Open.Error.Message.c_str()))
	{
		return 1;
	}

	FPackageBuildOptions OpenOptions;
	OpenOptions.Kind = EPackageKind::Library;
	OpenOptions.PackageName = "GeneratedGeneric";
	OpenOptions.bStripDebugInfo = true;
	if (!Check(BuildPackage(*Open.Module, OpenOptions, Bytes, Error), Error.c_str()))
	{
		return 1;
	}

	auto OpenPath = std::filesystem::temp_directory_path() / "veroscript_script_extension_open_generic.vslib";
	{
		std::ofstream File(OpenPath, std::ios::binary);
		File.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
	}
	auto Consumer = VeroScriptCompile::CompileSource(
		"import \"" + OpenPath.generic_string() + "\";int main(){return Build<int>(29);}");
	std::filesystem::remove(OpenPath);
	if (!Check(Consumer.bSuccess, Consumer.Error.Message.c_str()))
	{
		return 1;
	}

	VeroScriptVm ConsumerVm(Output);
		VeroScript::Test::Configure(ConsumerVm);
	if (!Check(
			ConsumerVm.Execute(*Consumer.Module, ERunMode::Script) && ConsumerVm.GetProcessExitCode() == 29,
			"open generic consumer needs no extension VM"))
	{
		return 1;
	}

	// The external cancellation predicate must work when both ordinary limits are unlimited.
	auto Endless = VeroScriptCompile::CompileSource("int main(){while(true){}return 0;}");
	if (!Check(Endless.bSuccess, Endless.Error.Message.c_str()))
	{
		return 1;
	}

	VeroScriptVm PredicateVm(Output);
		VeroScript::Test::Configure(PredicateVm);
	int Polls = 0;
	PredicateVm.SetExecutionCheck(
		[&]
		{
			return ++Polls < 30;
		});
	if (!Check(!PredicateVm.Execute(*Endless.Module, ERunMode::Script) && Polls == 30,
			"unlimited execution still checks external cancellation"))
	{
		return 1;
	}

	// A malformed extension entry must be rejected before its initializer can run.
	auto InvalidModule = *Extension.Module;
	for (auto& F : InvalidModule.Functions)
	{
		if (F.ClosureTypeName == "fn(std.compiler.registration)->void")
		{
			F.ClosureTypeName = "fn(int)->void";
		}
	}

	if (!Check(
			!BuildPackage(InvalidModule, PackageOptions, Bytes, Error), "package entry signature validation"))
	{
		return 1;
	}

	if (!Check(HasScriptCompileExtensionImport("/* root */import /* marker */compiler \"x.vs\";"),
			"stable prefix recognition with comments"))
	{
		return 1;
	}

	FHostBindingRegistry FunctionBindings;
	FHostTypeId Provider;
	if (!Check(FunctionBindings.RegisterType("FactoryProvider", EHostTypeKind::Class, Provider, Error),
			Error.c_str()))
	{
		return 1;
	}

	FHostMemberDesc Constructor;
	Constructor.Name = "FactoryProvider";
	Constructor.Kind = EHostMemberKind::Constructor;
	Constructor.ReturnType = ENativeType::HostObject;
	Constructor.ReturnHostTypeName = "FactoryProvider";
	Constructor.Callback = BindingCatalogStubNative;
	FHostMemberDesc FactoryProperty;
	FactoryProperty.Name = "factory";
	FactoryProperty.Kind = EHostMemberKind::PropertyGetter;
	FactoryProperty.ReturnType = ENativeType::Function;
	FactoryProperty.ReturnHostTypeName = "fn()->fn(std.compiler.context,std.compiler.output)->void";
	FactoryProperty.Callback = BindingCatalogStubNative;
	if (!Check(FunctionBindings.RegisterMember(Provider, Constructor, Error) &&
				FunctionBindings.RegisterMember(Provider, FactoryProperty, Error),
			Error.c_str()))
	{
		return 1;
	}

	std::string PropertySource = Handler("", "std::compiler::phase::bodies", "");
	PropertySource.replace(PropertySource.find("reg.register_processor(d,create)"),
		std::string("reg.register_processor(d,create)").size(),
		"FactoryProvider p=FactoryProvider();reg.register_processor(d,p.factory)");
	auto PropertyOptions = SemanticCompileOptions();
	PropertyOptions.ScriptCompileExtensions.Bindings = &FunctionBindings;
	auto FunctionProperty =
		CompileScriptCompileExtensionSource(PropertySource, "function-property.vs", PropertyOptions);
	if (!Check(FunctionProperty.bSuccess, FunctionProperty.Error.Message.c_str()))
	{
		return 1;
	}

	auto CommentError = VeroScriptCompile::CompileSource("/* never closed\n", "unterminated-comment.vs");
	if (!Check(!CommentError.bSuccess && CommentError.Error.Message.find("unterminated") != std::string::npos,
			"prefix probing preserves lexer errors"))
	{
		return 1;
	}

	auto ManifestOptions = SemanticCompileOptions();
	const std::string Registration =
		"import \"compiler.vs\";export void register_extension(std::compiler::registration reg){}";
	ManifestOptions.SourceOverlays = {{"E:/VeroScriptTests/dependency.vs", Registration}};
	ManifestOptions.ScriptCompileExtensions.Roots = {{"dependency.vs", {}}};
	auto SharedDependency =
		CompileScriptCompileExtensionSource("import compiler \"dependency.vs\";" + Registration,
			"E:/VeroScriptTests/producer.vs", ManifestOptions);
	if (!Check(SharedDependency.bSuccess && SharedDependency.CompileExtensionManifest &&
				SharedDependency.CompileExtensionManifest->Dependencies.size() == 1,
			"manifest deduplicates configured and source dependencies"))
	{
		return 1;
	}

	auto SharedPackageOptions = PackageOptions;
	SharedPackageOptions.CompileExtension = *SharedDependency.CompileExtensionManifest;
	if (!Check(BuildPackage(*SharedDependency.Module, SharedPackageOptions, Bytes, Error), Error.c_str()))
	{
		return 1;
	}

	ManifestOptions.ScriptCompileExtensions.Roots = {{"producer.vs", {}}};
	auto SelfRoot =
		CompileScriptCompileExtensionSource(Registration, "E:/VeroScriptTests/producer.vs", ManifestOptions);
	if (!Check(SelfRoot.bSuccess && SelfRoot.CompileExtensionManifest &&
				SelfRoot.CompileExtensionManifest->Dependencies.empty(),
			"manifest excludes the configured producer root"))
	{
		return 1;
	}

	const auto RestrictedExtension = std::filesystem::temp_directory_path() / "veroscript-allowed-root.vs";
	auto RootOptions = SemanticCompileOptions();
	RootOptions.SourceOverlays = {{RestrictedExtension.generic_string(),
		"import \"compiler.vs\";export void register_extension(std::compiler::registration reg){"
		"auto resource=std::compiler::read_dependency(\"allowed-root-input.txt\");}"}};
	RootOptions.CompileProcessing.ReadDependency = [](std::string_view, std::string& Content)
	{
		Content = "allowed";
		return true;
	};
	RootOptions.ScriptCompileExtensions.Roots = {{RestrictedExtension.generic_string(), {}}};
	RootOptions.ScriptCompileExtensions.AllowedRoots = {RestrictedExtension.root_path().generic_string()};
	auto AllowedRoot =
		VeroScriptCompile::CompileSource("int main(){return 0;}", "allowed-root.vs", RootOptions);
	if (!Check(AllowedRoot.bSuccess, "filesystem root includes its descendant extensions"))
	{
		return 1;
	}

	RootOptions.ScriptCompileExtensions.AllowedRoots = {
		RestrictedExtension.parent_path().generic_string() + "-other"};
	if (!Check(!VeroScriptCompile::CompileSource("int main(){return 0;}", "allowed-root.vs", RootOptions)
				   .bSuccess,
			"a directory prefix is not an allowed ancestor"))
	{
		return 1;
	}

	RootOptions.ScriptCompileExtensions.AllowedRoots = {""};
	if (!Check(!VeroScriptCompile::CompileSource("int main(){return 0;}", "allowed-root.vs", RootOptions)
				   .bSuccess,
			"an empty path does not authorize a filesystem root"))
	{
		return 1;
	}

	std::cout << "script compilation extensions passed\n";
	return 0;
}
