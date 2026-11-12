// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#include "VeroScriptCompile.h"
#include "VeroScriptVm.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>

using namespace VeroScript;
namespace
{
	int Failures = 0, Creates = 0;
	std::vector<FExternalDependency> NativeContracts;
	std::string JitPath;
	void Expect(bool B, const std::string& N)
	{
		if (!B)
		{
			++Failures;
			std::cerr << "FAIL: " << N << '\n';
		}
	}

	void* VEROSCRIPT_EXTENSION_CALL DummyCreate(const void*, char*, std::uint32_t)
	{
		++Creates;
		return new int(1);
	}

	void VEROSCRIPT_EXTENSION_CALL DummyDestroy(void* P)
	{
		delete static_cast<int*>(P);
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL BadQuery(std::uint32_t V, std::uint32_t N, void* P)
	{
		if (V != NativeExtensionApiVersion || N != sizeof(FNativeExtensionDescriptor)) return 0;
		FNativeExtensionDescriptor D;
		std::memcpy(D.Abi, "wrong", 6);
		std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
		std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
		std::memcpy(D.Id, "bad", 4);
		std::memcpy(D.Version, "0.1.1", 6);
		D.Roles = 1;
		D.Create = DummyCreate;
		D.Destroy = DummyDestroy;
		std::memcpy(P, &D, sizeof(D));
		return V;
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL StaticQuery(std::uint32_t V, std::uint32_t N, void* P)
	{
		BadQuery(V, N, P);
		auto& D = *static_cast<FNativeExtensionDescriptor*>(P);
		D.Flags = 0;
		const auto Abi = BuildNativeExtensionAbi();
		std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
		std::memcpy(D.Id, "static-fixture", 15);
		return V;
	}

	bool ExceptionOwnerAlive = false, ExceptionDestroyedWithOwner = false, ThrowUnknown = false;
	struct FExceptionOwner
	{
		FExceptionOwner()
		{
			ExceptionOwnerAlive = true;
			ExceptionDestroyedWithOwner = false;
		}
		~FExceptionOwner()
		{
			ExceptionOwnerAlive = false;
		}
	};

	struct FOwnedException final : std::exception
	{
		//~ Begin std::exception Interface
		const char* what() const noexcept override
		{
			return ExceptionOwnerAlive ? "owned native exception" : "exception owner already released";
		}
		~FOwnedException() override
		{
			ExceptionDestroyedWithOwner = ExceptionOwnerAlive;
		}
		//~ End std::exception Interface
	};

	struct FUnknownOwnedException
	{
		~FUnknownOwnedException()
		{
			ExceptionDestroyedWithOwner = ExceptionOwnerAlive;
		}
	};
	[[noreturn]] void ThrowOwnedException()
	{
		if (ThrowUnknown)
		{
			throw FUnknownOwnedException{};
		}

		throw FOwnedException{};
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL ThrowingQuery(std::uint32_t, std::uint32_t, void*)
	{
		ThrowOwnedException();
	}

	void* VEROSCRIPT_EXTENSION_CALL ThrowingCreate(const void*, char*, std::uint32_t)
	{
		ThrowOwnedException();
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL ThrowingFactoryQuery(std::uint32_t V, std::uint32_t N, void* P)
	{
		StaticQuery(V, N, P);
		auto& D = *static_cast<FNativeExtensionDescriptor*>(P);
		D.Create = ThrowingCreate;
		return V;
	}

	void CheckExceptionLifetimes()
	{
		for (const bool Unknown : {false, true})
		{
			ThrowUnknown = Unknown;
			std::string Error;
			const auto Expected = Unknown ? "native extension query threw an exception" : "owned native exception";
			const bool QueryFailed = !QueryNativeExtension(ThrowingQuery, std::make_shared<FExceptionOwner>(), Error);
			Expect(QueryFailed &&
					Error == Expected && ExceptionDestroyedWithOwner && !ExceptionOwnerAlive,
				"query exception retains owner through catch and exception destruction");
			FNativeExtensionLoadOptions Options;
			Options.FileLease = std::make_shared<FExceptionOwner>();
			const bool LeaseQueryFailed = !QueryNativeExtension(ThrowingQuery, {}, Error, std::move(Options));
			Expect(LeaseQueryFailed &&
					Error == Expected && ExceptionDestroyedWithOwner && !ExceptionOwnerAlive,
				"query exception retains file lease through catch and exception destruction");
			auto Module = QueryNativeExtension(ThrowingFactoryQuery, std::make_shared<FExceptionOwner>(), Error);
			Expect(Module != nullptr, "throwing factory handshake: " + Error);
			FNativeExtensionRegistry Registry;
			FNativeExtensionRegistrationOptions Registration;
			Registration.Origin.ProviderId = "throwing-native";
			const bool RegistrationFailed = !Registry.Add(std::move(Module), std::move(Registration), Error);
			Expect(RegistrationFailed &&
					Error == (Unknown ? "native registration factory threw an exception" : "owned native exception") &&
					ExceptionDestroyedWithOwner && !ExceptionOwnerAlive && Registry.GetOwners().empty(),
				"factory exception retains code through catch without committing registration");
		}
	}
	FModuleEnvironmentDesc Description(const std::string& Root, std::shared_ptr<const FHostBindingRegistry> Bindings)
	{
		FModuleEnvironmentDesc D;
		D.EntryOwnerId = "project";
		D.Scopes = {{"project", {"native"}}, {"native", {}}};
		D.SourceRoots = {{Root, "project"}};
		D.Bindings = std::move(Bindings);
		D.AvailableDependencies = NativeContracts;
		return D;
	}
	std::shared_ptr<const FModuleEnvironment> Environment(FModuleEnvironmentDesc D)
	{
		std::string E;
		auto R = FModuleEnvironment::Create(std::move(D), E);
		Expect(R != nullptr, "create native environment: " + E);
		return R;
	}

	void Run(FCompileResult& C, std::shared_ptr<const FModuleEnvironment> E, int Expected)
	{
		if (!C.bSuccess)
		{
			Expect(false, "compile: " + C.Error.Message);
			return;
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (JitPath.empty() && Backend != EExecutionBackend::Vm)
			{
				continue;
			}

			std::string Error;
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			if (!JitPath.empty() && Backend != EExecutionBackend::Vm && !Vm.SetJitBackendPath(JitPath, Error))
			{
				Expect(false, Error);
				continue;
			}
			const bool Ok = Vm.SetModuleEnvironment(E, Error) && Vm.Execute(*C.Module, ERunMode::Script, {});
			Expect(Ok && Vm.GetProcessExitCode() == Expected,
				std::string(ExecutionBackendName(Backend)) + " native execution: " + Vm.GetError().Message +
					Error + " (expected " + std::to_string(Expected) + ", got " +
					std::to_string(Vm.GetProcessExitCode()) + ")");
			if (Ok && Backend == EExecutionBackend::Jit)
			{
				Expect(Vm.GetJitStats().CompileAttempts > 0,
					"JIT path actually prepared the native-dependent program");
			}
		}
	}
	const bool RegisteredBeforeMain = []
	{
		std::string Error;
		auto Module = QueryNativeExtension(StaticQuery, {}, Error);
		FNativeExtensionRegistry Registry;
		FNativeExtensionRegistrationOptions O;
		O.Origin.ProviderId = "pre-main";
		return Module && Registry.Add(std::move(Module), std::move(O), Error);
	}();
}
int main(int Argc, char** Argv)
{
	std::string Error;
	Creates = 0;
	Expect(RegisteredBeforeMain, "static native registration before main");
	CheckExceptionLifetimes();
	if (const auto* Path = std::getenv("VEROSCRIPT_TEST_JIT_BACKEND"))
	{
		JitPath = Path;
	}

	Expect(!QueryNativeExtension(BadQuery, {}, Error) && Creates == 0 &&
			Error.find("mismatch") != std::string::npos,
		"ABI rejected before registration factory");
	auto Static = QueryNativeExtension(StaticQuery, {}, Error);
	Expect(Static != nullptr, "static handshake: " + Error);
	{
		FNativeExtensionRegistry A, B;
		FNativeExtensionRegistrationOptions O;
		O.Origin.ProviderId = "native";
		Expect(A.Add(Static, O, Error), "static registration: " + Error);
		Expect(!B.Add(Static, O, Error) && Error.find("restart") != std::string::npos, "nonparallel registrations require restart");
	}
	if (Argc < 2) return Failures ? 1 : 0; // Static-only platforms still validate registration.
	Expect(!LoadNativeExtension("relative-library.dll", Error), "reject implicit relative library lookup");
	FNativeExtensionLoadOptions Bad;
	Bad.ExpectedId = "different";
	Expect(!LoadNativeExtension(Argv[1], Error, Bad), "validate declared module identity");
	const auto Root = std::filesystem::temp_directory_path() / ("veroscript-native-environment-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(Root);
	const auto Main = PathUtil::ToUtf8(Root / "main.vs");
	std::weak_ptr<const FLoadedNativeExtension> WeakModule;
	FHostObjectHandle Escaped;
	{
		auto Module = LoadNativeExtension(Argv[1], Error);
		Expect(Module != nullptr, "dynamic handshake: " + Error);
		if (!Module) return 1;
		WeakModule = Module;
		{
			FNativeExtensionRegistry First, Second;
			FNativeExtensionRegistrationOptions Configured;
			Configured.Origin.ProviderId = "native";
			Configured.Configuration["generated-value"] = "74";
			Expect(First.Add(Module, Configured, Error), "first configured processor registration");
			Configured.Configuration["generated-value"] = "75";
			Expect(Second.Add(Module, Configured, Error), "second configured processor registration");
			auto BuildEnvironment = [&](FNativeExtensionRegistry& R)
			{
				auto Config = Description(PathUtil::ToUtf8(Root), R.GetBindings());
				Config.AvailableDependencies = R.GetDependencies();
				Config.CompileProcessing.Registry = R.GetProcessors();
				return Environment(std::move(Config));
			};
			auto FirstEnvironment = BuildEnvironment(First), SecondEnvironment = BuildEnvironment(Second);
			FCompileOptions FirstOptions, SecondOptions;
			FirstOptions.ModuleEnvironment = FirstEnvironment;
			SecondOptions.ModuleEnvironment = SecondEnvironment;
			const auto Source = "class Generated {} int main(){return Generated().generated();}";
			auto FirstCode = VeroScriptCompile::CompileSource(Source, Main, FirstOptions);
			Run(FirstCode, FirstEnvironment, 74);
			auto SecondCode = VeroScriptCompile::CompileSource(Source, Main, SecondOptions);
			Run(SecondCode, SecondEnvironment, 75);
			Expect(FirstEnvironment->Apply(FirstOptions, Error) && SecondEnvironment->Apply(SecondOptions, Error), "normalize configured processors");
			Expect(FirstCode.PreparedProgram && FirstCode.PreparedProgram->MatchesProcessing(FirstOptions.CompileProcessing, FirstOptions.Bindings), "unchanged native processor configuration matches preparation");
			Expect(FirstCode.PreparedProgram && !FirstCode.PreparedProgram->MatchesProcessing(SecondOptions.CompileProcessing, SecondOptions.Bindings), "changed native processor configuration invalidates preparation without an extra host fingerprint");
		}
		auto Registry = std::make_unique<FNativeExtensionRegistry>();
		FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = "native";
		Expect(Registry->Add(Module, Registration, Error), "dynamic registration: " + Error);
		NativeContracts = Registry->GetDependencies();
		const auto Before = Registry->GetBindings();
		Expect(!Registry->Add(Module, Registration, Error) && Registry->GetBindings() == Before, "duplicate registration preserves snapshot");
		auto D = Description(PathUtil::ToUtf8(Root), Registry->GetBindings());
		D.CompileProcessing.Registry = Registry->GetProcessors();
		D.Owners = Registry->GetOwners();
		D.CompileConfiguration = Registry->GetFingerprint();
		auto E = Environment(D);
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.ModuleEnvironment = E;
		auto Code = VeroScriptCompile::CompileSource("class Generated {} int main(){ int x=5; native_fixture_ref(ref x); NativeFixtureObject obj=NativeFixtureObject(4); return native_fixture_number()+obj.value()+Generated().generated()+x; }", Main, O);
		Run(Code, E, 120);
		auto CallbackOwners = VeroScriptCompile::CompileSource("int main(){return native_fixture_owners();}", Main, O);
		Run(CallbackOwners, E, 3);
		Expect(Code.bSuccess && Code.Module->RequiredExternalDependencies == D.AvailableDependencies, "native provider identity becomes an execution dependency");
		std::vector<std::uint8_t> Bytes;
		Expect(Code.bSuccess && VeroScriptBytecodeIO::SerializeBytecode(*Code.Module, 0, Bytes, Error), "serialize native-dependent program");
		auto Loaded = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 0, Error);
		if (Loaded)
		{
			std::ostringstream Out;
			VeroScriptVm Vm(Out);
		VeroScript::Test::Configure(Vm);
			Expect(!Vm.Execute(*Loaded, ERunMode::Script, {}),
				"serialized native program requires an explicit environment");
		}
		else
		{
			Expect(false, "deserialize native-dependent program: " + Error);
		}

		// Two source domains can use a native type through an exported API without
		// granting arbitrary free-function access to the transitive provider.
		D.CompileProcessing = {};
		D.Scopes = {{"project", {"wrapper"}}, {"wrapper", {"native"}}, {"native", {}}};
		const auto Wrapper = PathUtil::ToUtf8(Root / "wrapper/api.vs");
		D.SourceRoots.push_back({PathUtil::ToUtf8(Root / "wrapper"), "wrapper"});
		D.Modules = {{"wrapper.vs", "wrapper.api", Wrapper, "wrapper", "w1", std::string("export int wrapper(){return native_fixture_number();}")}};
		auto Scoped = Environment(D);
		O = {};
		O.ModuleEnvironment = Scoped;
		auto Good = VeroScriptCompile::CompileSource("import \"wrapper.vs\"; int main(){return wrapper();}", Main, O);
		Run(Good, Scoped, 37);
		Expect(!VeroScriptCompile::CompileSource("import \"wrapper.vs\"; int main(){return native_fixture_number();}", Main, O).bSuccess, "reject undeclared transitive native function");
		Expect(!VeroScriptCompile::CompileSource("import \"wrapper.vs\"; int main(){NativeFixtureObject value=NativeFixtureObject(2);return value.value();}", Main, O).bSuccess, "reject undeclared native type/constructor");
		D.Modules[0].Source = "export NativeFixtureObject make(){return NativeFixtureObject(4);}";
		auto Exposed = Environment(D);
		O = {};
		O.ModuleEnvironment = Exposed;
		auto API = VeroScriptCompile::CompileSource("import \"wrapper.vs\"; int main(){auto obj=make();return obj.value();}", Main, O);
		Run(API, Exposed, 4);
		// Catalogs retain provenance for tooling while callback addresses stay private.
		std::string Catalog;
		FHostBindingRegistry Metadata;
		Expect(ExportBindingCatalogJson(*Registry->GetBindings(), Catalog) && ImportBindingCatalogJson(Catalog, Metadata, Error), "binding catalog provenance round trip: " + Error);
		Expect(Metadata.FindType("NativeFixtureObject") && Metadata.FindType("NativeFixtureObject")->Origin.ProviderId == "native", "catalog keeps type provider scope");
		// Open generic bodies in a library retain their producer scope rather than
		// inheriting the importing project's narrower direct dependencies.
		FCompileOptions Producer = VeroScript::Test::MakeCompileOptions();
		Producer.ModuleEnvironment = E;
		auto Library = VeroScriptCompile::CompileSource("export int native_answer<T>(T v){return native_fixture_number();}", Main, Producer);
		FPackageBuildOptions Build;
		Build.Kind = EPackageKind::Library;
		Build.PackageName = "native-library";
		std::vector<std::uint8_t> LibraryBytes;
		Expect(Library.bSuccess && BuildPackage(*Library.Module, Build, LibraryBytes, Error), "build native generic library: " + Error);
		if (!LibraryBytes.empty())
		{
			const auto File = Root / "wrapper/native.vslib";
			std::filesystem::create_directories(File.parent_path());
			{
				std::ofstream Out(File, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(LibraryBytes.data()), LibraryBytes.size());
			}
			auto ConsumerDescription = D;
			ConsumerDescription.Modules = {{"native.vslib", "wrapper.native", PathUtil::ToUtf8(File), "wrapper", "library-v1", {}, D.AvailableDependencies}};
			auto Consumer = Environment(ConsumerDescription);
			FCompileOptions ConsumerOptions = VeroScript::Test::MakeCompileOptions();
			ConsumerOptions.ModuleEnvironment = Consumer;
			auto Call = VeroScriptCompile::CompileSource("import \"native.vslib\"; int main(){return native_answer<int>(1);}", Main, ConsumerOptions);
			Run(Call, Consumer, 37);
		}
		// Generic prototypes and member callback bridge owners survive registry copies.
		auto Concrete = std::make_shared<FHostBindingRegistry>(*Registry->GetBindings());
		FHostTypeId Id;
		const auto* Template = Concrete->FindGenericTypeTemplate("NativeFixtureBox");
		Expect(Template && Concrete->InstantiateGenericHostType(Template->Id, {"int"}, Id, Error), "generic native bridge: " + Error);
		for (const auto* Argument : {"3", "true", "-1", "@value:V3:int32:0:3"})
		{
			Expect(Template && !Concrete->InstantiateGenericHostType(Template->Id, {Argument}, Id, Error),
				"type-only native factory rejects scalar arguments");
		}
		auto GenericEnvironment = Environment(Description(PathUtil::ToUtf8(Root), Concrete));
		O = {};
		O.ModuleEnvironment = GenericEnvironment;
		auto Generic = VeroScriptCompile::CompileSource("int main(){NativeFixtureBox<int> obj=NativeFixtureBox<int>();return obj.value();}", Main, O);
		Run(Generic, GenericEnvironment, 19);
		// Provider registration never initializes the service.
		auto Providers = Registry->GetProviders();
		Expect(Providers.size() == 1, "provider indexed");
		if (!Providers.empty())
		{
			FCompileOptions ServiceOptions = VeroScript::Test::MakeCompileOptions();
			ServiceOptions.ModuleEnvironment = E;
			auto BeforeService = VeroScriptCompile::CompileSource("int main(){return native_fixture_services();}", Main, ServiceOptions);
			Run(BeforeService, E, 0);
			auto Service = CreateNativeService(Providers[0], {E, {}, {}}, Error);
			Expect(Service && *static_cast<int*>(Service->Get()) == 41, "explicit delayed service creation: " + Error);
			auto AfterService = VeroScriptCompile::CompileSource("int main(){return native_fixture_services();}", Main, ServiceOptions);
			Run(AfterService, E, 1);
		}
		// A returned host object retains its module and registration after all
		// compilation, catalogs, snapshots, and VM objects leave this scope.
		const auto& Natives = E->GetDescription().Bindings->GetNativeFunctions();
		const auto Ctor = std::find_if(Natives.begin(), Natives.end(),
			[](const auto& N)
		{
			return N.ReturnType == ENativeType::HostObject &&
				N.ReturnHostTypeName == "NativeFixtureObject";
		});
		if (Ctor != Natives.end())
		{
			FValue Arg = std::int32_t{12}, Return;
			FNativeCallContext Context;
			Context.Args = &Arg;
			Context.ArgCount = 1;
			Context.UserData = Ctor->UserData;
			Expect(Ctor->Callback(Context, Return, Error), "create escaped payload");
			Escaped = std::get<FHostObjectHandle>(Return);
		}
		else
		{
			Expect(false, "constructor present for ownership test");
		}
	}
	Expect(Escaped && !WeakModule.expired() && *static_cast<int*>(Escaped->Payload) == 12, "escaped host object retains native module");
	{
		auto Module = WeakModule.lock();
		FNativeExtensionRegistry Replacement;
		FNativeExtensionRegistrationOptions R;
		R.Origin.ProviderId = "native";
		Expect(Replacement.Add(Module, R, Error), "parallel replacement registration");
		auto Bindings = std::make_shared<FHostBindingRegistry>(*Replacement.GetBindings());
		FHostFunctionDesc Previous;
		Previous.Name = "previous_object";
		Previous.ReturnType = ENativeType::HostObject;
		Previous.ReturnHostTypeName = "NativeFixtureObject";
		Expect(Bindings->RegisterNative(
				   Previous,
				   [](const FNativeCallContext& C, FValue& Out, std::string&)
		{
			Out = *static_cast<FHostObjectHandle*>(C.UserData);
			return true;
		},
				   &Escaped, Error),
			"register retained-object probe");
		auto E = Environment(Description(PathUtil::ToUtf8(Root), Bindings));
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.ModuleEnvironment = E;
		auto Probe = VeroScriptCompile::CompileSource("int main(){return previous_object().value();}", Main, O);
		std::ostringstream Output;
		VeroScriptVm Vm(Output);
		VeroScript::Test::Configure(Vm);
		Expect(Probe.bSuccess && Vm.SetModuleEnvironment(E, Error) && !Vm.Execute(*Probe.Module, ERunMode::Script, {}), "reject an object belonging to the previous native registration");
	}
	Escaped.reset();
	Expect(WeakModule.expired(), "module released after last callback/object/provider owner");
	std::filesystem::remove_all(Root);
	return Failures ? 1 : 0;
}
