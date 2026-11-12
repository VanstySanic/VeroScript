// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptToolingTestVm.h"
#include "VeroScriptProcessingToolingFixture.h"
#include "VeroScriptTooling.h"
#include "VeroScriptLspStdio.h"
#include "VeroScriptDapStdio.h"
#include "VeroScriptDapAdapter.h"
#include "VeroScriptWorkspace.h"
#include "VeroScriptCompile.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPath.h"
#include "../Internal/VeroScriptSymbolQuery.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace VeroScript;
using namespace VeroScript::Tooling;
namespace
{
	constexpr const char* LibraryPath = "veroscript-module:/dep/api.vs";
	constexpr const char* LibrarySource = "/* 中文😀 */ export int dep_value(){return 7;}\n";
	bool Native(const FNativeCallContext&, FValue& Result, std::string&)
	{
		Result = std::int32_t{42};
		return true;
	}
	class FResources final : public IResourceProvider
	{
	public:

		bool Read(const FResourceRequest&, const FResourceWork& Work, FResourceData& Data,
			std::string&) const override
		{
			if (Work.CheckCancelled)
			{
				Work.CheckCancelled();
			}

			std::lock_guard<std::mutex> Lock(Mutex);
			Data = {};
			Data.bExists = true;
			Data.ContentIdentity = Type;
			Data.Bytes.assign(Type.begin(), Type.end());
			return true;
		}

		void SetType(std::string Value)
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Type = std::move(Value);
		}

