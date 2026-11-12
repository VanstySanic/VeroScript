// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptTooling.h"
#include "VeroScriptWorkspace.h"
#include "VeroScriptCompile.h"
#include "VeroScriptAst.h"
#include "VeroScriptPath.h"
#include "../VeroScriptOverlayUtil.h"
#include "VeroScriptProcessingToolingFixture.h"
#include "../Internal/VeroScriptSymbolQuery.h"
#include "../Internal/VeroScriptDocumentSymbols.h"
#include "../Internal/VeroScriptSourceMetadataBuiltins.h"
#include "../Internal/VeroScriptSignatureHelp.h"
#include "../Internal/VeroScriptToolingCompile.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace VeroScript;
using namespace VeroScript::Tooling;
namespace
{
	class FStatefulSource final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
		{
			if (Context.Source->Text.find("/*stateful-source*/") == std::string::npos) return;
			Output.EditSource({Context.Source->Id, 0, 0, {}}, std::string(++*Calls * 2, '\n'));
		}

		explicit FStatefulSource(std::shared_ptr<unsigned> InCalls) : Calls(std::move(InCalls))
		{
		}

	private:
		std::shared_ptr<unsigned> Calls;
	};
	FCompileProcessingOptions StatefulProcessing(const std::shared_ptr<unsigned>& Calls,
	    ECompileProcessingPhase Phase = ECompileProcessingPhase::ActiveSource)
	{
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Desc;
		Desc.ProcessorId = "tooling.test.stateful-source";
		Desc.Version = "1";
		Desc.Phases = CompilePhaseMask(Phase);
		Desc.Operations = CompileOperationMask(ECompileOperation::EditSource);
		Desc.Subscriptions.push_back({ECompileProcessingEvent::Module});
		Desc.Factory = [Calls]
		{
			return std::make_unique<FStatefulSource>(Calls);
		};
		Desc.Cacheable = false;
		std::string Error;
		if (!Registry->Register(std::move(Desc), Error))
		{
			throw std::runtime_error(Error);
		}

		Registry->Freeze();
		FCompileProcessingOptions Options;
		Options.Registry = std::move(Registry);
		return Options;
	}
	struct FTemporarySource
	{
		~FTemporarySource()
		{
			std::error_code Error;
			std::filesystem::remove(Path, Error);
		}

		std::filesystem::path Path = std::filesystem::temp_directory_path() /
		    ("vero-tooling-stateful-" +
		        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".vs");
	};
	class FLegacyRead final : public IAnnotationProcessor
	{
	public:
		void Process(const FAnnotationContext& Context, FAnnotationOutput& Output) override
		{
			std::string Name;
			Context.ReadDependency("legacy-name", Name);
			if (Name != "Read")
			{
				throw std::runtime_error("legacy adapter did not use the general dependency reader");
			}

			Output.SubmitMethod(
			    Context.Owner->Id, EMemberAccess::Public, "string Read() const {return text;}");
		}
	};
	bool Expect(bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "VeroScriptCompileProcessingToolingTests: " << Message << '\n';
		}

		return Condition;
	}
	FTextPosition At(std::string_view Source, std::string_view Text, std::size_t Offset = 0)
	{
		const auto End = Source.find(Text) + Offset;
		FTextPosition P{1, 1};
		for (std::size_t I = 0; I < End; ++I)
		{
			if (Source[I] == '\n')
			{
				++P.Line;
				P.Column = 1;
			}
			else
			{
				++P.Column;
			}
		}

		return P;
	}
}
int main()
{
	{
		// Retained processor metadata must never project another document's
		// declaration extent onto a declaration's current source location.
		FProgram Program;
		Program.EntryModulePath = "declaration-owner.vs";
		FClassDecl Type;
		Type.Name = "Owner";
		Type.ModulePath = Program.EntryModulePath;
		Type.Location = {Program.EntryModulePath, 1, 1};
		Type.DeclMeta.SourceRange = {"previous.vs", 0, 20,
		    {{"previous.vs", 1, 1}, {"previous.vs", 2, 8}}};
		Program.Classes.push_back(std::move(Type));
		const auto Foreign = Detail::VeroScriptDocumentSymbols::CollectFromProgram(Program);
		if (!Expect(Foreign.bSuccess && Foreign.Symbols.size() == 1 && Foreign.Symbols.front().DeclarationRange.StartLine == 0,
		        "foreign processor source extents are rejected instead of reinterpreted"))
		{
			return EXIT_FAILURE;
		}
		Program.Classes.front().DeclMeta.SourceRange = {Program.EntryModulePath, 0, 20,
		    {{Program.EntryModulePath, 1, 1}, {Program.EntryModulePath, 2, 8}}};
		const auto Local = Detail::VeroScriptDocumentSymbols::CollectFromProgram(Program);
		if (!Expect(Local.bSuccess && Local.Symbols.size() == 1 && Local.Symbols.front().DeclarationRange.StartLine == 1 && Local.Symbols.front().DeclarationRange.EndLine == 2,
		        "matching declaration documents retain their complete source extent"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FAnnotationDefinition Definition;
		Definition.Id = "tooling.annotation-name-policy";
		Definition.Name = "@GuiWidget";
		Definition.CaseSensitive = false;
		Definition.Documentation = "Widget documentation";
		Definition.Arguments = {{"Label", "string", "Label docs", {}}, {"Enabled", "bool", "Enabled docs", {}}};
		FWorkspaceOptions Config;
		Config.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Config.CompileProcessing.AnnotationDefinitions = {Definition};
		VeroScriptWorkspace Query;
		const std::string Text = "@gUIwIDGET(Label=1,Enabled=true) class Card{};void main(){}";
		Query.OpenDocument("name-policy.vs", Text);
		for (const bool CaseSensitive : {false, true, false})
		{
			Config.CompileProcessing.AnnotationDefinitions.front().CaseSensitive = CaseSensitive;
			Query.SetOptions(Config);
			const auto Hover = Query.HoverDocument("name-policy.vs", At(Text, "@gUIwIDGET", 3));
			const auto AnnotationCount = std::count_if(Hover.Definitions.begin(), Hover.Definitions.end(),
			    [](const auto& Hit)
			{
				return Hit.Kind == EOutlineSymbolKind::Annotation;
			});
			if (!Expect(AnnotationCount == (CaseSensitive ? 0 : 1),
			        "annotation hover obeys changed name policy without duplicate hits"))
			{
				return EXIT_FAILURE;
			}
			const auto Help = Query.SignatureHelpDocument("name-policy.vs", At(Text, "Enabled=", 2));
			if (!Expect(CaseSensitive ? Help.Signatures.empty()
			                          : Help.bSuccess && Help.Signatures.size() == 1 &&
			                Help.Signatures.front().Label == "@GuiWidget(Label: string, Enabled: bool)" &&
			                Help.Signatures.front().Parameters.size() == 2 && Help.ActiveParameter == 1,
			        "signature help obeys name policy and retains canonical name and parameters"))
			{
				return EXIT_FAILURE;
			}

			const auto ArgumentHover = Query.HoverDocument("name-policy.vs", At(Text, "Label", 2));
			if (!Expect((ArgumentHover.Contents.find("Label docs") != std::string::npos) == !CaseSensitive,
			        "annotation argument lookup obeys the definition's name policy"))
			{
				return EXIT_FAILURE;
			}
		}
		const std::string WrongArgument = "@guiwidget(label=1) class Card{};void main(){}";
		Query.UpdateDocument("name-policy.vs", WrongArgument);
		if (!Expect(Query.HoverDocument("name-policy.vs", At(WrongArgument, "label", 2))
		                .Contents.find("Label docs") == std::string::npos,
		        "argument names remain case sensitive"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(Query.AnalyzeDocument("name-policy.vs").bSuccess,
		        "analysis accepts configured annotation variants"))
		{
			return EXIT_FAILURE;
		}

		FHostBindingRegistry Registry;
		std::string Error;
		if (!Expect(Registry.RegisterAnnotation("@GuiWidget", "", Error, "Widget documentation",
		                {{"Label", "string", "Label docs", {}}}, AnnotationAllTargets, false),
		        "register relaxed host annotation"))
		{
			return EXIT_FAILURE;
		}

		FHoverOptions HoverOptions;
		HoverOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		HoverOptions.Bindings = &Registry;
		const auto HostHover =
		    VeroScriptHover::HoverAtSource(Text, At(Text, "@gUIwIDGET", 3), "host-policy.vs", HoverOptions);
		if (!Expect(std::count_if(HostHover.Definitions.begin(), HostHover.Definitions.end(),
		                [](const auto& Hit)
		{
			return Hit.Kind == EOutlineSymbolKind::Annotation;
		}) == 1,
		        "mixed-case host annotation hover has one canonical hit"))
		{
			return EXIT_FAILURE;
		}

		if (!Expect(
		        Registry.RegisterAnnotation("@LegacyOptions",
		            "annotation (map<string,int> values, string label)", Error, "Legacy annotation options"),
		        "register detail-only annotation"))
		{
			return EXIT_FAILURE;
		}

		FSignatureHelpOptions HelpOptions;
		HelpOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		HelpOptions.Bindings = &Registry;
		const std::string Legacy = "@LegacyOptions(values=1,label=2) class Card{};void main(){}";
		const auto LegacyHelp = VeroScriptCallHelp::HelpAtSource(Legacy, At(Legacy, "label=", 3), "legacy-policy.vs", HelpOptions);
		if (!Expect(LegacyHelp.bSuccess && LegacyHelp.Signatures.size() == 1 &&
		            LegacyHelp.Signatures.front().Parameters.size() == 2 && LegacyHelp.ActiveParameter == 1,
		        "detail-only annotation signatures split top-level parameters"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::vector<std::pair<std::string, std::string>> Cases{
		    {R"(string label="a,b")", R"(string label="a,b")"},
		    {"\n string label", "string label"},
		    {"\r\n string label", "string label"},
		    {"array<int> values={1,2}", "array<int> values={1,2}"},
		    {"map<string,array<int>> values", "map<string,array<int>> values"},
		    {R"vs(string label=r"(a,b)")vs", R"vs(string label=r"(a,b)")vs"},
		    {"char separator=','", "char separator=','"},
		    {"int value=1/* , ) ignored */", "int value=1/* , ) ignored */"},
		    {"fn(int,string)->array<int> callback", "fn(int,string)->array<int> callback"}};
		for (const auto& [Parameters, First] : Cases)
		{
			FHostAnnotationDesc Description;
			Description.Name = "@LegacyOptions";
			Description.Detail = "annotation (" + Parameters + ", bool enabled\r\n)";
			const auto Signature = Detail::MakeAnnotationSignature(Description);
			if (!Expect(Signature.Parameters.size() == 2 && Signature.Parameters.front().Label == First &&
			            Signature.Parameters.back().Label == "bool enabled",
			        "legacy annotation parameter parsing preserves literal punctuation, nested types and line breaks"))
			{
				return EXIT_FAILURE;
			}
		}
	}
	{
		const std::string Source = "void inspect(annotation_info a,annotation_definition_info d){a.matches_name(\"Widget\");d.case_sensitive();}void main(){}";
		const auto Complete = VeroScriptComplete::CompleteAtSource(Source, At(Source, "a.matches_name", 8));
		if (!Expect(std::any_of(Complete.Items.begin(), Complete.Items.end(),
		                [](const auto& Item)
		{
			return Item.Label == "matches_name";
		}),
		        "annotation matching reflection method is completed"))
		{
			return EXIT_FAILURE;
		}

		const auto Match = VeroScriptCallHelp::HelpAtSource(Source, At(Source, "matches_name(", 13));
		if (!Expect(Match.bSuccess && Match.Signatures.size() == 1 &&
		            Match.Signatures.front().Parameters.size() == 1,
		        "annotation matching reflection method has a signature"))
		{
			return EXIT_FAILURE;
		}

		const auto Hover = VeroScriptHover::HoverAtSource(Source, At(Source, "case_sensitive", 4));
		if (!Expect(Hover.bSuccess && Hover.Contents.find("bool") != std::string::npos,
		        "annotation case policy reflection method has the correct return type"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		FScriptCompileExtensionsOptions Explicit;
		Explicit.VmBackendFactory = VeroScript::Test::VmFactory();
		Explicit.bBindingsDeterministic = true;
		if (!Expect(!IsDefaultScriptCompileExtensions(Explicit), "explicit binding determinism is retained when inheriting workspace options")) return EXIT_FAILURE;
		FTemporarySource Extension, Resource;
		const auto ExtensionPath = PathUtil::ToUtf8(Extension.Path), ResourcePath = PathUtil::ToUtf8(Resource.Path);
		std::string ExtensionSource = R"vs(import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
auto dependency=ctx.read_dependency("RESOURCE");
string member=dependency.exists()?dependency.content():"int Read(){return 7;}";
out.add_declaration(ctx.top_scope(),"class Generated{public:"+member+"};");};}
export void register_extension(std::compiler::registration reg){
std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
std::compiler::processor_definition d=std::compiler::processor_definition();d.id="tooling.script.resource";d.version="1";
d.phases=array<std::compiler::phase>{std::compiler::phase::types};
d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,create);})vs";
		ExtensionSource.replace(
		    ExtensionSource.find("RESOURCE"), 8, PathUtil::ToUtf8(Resource.Path.filename()));
		{
			std::ofstream File(Extension.Path, std::ios::binary);
			File << ExtensionSource;
		}
		const std::string Source = "void main(){Generated value=Generated();value.Read;}";
		const auto Label = PathUtil::ToUtf8(Extension.Path.parent_path() / "tooling-script-resource.vs");
		FWorkspaceOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.ScriptCompileExtensions.Roots = {{ExtensionPath, {}}};
		VeroScriptWorkspace Workspace;
		Workspace.SetOptions(Options);
		Workspace.OpenDocument(Label, Source);
		const auto Missing = Workspace.SemanticTokensDocument(Label);
		if (!Expect(Missing.bSuccess && Detail::IsSemanticQuerySnapshotCacheable(Missing.Snapshot) &&
		            Detail::IsSemanticQuerySnapshotCurrent(Missing.Snapshot),
		        "script snapshots retain missing dependency observations"))
		{
			return EXIT_FAILURE;
		}

		{
			std::ofstream File(Resource.Path, std::ios::binary);
			File << "const int Read=7;";
		}
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Missing.Snapshot),
		        "creating a callback dependency invalidates semantic snapshots"))
		{
			return EXIT_FAILURE;
		}
		const auto Existing = Workspace.SemanticTokensDocument(Label);
		if (!Expect(Existing.bSuccess && Detail::IsSemanticQuerySnapshotCurrent(Existing.Snapshot),
		        "fresh script dependency snapshots are usable"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream File(Resource.Path, std::ios::binary);
			File << "int Read(){return 8;}";
		}
		if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Existing.Snapshot),
		        "changing resource contents invalidates semantic snapshots"))
		{
			return EXIT_FAILURE;
		}
		const auto Updated = Workspace.SemanticTokensDocument(Label);
		std::filesystem::remove(Resource.Path);
		if (!Expect(Updated.bSuccess && !Detail::IsSemanticQuerySnapshotCurrent(Updated.Snapshot),
		        "deleting a resource invalidates semantic snapshots"))
		{
			return EXIT_FAILURE;
		}
		Options.CompileProcessing.ReadDependency = [](std::string_view, std::string& Content)
		{
			Content = "int Read(){return 9;}";
			return true;
		};
		Workspace.SetOptions(Options);
		const auto Custom = Workspace.SemanticTokensDocument(Label);
		if (!Expect(Custom.bSuccess && Detail::IsSemanticQuerySnapshotCurrent(Custom.Snapshot) &&
		            !Detail::IsSemanticQuerySnapshotCacheable(Custom.Snapshot),
		        "custom script resource readers do not confuse bootstrap code with resources"))
		{
			return EXIT_FAILURE;
		}
	}
	for (const std::string Newline : {"\n", "\r\n"})
	{
		const auto Calls = std::make_shared<unsigned>(0);
		FAnalyzeOptions Analyze;
		Analyze.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Analyze.CompileProcessing = StatefulProcessing(Calls);
		const auto Source = "/*stateful-source*/" + Newline + "void main(){" + Newline + "print(unknown);" + Newline + "}";
		const auto Verify = [&](const auto& Result)
		{
			return !Result.bSuccess && *Calls == 1 && !Result.Diagnostics.empty() &&
			    Result.Diagnostics.front().Range.StartLine == 3 && Result.Diagnostics.front().Range.StartColumn == 7;
		};
		if (!Expect(Verify(VeroScriptAnalyzer::AnalyzeSource(Source, "stateful.vs", Analyze)),
		        "source analysis maps semantic errors without invoking volatile processors again"))
		{
			return EXIT_FAILURE;
		}

		*Calls = 0;
		FTemporarySource File;
		{
			std::ofstream Out(File.Path, std::ios::binary);
			Out << Source;
		}
		const auto U8Path = File.Path.generic_u8string();
		const std::string FileLabel(reinterpret_cast<const char*>(U8Path.data()), U8Path.size());
		if (!Expect(Verify(VeroScriptAnalyzer::AnalyzeFile(FileLabel, Analyze)),
		        "file analysis reuses its compilation's source maps"))
		{
			return EXIT_FAILURE;
		}

		*Calls = 0;
		FDebugLoadOptions DebugOptions;
		DebugOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		DebugOptions.CompileProcessing = Analyze.CompileProcessing;
		DebugOptions.ScriptCompileExtensions = Analyze.ScriptCompileExtensions;
		VeroScriptDebugSession Debug;
		VeroScript::Test::Configure(Debug);
		if (!Expect(Verify(Debug.LoadSource(Source, "stateful.vs", DebugOptions)),
		        "debug compilation maps errors even without a successful bytecode module"))
		{
			return EXIT_FAILURE;
		}

		*Calls = 0;
		if (!Expect(Verify(Debug.LoadFile(FileLabel, DebugOptions)), "debug file loading retains the same maps")) return EXIT_FAILURE;
	}
	{
		const auto Calls = std::make_shared<unsigned>(0);
		FDebugLoadOptions Options;
		Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Options.CompileProcessing = StatefulProcessing(Calls, ECompileProcessingPhase::RawSource);
		VeroScriptDebugSession Debug;
		VeroScript::Test::Configure(Debug);
		const std::string Source = "#if 1\nvoid main(){/*stateful-source*/\nprint(42);\n}\n#endif\n";
		if (!Expect(Debug.LoadSource(Source, "raw-shift.vs", Options).bSuccess, "RawSource line insertion compiles")) return EXIT_FAILURE;
		const auto Bound = Debug.BindBreakpoint("raw-shift.vs", 3);
		if (!Expect(Bound.Id != 0 && Bound.Line == 3, "RawSource edits retain original active lines for breakpoint binding")) return EXIT_FAILURE;
		if (!Expect(Debug.BindBreakpoint("raw-shift.vs", 5).Id == 0, "RawSource edits do not activate preprocessor directives")) return EXIT_FAILURE;
	}
	// The tooling orchestration preserves the core's native metadata validation.
	for (const auto InvalidContainer : {false, true})
	{
		FNativeFunction Native;
		Native.Name = "invalid";
		if (InvalidContainer)
		{
			Native.ReturnType = ENativeType::Array;
		}

		else
		{
			Native.ParamTypes = {ENativeType::Int};
			Native.ParamFlags = {0, 0};
		}
		const std::vector<FNativeFunction> Natives{Native};
		FCompileOptions Options;
		VeroScript::Test::Configure(Options);
		Options.Natives = &Natives;
		const auto Result = Detail::CompileToolingSource("void main(){}", "invalid.vs", Options);
		if (!Expect(!Result.bSuccess &&
		            Result.Error.MessageId ==
		                (InvalidContainer ? "diag.invalid_native_container_metadata"
		                                  : "diag.invalid_native_parameter_flags"),
		        "invalid native metadata is rejected before preparation"))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string Source = "/// Card documentation.\n@Builder record Card {public:string text;};\n"
	                           "void main(){CardBuilder builder=Card::Create();Card card=builder.Text(\"Save\").Build();print(card.text);}";
	FCompileProcessingOptions Processing;
	Processing.Registry = Tests::BuilderRegistry();
	std::string Name = "CardBuilder";
	Processing.ReadDependency = [&](std::string_view, std::string& Text)
	{
		Text = Name;
		return true;
	};
	FWorkspaceOptions Options;
	Options.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	Options.CompileProcessing = Processing;
	VeroScriptWorkspace Workspace;
	Workspace.SetOptions(Options);
	Workspace.OpenDocument("processing.vs", Source);
	if (!Expect(Workspace.AnalyzeDocument("processing.vs").bSuccess, "analysis uses the configured source and declaration processors")) return EXIT_FAILURE;
	const auto Completion = Workspace.CompleteAtDocument("processing.vs", At(Source, ".Build", 3));
	if (!Expect(Completion.bSuccess &&
	            std::any_of(Completion.Items.begin(), Completion.Items.end(),
	                [](const auto& Item)
	{
		return Item.Label == "Build";
	}),
	        "generated members complete from generated types"))
	{
		return EXIT_FAILURE;
	}
	const auto Navigation = Workspace.NavigateDocument("processing.vs", At(Source, ".Build", 2));
	if (!Expect(Navigation.bSuccess && Navigation.Definitions.size() == 1 && Navigation.Definitions[0].FileLabel.rfind("veroscript-generated:", 0) == 0, "navigation returns the owned generated source")) return EXIT_FAILURE;
	const auto Snapshot = Workspace.SemanticTokensDocument("processing.vs");
	if (!Expect(Snapshot.bSuccess && Detail::IsSemanticQuerySnapshotCacheable(Snapshot.Snapshot) && Detail::IsSemanticQuerySnapshotCurrent(Snapshot.Snapshot), "general processor snapshots are valid and cacheable")) return EXIT_FAILURE;
	Workspace.OpenDocument("index.vs", "@Builder record Card {public:string text;};");
	Workspace.WorkspaceSymbols();
	Name = "ChangedBuilder";
	if (!Expect(!Detail::IsSemanticQuerySnapshotCurrent(Snapshot.Snapshot), "general processor dependency updates invalidate snapshots")) return EXIT_FAILURE;
	FWorkspaceSymbolsOptions SymbolsOptions;
	SymbolsOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	const auto Symbols = Workspace.WorkspaceSymbols(SymbolsOptions);
	if (!Expect(std::any_of(Symbols.Symbols.begin(), Symbols.Symbols.end(),
	                [](const auto& Symbol)
	{
		return Symbol.Name == "ChangedBuilder";
	}),
	        "workspace indexing uses fresh general processor dependencies"))
	{
		return EXIT_FAILURE;
	}
	Name = "CardBuilder";
	FDebugLoadOptions DebugOptions;
	DebugOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
	DebugOptions.CompileProcessing = Processing;
	VeroScriptDebugSession Debug;
	VeroScript::Test::Configure(Debug);
	if (!Expect(Debug.LoadSource(Source, "processing.vs", DebugOptions).bSuccess, "debug compilation uses the same processing pipeline")) return EXIT_FAILURE;
	const auto Run = Debug.Run();
	if (!Expect(Run.bSuccess && Run.Output == "Save\n", "generated builder executes in the debugger")) return EXIT_FAILURE;

	Options.CompileProcessing.Registry = Tests::BuilderRegistry(false);
	Workspace.SetOptions(Options);
	const auto Volatile = Workspace.SemanticTokensDocument("processing.vs");
	if (!Expect(Volatile.bSuccess && !Detail::IsSemanticQuerySnapshotCacheable(Volatile.Snapshot), "volatile general processors are never cached")) return EXIT_FAILURE;
	Options.CompileProcessing.Registry = Processing.Registry;
	Options.CompileProcessing.CommentRetention = ECommentRetention::NoComments;
	Workspace.SetOptions(Options);
	const auto Hidden = Workspace.HoverDocument("processing.vs", At(Source, "CardBuilder builder", 3));
	if (!Expect(
	        Hidden.bSuccess && Hidden.Contents.find("Generated builder documentation") == std::string::npos,
	        "a producer's comment retention is respected by hover"))
	{
		return EXIT_FAILURE;
	}

	for (const std::string Newline : {"\n", "\r\n"})
	{
		const auto Mapped = "/*tooling-active-shift*/" + Newline + "void main(){" + Newline +
		    "int local=42;" + Newline + "print(unknown);" + Newline + "}";
		FAnalyzeOptions AnalyzeOptions;
		AnalyzeOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		AnalyzeOptions.CompileProcessing = Processing;
		const auto Analysis = VeroScriptAnalyzer::AnalyzeSource(Mapped, "mapped.vs", AnalyzeOptions);
		if (!Expect(!Analysis.bSuccess && !Analysis.Diagnostics.empty() &&
		            Analysis.Diagnostics[0].Range.StartLine == 4,
		        "semantic diagnostics retain original coordinates after ActiveSource edits"))
		{
			return EXIT_FAILURE;
		}

		const auto Broken = "/*tooling-active-shift*/" + Newline + "@Builder record Card {public:int ;" +
		    Newline + "string text;};" + Newline;
		const auto Failed = VeroScriptAnalyzer::AnalyzeSource(Broken, "broken.vs", AnalyzeOptions);
		if (!Expect(!Failed.bSuccess && !Failed.Diagnostics.empty() &&
		            std::all_of(Failed.Diagnostics.begin(), Failed.Diagnostics.end(),
		                [](const auto& D)
		{
			return D.Range.StartLine >= 1 && D.Range.EndLine <= 4 &&
			    D.Range.EndLine >= D.Range.StartLine;
		}),
		        "load failures never publish positions beyond the original document"))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string MixedSource = "@Builder record Card {public:@Getter string text;};"
	                                "void main(){auto builder=Card::Create();print(builder.Text(\"Mixed\").Build().Read());}";
	auto LegacyRegistry = std::make_shared<FAnnotationProcessorRegistry>();
	FAnnotationProcessorDesc LegacyDesc{"@Getter", "tooling.test.legacy", "1", "",
	    AnnotationTargetMask(EAnnotationTarget::Field), 1, 0, []
	{
		return std::make_unique<FLegacyRead>();
	}};
	std::string RegistryError;
	if (!Expect(LegacyRegistry->Register(std::move(LegacyDesc), RegistryError), "legacy processor registration")) return EXIT_FAILURE;
	LegacyRegistry->Freeze();
	DebugOptions.AnnotationProcessing.Registry = LegacyRegistry;
	DebugOptions.AnnotationProcessing.ReadDependency = [](std::string_view, std::string&)
	{
		return false;
	};
	DebugOptions.CompileProcessing.ReadDependency = [](std::string_view Path, std::string& Text)
	{
		Text = Path.find("legacy-name") == std::string_view::npos ? "CardBuilder" : "Read";
		return true;
	};
	if (!Expect(Debug.LoadSource(MixedSource, "mixed.vs", DebugOptions).bSuccess, "general and legacy processors prepare the same program")) return EXIT_FAILURE;
	const auto MixedRun = Debug.Run();
	if (!Expect(MixedRun.bSuccess && MixedRun.Output == "Mixed\n", "mixed processing honors reader precedence and executes generated code")) return EXIT_FAILURE;

	std::string Reflection;
	unsigned Index = 0;
	for (const auto Owner : {"type", "field_info", "method_info", "function_info", "global_info", "module_info", "enum_member_info",
	         "source_node_info", "annotation_info", "annotation_argument_info", "comment_info", "annotation_definition_info"})
	{
		std::string Type = Owner;
		if (Type == "type" || Type == "field_info" || Type == "method_info" || Type == "enum_member_info")
		{
			Type += "<int>";
		}

		Reflection += "void Check" + std::to_string(Index++) + "(" + Type + " info){";
		for (const auto& [Method, Signature] : Detail::SourceMetadataBuiltinMethods(Owner))
		{
			const std::string_view Detail = Signature;
			Reflection += "info." + std::string(Method) + "(";
			if (Detail.find("int index") != std::string_view::npos)
			{
				Reflection += "0";
			}

			else if (Detail.find("string name") != std::string_view::npos)
			{
				Reflection += "\"\"";
			}

			Reflection += ");";
		}
		Reflection += "}";
	}
	Reflection += "void main(){}";
	const auto MetadataCompile = VeroScriptCompile::CompileSource(Reflection, "metadata.vs");
	if (!Expect(MetadataCompile.bSuccess, "all advertised source metadata methods compile against the core"))
	{
		std::cerr << MetadataCompile.Error.Message << '\n';
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
