// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScript.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptBytecodeLinker.h"
#include "VeroScriptStandardLibrary.h"
#include "../Processing/VeroScriptProcessingSession.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

using namespace VeroScript;
namespace
{
	int Failures = 0;
	void Expect(bool B, const std::string& N)
	{
		if (!B)
		{
			++Failures;
			std::cerr << "FAIL: " << N << '\n';
		}
	}
	struct FResources : IResourceProvider
	{
		bool Read(const FResourceRequest&, const FResourceWork& W, FResourceData& D, std::string& E) const override
		{
			if (W.CheckCancelled)
			{
				W.CheckCancelled();
			}
			std::lock_guard<std::mutex> Lock(Mutex);
			D = {};
			if (Fail)
			{
				E = "resource broken";
				return false;
			}
			D.bExists = Exists;
			if (Exists)
			{
				if (W.MaxBytes && Value.size() > W.MaxBytes)
				{
					E = "resource byte limit";
					return false;
				}
				D.Bytes.assign(Value.begin(), Value.end());
				D.ContentIdentity = Value;
			}
			return true;
		}

		mutable std::mutex Mutex;

		std::string Value = "61";

		bool Exists = true, Fail = false;
	};
	struct FGenerator : ICompileProcessor
	{
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			FResourceData D;
			const bool Found = C.ReadResource({"data", "data.module", "templates", "value.txt"}, D);
			const std::string Value = Found ? std::string(D.Bytes.begin(), D.Bytes.end()) : "7";
			O.AddDeclaration({C.ModuleId, "", "", C.Snapshot}, "int generated_resource() { return " + Value + "; }");
		}
	};
	struct FScopedGenerator : ICompileProcessor
	{
		std::map<std::string, int>* Calls = nullptr;
		bool Generate = false;
		void Process(const FCompileProcessingContext& C, FCompileProcessingOutput& O) override
		{
			++(*Calls)[C.ModuleId];
			if (Generate)
			{
				O.AddDeclaration({C.ModuleId, "", "", C.Snapshot},
					"class ScopedGenerated{public:static int Value(){return scoped_native();}};");
			}
		}
	};
	std::shared_ptr<const FModuleEnvironment> Make(FModuleEnvironmentDesc D)
	{
		std::string E;
		auto R = FModuleEnvironment::Create(std::move(D), E);
		Expect(R != nullptr, "create environment: " + E);
		return R;
	}
	bool Run(FCompileResult& C, std::shared_ptr<const FModuleEnvironment> E, int Expected)
	{
		if (!C.bSuccess)
		{
			Expect(false, "compile: " + C.Error.Message);
			return false;
		}
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		std::string Error;
		const bool Ok = Vm.SetModuleEnvironment(std::move(E), Error) && Vm.Execute(*C.Module, ERunMode::Script, {}) && Vm.GetProcessExitCode() == Expected;
		Expect(Ok, "run: " + Vm.GetError().Message + Error);
		return Ok;
	}
	void TestNativeSnapshotRetirement()
	{
		FHostBindingRegistry Bindings;
		std::string Error;
		Expect(Bindings.RegisterNative("snapshot_value", ENativeType::Int, {},
			[](const FNativeCallContext&, FValue& Value, std::string&)
			{
				Value = std::int32_t{17};
				return true;
			}, nullptr, Error), "register snapshot retirement native");
		auto Options = VeroScript::Test::MakeCompileOptions();
		Options.Bindings = &Bindings;
		auto Compiled = VeroScriptCompile::CompileSource(
			"int work(){return snapshot_value();}", "snapshot-retirement.vs", Options);
		Expect(Compiled.bSuccess, "compile snapshot retirement entry");
		if (!Compiled.bSuccess) return;
		FModuleEnvironmentDesc Description;
		Description.EntryOwnerId = "snapshot";
		Description.Scopes = {{"snapshot", {}}};
		const auto First = Make(Description), Second = Make(Description);
		for (const bool ChangeBindingsMode : {false, true})
		{
			std::ostringstream Output;
			VeroScriptVm Vm(Output);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(EExecutionBackend::Vm);
			Expect(Vm.SetModuleEnvironment(First, Error, false), "configure snapshot retirement environment");
			std::weak_ptr<void> CallbackLease;
			std::weak_ptr<const void> ProviderLease;
			{
				auto Natives = Bindings.GetNativeFunctions();
				Natives.front().CallbackOwner = std::make_shared<int>(1);
				Natives.front().ProviderIdentity = std::make_shared<int>(2);
				CallbackLease = Natives.front().CallbackOwner;
				ProviderLease = Natives.front().ProviderIdentity;
				// A complete catalog is used directly, so the historical merge
				// buffer does not independently retain either external lease.
				const auto& Standard = StandardLibrary::GetBindingCatalog().GetNativeFunctions();
				Natives.insert(Natives.end(), Standard.begin(), Standard.end());
				FValue Value;
				Expect(Vm.ExecuteFunction(*Compiled.Module, 0, {}, &Natives, &Value)
					&& std::get<std::int32_t>(Value) == 17, "execute snapshotted native catalog");
			}
			Expect(!CallbackLease.expired() && !ProviderLease.expired(), "idle native cache retains active catalog leases");
			Expect(Vm.SetModuleEnvironment(First, Error, false)
				&& !CallbackLease.expired() && !ProviderLease.expired(), "unchanged environment preserves native cache");
			Expect(Vm.SetModuleEnvironment(ChangeBindingsMode ? First : Second, Error, ChangeBindingsMode)
				&& CallbackLease.expired() && ProviderLease.expired(), "changed environment or binding mode retires native snapshots immediately");
		}
	}
	void TestProcessorScopes()
	{
		const std::string Main = "veroscript-module:/project/main.vs";
		const std::string Foreign = "veroscript-module:/foreign/api.vs";
		const std::string Provider = "veroscript-module:/provider/api.vs";
		std::string Error;
		auto Bindings = std::make_shared<FHostBindingRegistry>();
		Bindings->SetRegistrationOwner({}, {"provider", "", true});
		Expect(Bindings->RegisterNative("scoped_native", ENativeType::Int, {},
			[](const FNativeCallContext&, FValue& R, std::string&)
			{
				R = std::int32_t{42};
				return true;
			}, nullptr, Error), "register processor dependency");
		Bindings->SetRegistrationOwner({});
		for (const bool Related : {false, true})
		{
			FModuleEnvironmentDesc D;
			D.EntryOwnerId = "project";
			D.Scopes = {{"project", {"provider", "foreign"}}, {"provider", {}},
				{"foreign", Related ? std::vector<std::string>{"provider"} : std::vector<std::string>{}}};
			D.Modules = {{"foreign.api", "foreign.api", Foreign, "foreign", "f1", "export int foreign_value(){return 7;}"},
				{"provider.api", "provider.api", Provider, "provider", "p1", "export int provider_value(){return 8;}"},
				{"project.main", "project.main", Main, "project", "m1", "int main(){return 0;}"}};
			D.Bindings = Bindings;
			D.AvailableDependencies = {{"provider", "0.1.1", "test-abi", "test-build", "test-content"}};
			std::map<std::string, int> NativeCalls, GlobalCalls;
			auto Registry = std::make_shared<FCompileProcessorRegistry>();
			for (const bool Global : {false, true})
			{
				FCompileProcessorDesc P;
				P.ProcessorId = Global ? "global.observer" : "scoped.generator";
				P.Version = "0.1.1";
				P.Phases = CompilePhaseMask(ECompileProcessingPhase::Types);
				P.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
				P.Subscriptions = {{ECompileProcessingEvent::Module}};
				if (!Global)
				{
					P.Origin.ProviderId = "provider";
				}
				P.Factory = [&, Global]
				{
					auto Instance = std::make_unique<FScopedGenerator>();
					Instance->Calls = Global ? &GlobalCalls : &NativeCalls;
					Instance->Generate = !Global;
					return Instance;
				};
				Expect(Registry->Register(std::move(P), Error), "register scoped processor: " + Error);
			}
			Registry->Freeze();
			D.CompileProcessing.Registry = Registry;
			auto Environment = Make(D);
			auto O = VeroScript::Test::MakeCompileOptions();
			O.ModuleEnvironment = Environment;
			auto C = VeroScriptCompile::CompileSource(
				"import \"foreign.api\";import \"provider.api\";int main(){return ScopedGenerated::Value();}", Main, O);
			Run(C, Environment, 42);
			Expect(NativeCalls[Main] == 1 && NativeCalls[Provider] == 1 && NativeCalls[Foreign] == (Related ? 1 : 0),
				"only visible source domains receive provider-scoped generation");
			Expect(GlobalCalls[Main] == 1 && GlobalCalls[Provider] == 1 && GlobalCalls[Foreign] == 1,
				"host global processors retain compilation-wide scope");
			D.Modules.front().Source = "export int foreign_value(){return scoped_native();}";
			O.ModuleEnvironment = Make(D);
			C = VeroScriptCompile::CompileSource("import \"foreign.api\";int main(){return foreign_value();}", Main, O);
			Expect(C.bSuccess == Related, "skipped processing does not relax native declaration visibility");
			O.ModuleEnvironment.reset();
			FHostBindingRegistry HostBindings;
			Expect(HostBindings.RegisterNative("scoped_native", ENativeType::Int, {},
				[](const FNativeCallContext&, FValue& Value, std::string&)
				{
					Value = std::int32_t{42};
					return true;
				}, nullptr, Error), "register no-environment host binding");
			O.Bindings = &HostBindings;
			O.CompileProcessing.Registry = Registry;
			C = VeroScriptCompile::CompileSource("int main(){return ScopedGenerated::Value();}", Main, O);
			Expect(C.bSuccess, "host processors without a module environment retain existing behavior: " + C.Error.Message);
			FCompileProcessingSession Session(D.CompileProcessing);
			Session.Options.ModuleEnvironment = Environment;
			FProgram Program;
			Program.ModuleSourceOwners[Foreign] = "provider";
			Session.Program = &Program;
			FSourceDocument Document;
			Document.Id = Document.ModuleId = Foreign;
			NativeCalls.clear();
			Session.Dispatch(ECompileProcessingPhase::Types, {}, &Document);
			Expect(NativeCalls[Foreign] == 1, "captured source owners override consumer physical-path ownership");
			FPackageSourceModule Packaged;
			Packaged.bFromPackage = true;
			Packaged.ModulePath = Foreign;
			Packaged.SourceOwnerId = "foreign";
			Program.PackageSourceModules.push_back(std::move(Packaged));
			NativeCalls.clear();
			Session.Dispatch(ECompileProcessingPhase::Types, {}, &Document);
			Expect(NativeCalls[Foreign] == (Related ? 1 : 0), "packaged producer owners override captured consumer ownership");
		}
	}
}
int main()
{
	TestNativeSnapshotRetirement();
	TestProcessorScopes();
	{
		FAnnotationDefinition Definition;
		Definition.Id="environment.name-policy";
		Definition.Name="@GuiWidget";
		FModuleEnvironmentDesc Exact;
		Exact.EntryOwnerId="project";
		Exact.Scopes={{"project",{}}};
		Exact.CompileProcessing.AnnotationDefinitions={Definition};
		auto Folded=Exact;
		Folded.CompileProcessing.AnnotationDefinitions.front().CaseSensitive=false;
		const auto A=Make(Exact),B=Make(Folded);
		Expect(A && B && A->GetCompileFingerprint()!=B->GetCompileFingerprint(),"annotation policy changes module environment identity");
	}
	const auto Root = std::filesystem::temp_directory_path() /
		("veroscript-module-environment-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	for (const auto* D : {"project", "a", "b", "outside"})
	{
		std::filesystem::create_directories(Root / D);
	}

	const auto Path = [&](const std::string& P)
	{
		return PathUtil::ToUtf8(Root / PathUtil::FromUtf8(P));
	};
	FModuleEnvironmentDesc D;
	D.EntryOwnerId = "project";
	D.Scopes = {{"project", {"a"}}, {"a", {"b"}}, {"b", {}}};
	D.SourceRoots = {{Path("project"), "project"}, {Path("a"), "a"}, {Path("b"), "b"}};
	D.Modules = {{"lib/a.vs", "a.api", Path("a/api.vs"), "a", "a1", std::string("import \"lib/b.vs\"; export int answer(){ return b(); }")},
		{"lib/b.vs", "b.api", Path("b/api.vs"), "b", "b1", std::string("#define_export ANSWER 42\nexport int b(){return ANSWER;}")}};
	auto Environment = Make(D);
	FCompileOptions O = VeroScript::Test::MakeCompileOptions();
	O.ModuleEnvironment = Environment;
	const auto Main = Path("project/main.vs");
	#if defined(_WIN32)
	{
		const auto Extended = [](const std::string& Value)
		{
			return Value.rfind("//", 0) == 0 ? "//?/UNC/" + Value.substr(2) : "//?/" + Value;
		};
		auto AliasedDescription = D;
		for (auto& R : AliasedDescription.SourceRoots)
		{
			R.Path = Extended(R.Path);
		}
		for (auto& M : AliasedDescription.Modules)
		{
			M.Path = Extended(M.Path);
		}
		auto AliasedEnvironment = Make(AliasedDescription);
		std::string AliasError;
		Expect(AliasedEnvironment && AliasedEnvironment->GetSourceOwner(Main) == "project" &&
			AliasedEnvironment->GetSourceOwner(Extended(Main)) == "project" &&
			AliasedEnvironment->FindSource(Path("a/api.vs")) == AliasedEnvironment->FindSource(Extended(Path("a/api.vs"))) &&
			AliasedEnvironment->AuthorizeSource(Extended(Path("a/api.vs")), Main, AliasError) &&
			AliasedEnvironment->AuthorizeSource(Path("a/api.vs"), Extended(Path("project")), AliasError, true),
			"extended roots and ordinary importers share physical ownership and direct dependencies");
		auto AliasOptions = VeroScript::Test::MakeCompileOptions();
		AliasOptions.ModuleEnvironment = AliasedEnvironment;
		auto NormalEntry = VeroScriptCompile::CompileSource(
			"import \"lib/a.vs\";int main(){return answer();}", Main, AliasOptions);
		Run(NormalEntry, AliasedEnvironment, 42);
		AliasOptions.ModuleEnvironment = Environment;
		auto ExtendedEntry = VeroScriptCompile::CompileSource(
			"import \"lib/a.vs\";int main(){return answer();}", Extended(Main), AliasOptions);
		Run(ExtendedEntry, Environment, 42);
		Expect(AliasedEnvironment && !AliasedEnvironment->AuthorizeSource(Path("b/api.vs"), Extended(Main), AliasError) &&
			!AliasedEnvironment->AuthorizeSource(Extended(Path("project-sibling/leak.vs")), Main, AliasError),
			"path aliases do not grant transitive dependencies or similarly prefixed sibling directories");
		AliasedDescription.SourceRoots.push_back({Path("project"), "project"});
		Expect(!FModuleEnvironment::Create(AliasedDescription, AliasError), "ordinary and extended aliases cannot register duplicate roots");
		std::string LongRoot = Path("long-project");
		while (LongRoot.size() < 300u)
		{
			LongRoot += "/extended-path-component";
		}
		const auto PackageRoot = LongRoot + "/packages/compiler";
		std::filesystem::create_directories(PathUtil::FromUtf8(PackageRoot));
		Expect(PathUtil::FromUtf8(LongRoot).native().rfind(L"\\\\?\\", 0) == 0 &&
			PathUtil::ToUtf8(PathUtil::FromUtf8(LongRoot)) == LongRoot &&
			PathUtil::Equal(LongRoot, Extended(LongRoot)),
			"long paths retain extended filesystem access and a single public identity");
		FModuleEnvironmentDesc LongDescription;
		LongDescription.EntryOwnerId = "project";
		LongDescription.Scopes = {{"project", {"compiler"}}, {"compiler", {}}};
		LongDescription.SourceRoots = {{Extended(LongRoot), "project"}, {Extended(PackageRoot), "compiler"}};
		LongDescription.Modules = {{"compiler/api.vs", "compiler.api", Extended(PackageRoot + "/api.vs"),
			"compiler", "compiler1", std::string("export int long_answer(){return 43;}")}};
		auto LongEnvironment = Make(std::move(LongDescription));
		AliasOptions.ModuleEnvironment = LongEnvironment;
		auto LongEntry = VeroScriptCompile::CompileSource(
			"import \"compiler/api.vs\";int main(){return long_answer();}", LongRoot + "/main.vs", AliasOptions);
		Run(LongEntry, LongEnvironment, 43);
		std::filesystem::remove_all(PathUtil::FromUtf8(Extended(Path("long-project"))));
	}
	#endif
	{
		const auto Directory = Path("project/directory.vs");
		const auto Empty = Path("project/empty.vs");
		std::filesystem::create_directory(PathUtil::FromUtf8(Directory));
		std::ofstream(PathUtil::FromUtf8(Empty)).close();
		auto Physical = VeroScript::Test::MakeCompileOptions();
		Expect(!VeroScriptCompile::CompileFile(Directory, Physical).bSuccess,
			"physical directory cannot be the entry source");
		Expect(VeroScriptCompile::CompileFile(Empty, Physical).bSuccess,
			"empty physical source remains valid");
		for (bool Scoped : {false, true})
		{
			Physical.ModuleEnvironment = Scoped ? Environment : nullptr;
			Expect(!VeroScriptCompile::CompileSource("import \"directory.vs\"; void main(){}", Main, Physical).bSuccess,
				"physical directory cannot satisfy an explicit import");
			Expect(VeroScriptCompile::CompileSource("import \"empty.vs\"; void main(){}", Main, Physical).bSuccess,
				"empty physical import remains valid");
			Physical.ImplicitImportPaths = {Directory};
			Expect(!VeroScriptCompile::CompileSource("void main(){}", Main, Physical).bSuccess,
				"physical directory cannot satisfy an implicit import");
			Physical.ImplicitImportPaths.clear();
		}
		Physical.SourceOverlays.push_back({Directory, "export int from_overlay(){return 1;}"});
		Expect(VeroScriptCompile::CompileFile(Directory, Physical).bSuccess &&
			VeroScriptCompile::CompileSource("import \"directory.vs\"; int main(){return from_overlay();}", Main, Physical).bSuccess,
			"explicit source overlays retain their virtual file labels");
	}
	auto C = VeroScriptCompile::CompileSource("import \"lib/a.vs\"; int main(){return answer();}", Main, O);
	Run(C, Environment, 42);
	Expect(C.PreparedProgram && C.PreparedProgram->GetProgram().ModuleEnvironment == Environment, "prepared program retains environment");
	auto Denied = VeroScriptCompile::CompileSource("import \"lib/b.vs\"; int main(){return b();}", Main, O);
	Expect(!Denied.bSuccess && Denied.Error.Message.find("scope") != std::string::npos, "reject undeclared transitive import");
	O.SourceOverlays.push_back({Path("project/lib/a.vs"), "export int local(){return 9;}"});
	auto Ambiguous = VeroScriptCompile::CompileSource("import \"lib/a.vs\";", Main, O);
	Expect(!Ambiguous.bSuccess && Ambiguous.Error.Message.find("ambiguous") != std::string::npos, "mapped/local import ambiguity");
	auto Local = VeroScriptCompile::CompileSource("import \"./lib/a.vs\"; int main(){return local();}", Main, O);
	Run(Local, Environment, 9);
	O.SourceOverlays.clear();
	{
		auto RelativeDescription = D;
		RelativeDescription.ImportPaths = {Path("a")};
		auto RelativeEnvironment = Make(std::move(RelativeDescription));
		FCompileOptions RelativeOptions = VeroScript::Test::MakeCompileOptions();
		RelativeOptions.ModuleEnvironment = RelativeEnvironment;
		{
			std::ofstream File(Root / "a/fallback.vs");
			File << "export int fallback(){return 31;}";
		}
		auto Named = VeroScriptCompile::CompileSource(
			"import \"fallback.vs\"; int main(){return fallback();}", Main, RelativeOptions);
		Run(Named, RelativeEnvironment, 31);
		RelativeOptions.SourceOverlays.push_back({Path("a/buffer.vs"), "export int buffer_value(){return 32;}"});
		auto Buffer = VeroScriptCompile::CompileSource("import \"buffer.vs\"; int main(){return buffer_value();}", Main, RelativeOptions);
		Run(Buffer, RelativeEnvironment, 32);
		RelativeOptions.SourceOverlays.push_back({Path("a/api.vs"), "export int answer(){return 33;}"});
		auto Override = VeroScriptCompile::CompileSource("import \"lib/a.vs\"; int main(){return answer();}", Main, RelativeOptions);
		Run(Override, RelativeEnvironment, 33);
		Expect(!VeroScriptCompile::CompileSource("import \"./fallback.vs\"; int main(){return fallback();}", Main, RelativeOptions).bSuccess,
			"missing explicit relative import cannot resolve in another search root");
		std::string OverlayContent, OverlayError;
		Expect(RelativeEnvironment->Apply(RelativeOptions, OverlayError) && RelativeOptions.CompileProcessing.ReadDependency(
			RelativeEnvironment->GetDescription().ImportPaths.front() + "/buffer.vs", OverlayContent) && OverlayContent == "export int buffer_value(){return 32;}",
			"dependency revalidation reads the same canonical unsaved overlay as import resolution");
	}
	{
		auto MappedDescription = D;
		MappedDescription.Modules.push_back({"mapped.vs", "project.mapped", Path("project/mapped.vs"), "project", "m1",
			std::string("export int mapped_value(){return 34;}")});
		auto MappedEnvironment = Make(std::move(MappedDescription));
		FCompileOptions MappedOptions = VeroScript::Test::MakeCompileOptions();
		MappedOptions.ModuleEnvironment = MappedEnvironment;
		// The temporary root may use a short Windows path or a symlink spelling.
		// Its unsaved local candidate and the mapped source still identify one module.
		auto Mapped = VeroScriptCompile::CompileSource("import \"mapped.vs\"; int main(){return mapped_value();}", Main, MappedOptions);
		Run(Mapped, MappedEnvironment, 34);
		std::error_code AliasError;
		std::filesystem::create_directory_symlink(Root / "project", Root / "project-alias", AliasError);
		if (!AliasError)
		{
			auto Aliased = VeroScriptCompile::CompileSource("import \"mapped.vs\"; int main(){return mapped_value();}", Path("project-alias/main.vs"), MappedOptions);
			Run(Aliased, MappedEnvironment, 34);
		}
	}
	{
		// A cyclic import must reuse the explicit entry source rather than its mapped snapshot.
		auto EntryDescription = D;
		EntryDescription.Modules.push_back({"entry.vs", "project.entry", Main, "project", "e1",
			std::string("export int entry_value(){return 36;}")});
		EntryDescription.Modules.push_back({"entry-user.vs", "project.entry-user", "veroscript-module:/project/entry-user.vs", "project", "u1",
			std::string("import \"entry.vs\"; export int imported_entry_value(){return entry_value();}")});
		auto EntryEnvironment = Make(std::move(EntryDescription));
		FCompileOptions EntryOptions = VeroScript::Test::MakeCompileOptions();
		EntryOptions.ModuleEnvironment = EntryEnvironment;
		auto Entry = VeroScriptCompile::CompileSource("import \"entry-user.vs\"; export int entry_value(){return 35;} int main(){return imported_entry_value();}", Main, EntryOptions);
		Run(Entry, EntryEnvironment, 35);
	}
	O.SourceOverlays.push_back({Path("outside/hidden.vs"), "export int secret(){return 99;}"});
	auto Overlay = VeroScriptCompile::CompileSource("import \"hidden.vs\"; int main(){return secret();}", Main, O);
	Expect(!Overlay.bSuccess, "overlay suffix cannot bypass explicit source policy");
	auto Escape = VeroScriptCompile::CompileSource("#include \"../outside/hidden.vs\"\nint main(){return secret();}", Main, O);
	Expect(!Escape.bSuccess, "include obeys the same source/dependency policy");
	O.SourceOverlays.clear();
	std::ofstream(Root / "outside/hidden.vs") << "export int secret(){return 99;}";
	std::error_code LinkError;
	std::filesystem::create_symlink(Root / "outside/hidden.vs", Root / "project/link.vs", LinkError);
	if (!LinkError)
	{
		Expect(!VeroScriptCompile::CompileSource("import \"./link.vs\";", Main, O).bSuccess,
			"symlink cannot escape source roots");
	}

	D.Modules[0].Source = "export import \"lib/b.vs\";";
	auto ReExportEnvironment = Make(D);
	O.ModuleEnvironment = ReExportEnvironment;
	auto ReExport = VeroScriptCompile::CompileSource("import \"lib/a.vs\"; int main(){return ANSWER + b();}", Main, O);
	Run(ReExport, ReExportEnvironment, 84);
	D.Modules.push_back(D.Modules[0]);
	std::string Error;
	Expect(!FModuleEnvironment::Create(D, Error), "reject duplicate public module mapping");
	D.Modules.pop_back();
	D.SourceRoots[0].Path = "relative/project";
	Expect(!FModuleEnvironment::Create(D, Error), "require explicit absolute physical roots");
	D.SourceRoots[0].Path = Path("project");
	D.Modules[0].Path = "veroscript-module:/a/api.vs";
	D.Modules[0].Source = "export int answer(){return 17;}";
	auto MemoryEnvironment = Make(D);
	O.ModuleEnvironment = MemoryEnvironment;
	auto Memory = VeroScriptCompile::CompileSource("import \"lib/a.vs\"; int main(){return answer();}", Main, O);
	Run(Memory, MemoryEnvironment, 17);
	// Non-source resources use exact logical identities and a real cache ledger.
	auto Resources = std::make_shared<FResources>();
	D = {};
	D.EntryOwnerId = "project";
	D.Scopes = {{"project", {"data"}}, {"data", {}}};
	D.SourceRoots = {{Path("project"), "project"}};
	FResourceProviderBinding Binding;
	Binding.Id = "data";
	Binding.OwnerId = "data";
	Binding.ContractIdentity = "memory-v1";
	Binding.Provider = Resources;
	Binding.ModuleIds = {"data.module"};
	D.Resources = {Binding};
	auto Registry = std::make_shared<FCompileProcessorRegistry>();
	FCompileProcessorDesc Processor;
	Processor.ProcessorId = "resource.generate";
	Processor.Version = "0.1.1";
	Processor.Phases = CompilePhaseMask(ECompileProcessingPhase::Members);
	Processor.Operations = CompileOperationMask(ECompileOperation::AddDeclaration);
	Processor.Subscriptions.push_back({ECompileProcessingEvent::Module});
	Processor.Factory = []
	{
		return std::make_unique<FGenerator>();
	};
	Expect(Registry->Register(Processor, Error), "register resource generator");
	Registry->Freeze();
	D.CompileProcessing.Registry = Registry;
	auto ResourceEnvironment = Make(D);
	O = {};
	O.ModuleEnvironment = ResourceEnvironment;
	auto Generated = VeroScriptCompile::CompileSource("int main(){return generated_resource();}", Main, O);
	Run(Generated, ResourceEnvironment, 61);
	Expect(ResourceEnvironment->Apply(O, Error), "normalize environment compile inputs");
	Expect(Generated.PreparedProgram && Generated.PreparedProgram->MatchesDependencies(O.CompileProcessing), "resource dependency cache hit");
	Resources->Value = "62";
	Expect(Generated.PreparedProgram && !Generated.PreparedProgram->MatchesDependencies(O.CompileProcessing), "changed resource invalidates preparation");
	Resources->Exists = false;
	auto Missing = VeroScriptCompile::CompileSource("int main(){return generated_resource();}", Main, O);
	Run(Missing, ResourceEnvironment, 7);
	Resources->Exists = true;
	Expect(Missing.PreparedProgram && !Missing.PreparedProgram->MatchesDependencies(O.CompileProcessing), "previously missing resource invalidates when it appears");
	FResourceData Data;
	FResourceRequest Request{"data", "data.module", "templates", "../secret"};
	Expect(!ResourceEnvironment->ReadResource(Request, Main, {}, Data, Error), "reject resource traversal");
	Request.Path = "value.txt";
	Expect(!ResourceEnvironment->ReadResource(Request, Main, {{}, 1}, Data, Error), "honor resource output budget");
	Resources->Fail = true;
	Expect(!ResourceEnvironment->ReadResource(Request, Main, {}, Data, Error) && Error == "resource broken", "resource failure is not a missing key");
	Resources->Fail = false;
	bool Cancelled = false;
	try
	{
		ResourceEnvironment->ReadResource(Request, Main,
			{[]
				{
					throw std::runtime_error("cancel resource");
				},
				8},
			Data, Error);
	}
	catch (const std::runtime_error&)
	{
		Cancelled = true;
	}
	Expect(Cancelled, "resource cancellation is observed before reading");
	const auto Id = MakeResourceDependencyId(Request, Main);
	FResourceRequest Parsed;
	std::string From;
	Expect(ParseResourceDependencyId(Id, Parsed, From) && From == Main && Parsed.Path == Request.Path, "resource dependency identity round trip");
	Expect(!ParseResourceDependencyId(Id + "junk", Parsed, From), "resource identity rejects trailing fields");
	// Script processors use the compiler module's scope for resources, even when
	// the business module does not directly depend on the data provider.
	FModuleEnvironmentDesc ScriptDescription;
	ScriptDescription.EntryOwnerId = "project";
	ScriptDescription.Scopes = {{"project", {"compiler"}}, {"compiler", {"data"}}, {"data", {}}, {"stdlib", {}, true}};
	const auto Stdlib = PathUtil::ToUtf8(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Resources/Scripts/stdlib");
	ScriptDescription.SourceRoots = {{Path("project"), "project"}, {Stdlib, "stdlib"}};
	ScriptDescription.StdlibSearchPaths = {Stdlib};
	ScriptDescription.Resources = {Binding};
	const std::string ScriptProcessor = R"vs(
import "compiler.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
callback create(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 auto resource=ctx.read_resource("data","data.module","templates","value.txt");
 out.add_declaration(ctx.top_scope(),"int generated_script(){return "+resource.content()+";}");
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::subscription s=std::compiler::subscription(); s.event=std::compiler::event::module;
 std::compiler::processor_definition p=std::compiler::processor_definition();p.id="resource.script";p.version="0.1.1";
 p.phases=array<std::compiler::phase>{std::compiler::phase::members};p.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration};
 p.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(p,create);
})vs";
	ScriptDescription.Modules = {{"compiler/processor.vs", "compiler.processor", "veroscript-module:/compiler/processor.vs", "compiler", "s1", ScriptProcessor}};
	auto ScriptEnvironment = Make(ScriptDescription);
	FCompileOptions ScriptOptions = VeroScript::Test::MakeCompileOptions();
	ScriptOptions.ModuleEnvironment = ScriptEnvironment;
	auto ScriptGenerated = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, ScriptOptions);
	Run(ScriptGenerated, ScriptEnvironment, 62);
	Expect(!ScriptEnvironment->ReadResource(Request, Main, {}, Data, Error), "business scope cannot directly read a compiler-only resource");
	{
		// Logical resource paths retain case even on a case-insensitive filesystem.
		FResourceData Seed;
		Seed.bExists = true;
		Seed.Bytes = {'6', '2'};
		Seed.ContentIdentity = "62";
		auto SeededOptions = ScriptOptions;
		SeededOptions.CompileProcessing.InitialDependencies = {
			{MakeResourceDependencyId({"data", "data.module", "templates", "Value.txt"}, "compiler"), ResourceDependencyContent(Seed), true},
			{MakeResourceDependencyId({"data", "data.module", "templates", "value.txt"}, "compiler"), ResourceDependencyContent(Seed), true}};
		auto Seeded = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, SeededOptions);
		Run(Seeded, ScriptEnvironment, 62);
	}

	{
		// Allowed roots use physical identities and apply before cached reads.
		auto PhysicalDescription = ScriptDescription;
		const auto ProcessorPath = Path("project/processor-extension.vs");
		PhysicalDescription.Modules.front().Path = ProcessorPath;
		auto PhysicalEnvironment = Make(PhysicalDescription);
		FCompileOptions PhysicalOptions = VeroScript::Test::MakeCompileOptions();
		PhysicalOptions.ModuleEnvironment = PhysicalEnvironment;
		PhysicalOptions.ScriptCompileExtensions.AllowedRoots = {Path("project")};
		PhysicalOptions.SourceOverlays = {{ProcessorPath, ScriptProcessor}};
		auto Physical = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, PhysicalOptions);
		Run(Physical, PhysicalEnvironment, 62);
		std::error_code PolicyAliasError;
		if (std::filesystem::is_directory(Root / "project-alias", PolicyAliasError))
		{
			auto AliasedOptions = PhysicalOptions;
			AliasedOptions.SourceOverlays = {{Path("project-alias/processor-extension.vs"), ScriptProcessor}};
			auto Aliased = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, AliasedOptions);
			Run(Aliased, PhysicalEnvironment, 62);
		}
		const auto InputPath = Path("project/compiler-input.txt");
		auto RegistrationSource = ScriptProcessor;
		RegistrationSource.insert(RegistrationSource.find("using callback"),
			"std::compiler::dependency initial=std::compiler::read_dependency(\"" + InputPath + "\");\n");
		PhysicalDescription.Modules.front().Source = RegistrationSource;
		PhysicalDescription.Modules.push_back({"compiler/input.txt", "compiler.input", InputPath, "compiler", "i1", std::string("input")});
		auto ReadingEnvironment = Make(PhysicalDescription);
		FCompileOptions ReadingOptions = VeroScript::Test::MakeCompileOptions();
		ReadingOptions.ModuleEnvironment = ReadingEnvironment;
		ReadingOptions.ScriptCompileExtensions.AllowedRoots = {Path("project")};
		auto Reading = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, ReadingOptions);
		Run(Reading, ReadingEnvironment, 62);
		const auto DeniedPath = Path("b/outside-policy.txt");
		{
			std::ofstream File(PathUtil::FromUtf8(DeniedPath));
			File << "cached input";
		}
		auto CachedSource = ScriptProcessor;
		CachedSource.insert(CachedSource.find(" auto resource="), " auto denied=ctx.read_dependency(\"" + DeniedPath + "\");\n");
		PhysicalDescription.Modules.front().Source = CachedSource;
		PhysicalDescription.SourceRoots.push_back({Path("b"), "data"});
		auto CachedEnvironment = Make(std::move(PhysicalDescription));
		FCompileOptions CachedOptions = VeroScript::Test::MakeCompileOptions();
		CachedOptions.ModuleEnvironment = CachedEnvironment;
		CachedOptions.ScriptCompileExtensions.AllowedRoots = {Path("project")};
		auto Uncached = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, CachedOptions);
		Expect(!Uncached.bSuccess && Uncached.Error.Message.find("allowed roots") != std::string::npos, "uncached processor dependency obeys allowed roots");
		CachedOptions.CompileProcessing.InitialDependencies = {{DeniedPath, "cached input", true}};
		auto Cached = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, CachedOptions);
		Expect(!Cached.bSuccess && Cached.Error.Message.find("allowed roots") != std::string::npos, "cached processor dependency obeys the same allowed roots");
	}
	const auto DataPath = PathUtil::Normalize(Path("b/input.txt"));
	const auto SecretPath = PathUtil::Normalize(Path("outside/private.txt"));
	{
		std::ofstream File(PathUtil::FromUtf8(DataPath));
		File << "data";
	}
	{
		std::ofstream File(PathUtil::FromUtf8(SecretPath));
		File << "private";
	}
	ScriptDescription.Scopes.push_back({"secret", {}});
	ScriptDescription.SourceRoots.push_back({Path("b"), "data"});
	ScriptDescription.SourceRoots.push_back({Path("outside"), "secret"});
	auto FileProcessor = ScriptProcessor;
	const auto ReadPosition = FileProcessor.find(" auto resource=");
	FileProcessor.insert(ReadPosition, " auto file=ctx.read_dependency(\"" + DataPath + "\");\n");
	ScriptDescription.Modules.front().Source = FileProcessor;
	auto FileEnvironment = Make(ScriptDescription);
	ScriptOptions = VeroScript::Test::MakeCompileOptions();
	ScriptOptions.ModuleEnvironment = FileEnvironment;
	auto Files = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, ScriptOptions);
	Run(Files, FileEnvironment, 62);
	const auto SecretRead = "std::compiler::dependency forbidden=std::compiler::read_dependency(\"" + SecretPath + "\");\n";
	FileProcessor.insert(FileProcessor.find("using callback"), SecretRead);
	ScriptDescription.Modules.front().Source = FileProcessor;
	auto ForbiddenEnvironment = Make(ScriptDescription);
	ScriptOptions = VeroScript::Test::MakeCompileOptions();
	ScriptOptions.ModuleEnvironment = ForbiddenEnvironment;
	auto Forbidden = VeroScriptCompile::CompileSource("import compiler \"compiler/processor.vs\"; int main(){return generated_script();}", Main, ScriptOptions);
	Expect(!Forbidden.bSuccess && Forbidden.Error.Message.find("scope") != std::string::npos, "compiler registration cannot read another undeclared source scope");

	// Contracts survive bytecode and binary-package boundaries; mismatches fail before init.
	FExternalDependency Required{"runtime.native", "0.1.1", "abi-a", "build-a", "content-a"};
	FModuleEnvironmentDesc E;
	E.EntryOwnerId = "project";
	E.Scopes = {{"project", {}}};
	E.SourceRoots = {{Path("project"), "project"}};
	E.AvailableDependencies = {Required};
	E.RequiredDependencies = {Required};
	auto RuntimeEnvironment = Make(E);
	O = {};
	O.ModuleEnvironment = RuntimeEnvironment;
	auto Contract = VeroScriptCompile::CompileSource("print(\"initialized\"); int main(){return 5;}", Main, O);
	Expect(Contract.bSuccess && Contract.Module->RequiredExternalDependencies == E.RequiredDependencies, "compile keeps execution dependencies");
	std::vector<std::uint8_t> Bytes;
	Expect(Contract.bSuccess && VeroScriptBytecodeIO::SerializeBytecode(*Contract.Module, 0, Bytes, Error), "serialize dependency contracts");
	auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
	Expect(Loaded && Loaded->RequiredExternalDependencies == E.RequiredDependencies, "deserialize dependency contracts");
	if (Loaded)
	{
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Expect(!Vm.Execute(*Loaded, ERunMode::Script, {}) && Output.str().empty(), "missing environment fails before initialization");
		Expect(Vm.SetModuleEnvironment(RuntimeEnvironment, Error) && Vm.Execute(*Loaded, ERunMode::Script, {}), "matching contracts execute");
		E.AvailableDependencies[0].Abi = "wrong";
		E.RequiredDependencies.clear();
		auto Wrong = Make(E);
		Output.str("");
		Expect(Vm.SetModuleEnvironment(Wrong, Error) && !Vm.Execute(*Loaded, ERunMode::Script, {}) && Output.str().empty(), "ABI mismatch fails before initialization");
	}
	{
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		E.RequiredDependencies.clear();
		E.I18nServices = std::make_shared<I18n::FI18nServices>();
		auto Localized = Make(E);
		Expect(Vm.SetModuleEnvironment(Localized, Error) && Vm.GetI18nServices() == E.I18nServices, "environment supplies app localization");
		Expect(Vm.SetModuleEnvironment(RuntimeEnvironment, Error) && !Vm.GetI18nServices(), "new environment does not retain the previous app localization");
	}
	std::filesystem::remove_all(Root);
	return Failures ? 1 : 0;
}