	private:
		mutable std::mutex Mutex;
		std::string Type = "int";
	};
	class FGenerator final : public ICompileProcessor
	{
	public:
		void Process(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output) override
		{
			FResourceData Data;
			if (!Context.ReadResource({"data", "types", "generation", "field-type"}, Data))
			{
				throw std::runtime_error("missing resource");
			}

			const std::string Type(Data.Bytes.begin(), Data.Bytes.end());
			Output.AddDeclaration({Context.ModuleId, "", "", Context.Snapshot},
				"struct DynamicValue{public:" + Type + " value;};");
		}
	};
	std::shared_ptr<const FModuleEnvironment> Environment(const std::filesystem::path& Root, bool AllowHidden = false)
	{
		auto Bindings = std::make_shared<FHostBindingRegistry>();
		std::string Error;
		for (const auto& Pair : {std::pair{"visible_native", "dep"}, {"hidden_native", "hidden"}})
		{
			FHostFunctionDesc Function;
			Function.Name = Pair.first;
			Function.ReturnType = ENativeType::Int;
			Function.Origin = {Pair.second, Pair.second == std::string("dep") ? LibraryPath : "veroscript-module:/hidden/api.vs", true};
			Bindings->SetRegistrationOwner({}, Function.Origin);
			if (!Bindings->RegisterNative(std::move(Function), Native, nullptr, Error))
			{
				throw std::runtime_error(Error);
			}
		}
		FHostTypeId Hidden;
		Bindings->SetRegistrationOwner({}, {"hidden", {}, true});
		if (!Bindings->RegisterType("HiddenType", EHostTypeKind::Struct, Hidden, Error))
		{
			throw std::runtime_error(Error);
		}

		FHostMemberDesc Member;
		Member.Name = "peek";
		Member.Kind = EHostMemberKind::Method;
		Member.ReturnType = ENativeType::Int;
		Member.Callback = Native;
		if (!Bindings->RegisterMember(Hidden, std::move(Member), Error))
		{
			throw std::runtime_error(Error);
		}

		FHostTypeId Private;
		if (!Bindings->RegisterType("PrivateType", EHostTypeKind::Struct, Private, Error))
		{
			throw std::runtime_error(Error);
		}

		FGenericHostTypeTemplateId PrivateGeneric;
		if (!Bindings->RegisterGenericHostTypeTemplateMetadata("PrivateGeneric", 1, PrivateGeneric, Error))
		{
			throw std::runtime_error(Error);
		}

		Bindings->SetRegistrationOwner({});
		FHostFunctionDesc Factory;
		Factory.Name = "make_hidden";
		Factory.ReturnType = ENativeType::HostObject;
		Factory.ReturnHostTypeName = "HiddenType";
		Factory.Origin = {"dep", LibraryPath, true};
		Bindings->SetRegistrationOwner({}, Factory.Origin);
		if (!Bindings->RegisterNative(std::move(Factory), Native, nullptr, Error))
		{
			throw std::runtime_error(Error);
		}

		Bindings->SetRegistrationOwner({});
		FModuleEnvironmentDesc Desc;
		Desc.EntryOwnerId = "app";
		Desc.Scopes = {{"app", AllowHidden ? std::vector<std::string>{"dep", "hidden"} : std::vector<std::string>{"dep"}},
			{"dep", {"leaf"}}, {"leaf", {}}, {"hidden", {}}};
		Desc.SourceRoots = {{PathUtil::ToUtf8(Root / "app"), "app"}};
		Desc.Modules = {{"dep", "dep.api", LibraryPath, "dep", "v1", LibrarySource, {}},
			{"strange", "dep.strange", "veroscript-module:/dep/encoded % 模块.vs", "dep", "v1", "export int named_value(){return 9;}\n", {}},
			{"facade", "dep.facade", "veroscript-module:/dep/facade.vs", "dep", "v1", "export import \"leaf\";", {}},
			{"leaf", "leaf.api", "veroscript-module:/leaf/api.vs", "leaf", "v1", "export int leaf_value(){return 13;}\n", {}},
			{"physical", "dep.physical", PathUtil::ToUtf8(Root / "dep/physical.vs"), "dep", "v1", "export int physical_value(){return 21;}\n", {}},
			{"forbidden", "hidden.api", "veroscript-module:/hidden/api.vs", "hidden", "v1", "export int secret(){return 9;}\n", {}}};
		Desc.Bindings = std::move(Bindings);
		Desc.AvailableDependencies = {{"dep", "1", "test", "build-1", "content-1"}, {"hidden", "1", "test", "build-1", "content-1"}};
		Desc.RequiredDependencies = {Desc.AvailableDependencies.front()};
		auto Result = FModuleEnvironment::Create(std::move(Desc), Error);
		if (!Result)
		{
			throw std::runtime_error(Error);
		}

		return Result;
	}
	void Check(bool Value, const char* Name)
	{
		if (!Value)
		{
			throw std::runtime_error(Name);
		}
	}
	FTextPosition Position(const std::string& Source, const std::string& Token, bool End = true)
	{
		auto Offset = Source.find(Token);
		Check(Offset != std::string::npos, "test token is present");
		if (End)
		{
			Offset += Token.size();
		}

		FTextPosition Result;
		for (std::size_t I = 0; I < Offset; ++I)
		{
			if (Source[I] == '\n')
			{
				++Result.Line;
				Result.Column = 1;
			}
			else
			{
				++Result.Column;
			}
		}

		return Result;
	}
	bool Has(const FCompletionResult& Result, const std::string& Name)
	{
		return std::any_of(Result.Items.begin(), Result.Items.end(),
			[&](const auto& Item)
			{
				return Item.Label == Name;
			});
	}
	void RunModuleEnvironmentTests(const std::filesystem::path& Root)
	{
		const auto Main = PathUtil::ToUtf8(Root / "app/main.vs");
		const auto Env = Environment(Root);
		const std::string Source = "import \"dep\"; void main(){print(visible_native());print(dep_value());}";
		FCompileOptions Core;
		VeroScript::Test::Configure(Core);
		Core.ModuleEnvironment = Env;
		Check(VeroScriptCompile::CompileSource(Source, Main, Core).bSuccess, "core fixture compiles");
		FAnalyzeOptions Analyze;
		Analyze.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Analyze.ModuleEnvironment = Env;
		const auto Analysis = VeroScriptAnalyzer::AnalyzeSource(Source, Main, Analyze);
		for (const auto& D : Analysis.Diagnostics)
		{
			std::cerr << D.Message << '\n';
		}

		Check(Analysis.bSuccess, "analysis shares core module resolution and native bindings");
		Check(!VeroScriptAnalyzer::AnalyzeSource("import \"forbidden\";", Main, Analyze).bSuccess,
			"analysis rejects dependencies outside the environment");
		FCompletionOptions Completion;
		Completion.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Completion.ModuleEnvironment = Env;
		const auto Complete = [&](const std::string& Text, const std::string& Token)
		{
			return VeroScriptComplete::CompleteAtSource(Text, Position(Text, Token), Main, Completion);
		};
		Check(Has(Complete("import \"de", "de"), "dep"), "import completion includes mapped memory modules");
		Check(!Has(Complete("import \"for", "for"), "forbidden"), "import completion rejects an unauthorized mapped module");
		Check(Has(Complete("import \"dep\"; void main(){vis}", "vis"), "visible_native"), "authorized native completion");
		Check(!Has(Complete("void main(){vis}", "vis"), "visible_native"), "native module declarations require an import");
		Check(!Has(Complete("import \"dep\"; void main(){hidden}", "hidden"), "hidden_native"), "no unrelated native completion");
		Check(Has(Complete("import \"dep\"; void main(){Hidden}", "Hidden"), "HiddenType"), "public signatures expose their native types");
		Check(!Has(Complete("import \"dep\"; void main(){Private}", "Private"), "PrivateType"), "unexposed transitive types stay hidden");
		Check(!Has(Complete("import \"dep\"; void main(){Private}", "Private"), "PrivateGeneric"), "unexposed generic templates stay hidden");
		for (const auto* Broken : {"int broken[; void main(){hidden", "class Broken : { void main(){hidden"})
		{
			Check(!Has(Complete(Broken, "hidden"), "hidden_native"),
				"unrecoverable completion cannot bypass native visibility");
		}

		Check(Has(Complete("import \"dep\"; void main(){auto item=make_hidden();item.}", "item."), "peek"),
			"members on a native type exposed by a public signature");
		FHoverOptions Hover;
		Hover.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Hover.ModuleEnvironment = Env;
		const auto Visible = VeroScriptHover::HoverAtSource(Source, Position(Source, "visible_native", false), Main, Hover);
		Check(Visible.bSuccess && Visible.Contents.find("visible_native") != std::string::npos, "native hover with environment bindings");
		const std::string Hidden = "void main(){hidden_native();}";
		const auto HiddenHover = VeroScriptHover::HoverAtSource(Hidden, Position(Hidden, "hidden_native", false), Main, Hover);
		Check(HiddenHover.Definitions.empty() && HiddenHover.Contents.empty(), "hover fallback cannot bypass native visibility");
		FSignatureHelpOptions Help;
		Help.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Help.ModuleEnvironment = Env;
		const std::string Call = "import \"dep\"; void main(){visible_native(}";
		Check(!VeroScriptCallHelp::HelpAtSource(Call, Position(Call, "visible_native("), Main, Help).Signatures.empty(), "native signature help");
		const std::string HiddenCall = "void main(){hidden_native(}";
		Check(VeroScriptCallHelp::HelpAtSource(HiddenCall, Position(HiddenCall, "hidden_native("), Main, Help).Signatures.empty(),
			"signature fallback cannot bypass visibility");
		FNavigationOptions Navigation;
		Navigation.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Navigation.ModuleEnvironment = Env;
		const auto Definition = VeroScriptNavigation::QueryAtSource(Source, Position(Source, "dep_value", false), Main, Navigation);
		Check(!Definition.Definitions.empty() && Definition.Definitions.front().FileLabel == LibraryPath, "definition in an in-memory module");
		const auto Import = VeroScriptNavigation::QueryAtSource(Source, Position(Source, "dep", false), Main, Navigation);
		Check(!Import.Definitions.empty() && Import.Definitions.front().FileLabel == LibraryPath, "mapped import navigation");
		VeroScriptWorkspace Workspace;
		FWorkspaceOptions WorkspaceOptions;
		WorkspaceOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		WorkspaceOptions.ModuleEnvironment = Env;
		Workspace.SetOptions(WorkspaceOptions);
		Workspace.OpenDocument(Main, Source);
		Check(Workspace.AnalyzeDocument(Main).bSuccess && Workspace.SemanticTokensDocument(Main).bSuccess, "workspace analysis and semantic tokens");
		Check(Workspace.OutlineDocument(Main).bSuccess, "outline retains the environment");
		const auto References = Workspace.FindReferencesDocument(Main, Position(Source, "dep_value", false));
		Check(References.bSuccess && References.References.size() == 2, "references include the memory declaration and caller");
		Check(!Workspace.CanRenameDocument(Main, Position(Source, "dep_value", false)).bCanRename,
			"immutable host module declarations cannot be renamed from a caller");
		FWorkspaceSymbolsOptions Symbols;
		Symbols.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		Symbols.bIncludeHostBindings = true;
		const auto Index = Workspace.WorkspaceSymbols(Symbols);
		Check(Index.bSuccess &&
				std::any_of(Index.Symbols.begin(), Index.Symbols.end(),
					[](const auto& S)
					{
						return S.Name == "visible_native";
					}) &&
				std::none_of(Index.Symbols.begin(), Index.Symbols.end(),
					[](const auto& S)
					{
						return S.Name == "hidden_native";
					}),
			"workspace index preserves native visibility");
		const std::string LocalSource = "import \"dep\"; void main(){int local=dep_value();print(local);}";
		Workspace.UpdateDocument(Main, LocalSource);
		Check(Workspace.HighlightDocument(Main, Position(LocalSource, "local", false)).Highlights.size() == 2,
			"local highlights retain module bindings");
		const auto Rename = Workspace.RenameDocument(Main, Position(LocalSource, "local", false), "renamed");
		Check(Rename.bSuccess && Rename.bCanRename && Rename.Edits.size() == 2,
			"local rename stays editable under a module environment");
		{
			// Use canonical labels so the environment and buffer have exactly the
			// same path identity (Windows short temp paths can otherwise hide this).
			const auto PhysicalPath = PathUtil::ToUtf8(std::filesystem::weakly_canonical(Root) / "dep/physical.vs");
			std::string Caller = "import \"physical\";void main(){print(physical_value());}";
			std::string Buffer = "\n\n/// Current buffer documentation\nexport int physical_value(){return 22;}\n";
			VeroScriptWorkspace Buffers;
			Buffers.SetOptions(WorkspaceOptions);
			Buffers.OpenDocument(Main, Caller);
			Buffers.OpenDocument(PhysicalPath, Buffer);
			const auto Refs = Buffers.FindReferencesDocument(Main, Position(Caller, "physical_value", false));
			Check(Refs.bSuccess && Refs.References.size() == 2 &&
					std::any_of(Refs.References.begin(), Refs.References.end(),
						[](const auto& R)
						{
							return R.Range.StartLine == 4;
						}),
				"references use the current buffer instead of the environment source");
			const auto Edits =
				Buffers.RenameDocument(Main, Position(Caller, "physical_value", false), "renamed_value");
			Check(Edits.bSuccess && Edits.bCanRename && Edits.Edits.size() == 2, "rename includes the overridden declaration and caller");
			for (const auto& Edit : Edits.Edits)
			{
				auto& Text = PathUtil::Equal(Edit.FileLabel, Main) ? Caller : Buffer;
				std::size_t Offset = 0;
				for (std::uint32_t Line = 1; Line < Edit.Range.StartLine; ++Line)
				{
					Offset = Text.find('\n', Offset) + 1;
				}

				Offset += Edit.Range.StartColumn - 1;
				Check(Text.substr(Offset, Edit.Range.EndColumn - Edit.Range.StartColumn) == "physical_value", "rename range addresses the actual buffer token");
				Text.replace(Offset, Edit.Range.EndColumn - Edit.Range.StartColumn, Edit.NewText);
			}
			Core.SourceOverlays = {{PhysicalPath, Buffer}};
			Check(VeroScriptCompile::CompileSource(Caller, Main, Core).bSuccess, "applying every rename edit preserves a compilable program");
			Core.SourceOverlays.clear();
			FRenameOptions DuplicateOptions;
			DuplicateOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
			const auto DuplicatePath = PathUtil::ToUtf8(Root / "app/duplicates.vs");
			DuplicateOptions.SourceOverlays = {{DuplicatePath, "export int name(){return 1;}"},
				{DuplicatePath, "\n\nexport int name(){return 2;}"}};
			const std::string DuplicateCaller = "import \"duplicates.vs\";void main(){print(name());}";
			const auto DuplicateRename = VeroScriptRename::PrepareAtSource(DuplicateCaller,
				Position(DuplicateCaller, "name", false), "updated", Main, DuplicateOptions);
			Check(DuplicateRename.bSuccess && DuplicateRename.bCanRename && DuplicateRename.Edits.size() == 2,
				"duplicate explicit overlays also follow last-wins precedence without an environment");
		}
		Workspace.OpenDocument(Main, "import \"forbidden\"; void main(){print(hidden_native());}");
		Check(!Workspace.AnalyzeDocument(Main).bSuccess, "old dependency graph denies a hidden import");
		WorkspaceOptions.ModuleEnvironment = Environment(Root, true);
		Workspace.SetOptions(WorkspaceOptions);
		Check(Workspace.AnalyzeDocument(Main).bSuccess, "replacing the environment refreshes the dependency graph");
		Workspace.SetOptions({});
		Check(!Workspace.AnalyzeDocument(Main).bSuccess, "clearing the environment does not retain mapped modules");
		FHostBindingRegistry Conflict;
		Analyze.Bindings = &Conflict;
		Check(!VeroScriptAnalyzer::AnalyzeSource(Source, Main, Analyze).bSuccess, "conflicting live catalogs fail as diagnostics");
		VeroScriptDebugSession Debug;
		VeroScript::Test::Configure(Debug);
		VeroScriptDapAdapter Adapter;
		Check(Adapter.Initialize().bSuccess, "initialize adapter source test");
		FDapLaunchArgs Launch;
		Launch.Source = Source;
		Launch.FileLabel = Main;
		Launch.ModuleEnvironment = Env;
		Check(Adapter.Launch(Launch).bSuccess, "adapter loads the environment");
		std::string UnstoppedSource = "old source";
		Check(!Adapter.TryGetSource(LibraryPath, UnstoppedSource) && UnstoppedSource.empty(),
			"adapter source capture follows the stopped-state access contract");
		FDebugLoadOptions DebugOptions;
		DebugOptions.ScriptCompileExtensions.VmBackendFactory = VeroScript::Test::VmFactory();
		DebugOptions.ModuleEnvironment = Env;
		Check(Debug.LoadSource(Source, Main, DebugOptions).bSuccess, "debug load uses the environment");
		std::string LoadedSource;
		Check(Debug.TryGetSource(Main, LoadedSource) && LoadedSource == Source, "debug reads the actual inline main source");
		Check(Debug.TryGetSource(LibraryPath, LoadedSource) && LoadedSource == LibrarySource, "debug reads frozen memory source");
		Check(!Debug.TryGetSource("veroscript-module:/hidden/api.vs", LoadedSource), "debug does not expose an unloaded memory module");
		const auto Run = Debug.Run();
		for (const auto& D : Run.Diagnostics)
		{
			std::cerr << D.Message << '\n';
		}

		Check(Run.bSuccess && Run.Output == "42\n7\n",
			"debug VM retains the environment and exact external dependencies");
		const std::string Override = "/* buffer 😀 */ export int dep_value(){return 8;}\n";
		DebugOptions.SourceOverlays = {{LibraryPath, Override}};
		Check(Debug.LoadSource(Source, Main, DebugOptions).bSuccess, "debug reload accepts a precise memory source overlay");
		Check(Debug.TryGetSource(LibraryPath, LoadedSource) && LoadedSource == Override, "debug source matches the compiled overlay");
		Check(Debug.Run().Output == "42\n8\n", "debug executes the compiled memory overlay");
		const auto PhysicalPath = PathUtil::ToUtf8(Root / "dep/physical.vs");
		const std::string PhysicalOverride = "export int physical_value(){return 22;}\n";
		DebugOptions.SourceOverlays = {{PhysicalPath, PhysicalOverride}};
		Check(Debug.LoadSource("import \"physical\";void main(){print(physical_value());}", Main, DebugOptions).bSuccess,
			"debug accepts environment memory sources with physical labels");
		Check(Debug.TryGetSource(PhysicalPath, LoadedSource) && LoadedSource == PhysicalOverride,
			"physical memory labels also return the actual compiled overlay");
		Check(Debug.Run().Output == "22\n", "physical-label overlay executes correctly");
		DebugOptions.Bindings = &Conflict;
		Check(!Debug.LoadSource(Source, Main, DebugOptions).bSuccess && !Debug.IsLoaded(), "conflicting reload clears obsolete debug state");
		const std::string I18n = "void main(){std::i18n::}";
		const auto I18nCompletion = VeroScriptComplete::CompleteAtSource(I18n, Position(I18n, "std::i18n::"), Main);
		Check(Has(I18nCompletion, "text") && Has(I18nCompletion, "normalize_language_tag"), "new standard native metadata is available without a host catalog");
		const std::string BrokenI18n = "int broken[;void main(){std::i18n::";
		const auto CheckI18nRecovery = [&](const std::string& Text, const std::string& Prefix,
		    const FCompletionOptions& Options, const char* Name)
		{
			const auto Caret = Position(Text, "std::i18n::" + Prefix);
			const auto Recovered = VeroScriptComplete::CompleteAtSource(Text, Caret, Main, Options);
			Check(Recovered.bSuccess && Has(Recovered, "text") &&
			        (Prefix.empty() ? Has(Recovered, "normalize_language_tag") : !Has(Recovered, "normalize_language_tag")) &&
			        Recovered.Prefix == Prefix && Recovered.bHasReplaceRange == !Prefix.empty() &&
			        (Prefix.empty() || (Recovered.ReplaceRange.StartLine == Caret.Line &&
			            Recovered.ReplaceRange.EndLine == Caret.Line &&
			            Recovered.ReplaceRange.EndColumn == Caret.Column &&
			            Recovered.ReplaceRange.StartColumn == Caret.Column - Prefix.size())),
			    Name);
		};
		CheckI18nRecovery(BrokenI18n, {}, {}, "standard namespace recovery includes native types and functions");
		for (const std::string Newline : {"\n", "\r\n"})
		{
			CheckI18nRecovery(BrokenI18n + Newline, {}, {}, "standard namespace recovery preserves the caret before a trailing newline");
		}
		CheckI18nRecovery("#define PAD 123456789\nint broken[;void main(){int pad=PAD;std::i18n::",
		    {}, {}, "standard namespace recovery maps a longer preceding macro expansion");
		CheckI18nRecovery("#define PADDED_LITERAL_NAME 1\nint broken[;void main(){int pad=PADDED_LITERAL_NAME;std::i18n::",
		    {}, {}, "standard namespace recovery maps a shorter preceding macro expansion");
		FCompletionOptions MappedRecovery;
		MappedRecovery.CompileProcessing.Registry = VeroScript::Tooling::Tests::BuilderRegistry();
		for (const std::string Newline : {"\n", "\r\n"})
		{
			const std::string Mapped = "/*tooling-raw-shift*/" + Newline + BrokenI18n;
			CheckI18nRecovery(Mapped, {}, MappedRecovery,
			    "standard namespace recovery keeps the caret after projected source edits");
			CheckI18nRecovery(Mapped + "te", "te", MappedRecovery,
			    "standard namespace recovery keeps prefix replacement in original source coordinates");
		}
		VeroScriptWorkspace I18nWorkspace;
		const std::string ReadOnly = "void main(){auto context=std::i18n::context(std::i18n::configuration());auto v=context.version();print(v.context_id);}";
		I18nWorkspace.OpenDocument(Main, ReadOnly);
		const auto I18nTokens = I18nWorkspace.SemanticTokensDocument(Main);
		const auto IdPosition = Position(ReadOnly, "context_id", false);
		Check(I18nTokens.bSuccess &&
				std::any_of(I18nTokens.Tokens.begin(), I18nTokens.Tokens.end(),
					[&](const auto& T)
					{
						return T.Range.StartLine == IdPosition.Line &&
							T.Range.StartColumn == IdPosition.Column && T.Kind == EOutlineSymbolKind::Field &&
							(T.Modifiers & SemanticTokenModifiers::Readonly);
					}),
			"getter-only standard properties keep property and readonly semantic classification");
		const auto Resources = std::make_shared<FResources>();
		FModuleEnvironmentDesc ResourceDesc;
		ResourceDesc.EntryOwnerId = "app";
		ResourceDesc.Scopes = {{"app", {"data"}}, {"data", {}}};
		ResourceDesc.SourceRoots = {{PathUtil::ToUtf8(Root / "app"), "app"}};
		FResourceProviderBinding Provider;
		Provider.Provider = Resources;
		Provider.Id = "data";
		Provider.OwnerId = "data";
		Provider.ContractIdentity = "test-resource-v1";
		Provider.ModuleIds = {"types"};
		ResourceDesc.Resources.push_back(std::move(Provider));
		auto Registry = std::make_shared<FCompileProcessorRegistry>();
		FCompileProcessorDesc Processor;
		Processor.ProcessorId = "tooling.module.resource";
		Processor.Version = "1";
		Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::Types);
		Processor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
		FCompileSubscription Subscription;
		Subscription.Event = ECompileProcessingEvent::Module;
		Processor.Subscriptions.push_back(std::move(Subscription));
		Processor.Factory = []
		{
			return std::make_unique<FGenerator>();
		};
		std::string Error;
		Check(Registry->Register(std::move(Processor), Error), "register environment resource processor");
		Registry->Freeze();
		ResourceDesc.CompileProcessing.Registry = Registry;
		WorkspaceOptions = {};
		WorkspaceOptions.ModuleEnvironment = FModuleEnvironment::Create(std::move(ResourceDesc), Error);
		Check(WorkspaceOptions.ModuleEnvironment != nullptr, "create resource environment");
		Workspace.SetOptions(WorkspaceOptions);
		const std::string ResourceSource = "void main(){auto item=DynamicValue();print(item.value);}";
		Workspace.UpdateDocument(Main, ResourceSource);
		const auto First = Workspace.SemanticTokensDocument(Main);
		for (const auto& D : First.Diagnostics)
		{
			std::cerr << "resource: " << D.Message << '\n';
		}

		Check(First.bSuccess && First.Snapshot && Detail::IsSemanticQuerySnapshotCurrent(First.Snapshot),
			"resource snapshot starts current");
		const auto Before = Workspace.HoverDocument(Main, Position(ResourceSource, "value", false));
		Check(!Before.Definitions.empty() && Before.Definitions.front().Detail == "int", "initial resource-generated field type");
		Resources->SetType("string");
		Check(!Detail::IsSemanticQuerySnapshotCurrent(First.Snapshot), "resource update invalidates semantic snapshot without a text edit");
		const auto After = Workspace.HoverDocument(Main, Position(ResourceSource, "value", false));
		Check(!After.Definitions.empty() && After.Definitions.front().Detail == "string", "query observes changed environment resource");
		Check(Workspace.SemanticTokensDocument(Main).bSuccess, "updated resource reanalyzes successfully");
	}
}
int main(int argc, char** argv)
try
{
	if (argc > 1)
	{
		#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
		#endif
		auto Env = Environment(std::filesystem::current_path());
		if (std::string(argv[1]) == "--lsp")
		{
			FLspStdioRunOptions Options;
			Options.ModuleEnvironment = std::move(Env);
			return VeroScript::Test::FLspStdio::Run(std::cin, std::cout, Options);
		}
		if (std::string(argv[1]) == "--dap")
		{
			FDapStdioRunOptions Options;
			Options.ModuleEnvironment = std::move(Env);
			return VeroScript::Test::FDapStdio::Run(std::cin, std::cout, Options);
		}
		return 2;
	}
	const auto Root = std::filesystem::temp_directory_path() / ("vero-module-tooling-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(Root / "app");
	RunModuleEnvironmentTests(Root);
	std::error_code Error;
	std::filesystem::remove(Root / "app", Error);
	std::filesystem::remove(Root, Error);
	return 0;
}
catch (const std::exception& Error)
{
	std::cerr << Error.what() << '\n';
	return 1;
}
