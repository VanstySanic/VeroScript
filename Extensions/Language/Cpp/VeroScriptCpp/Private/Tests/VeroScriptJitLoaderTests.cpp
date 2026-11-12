// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestVm.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptJitModule.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
	using namespace VeroScript;

	void Require(bool Condition, const std::string& Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	#if !defined(_WIN32)
	class FUnreadableDirectoryScope final
	{
	public:
		explicit FUnreadableDirectoryScope(std::filesystem::path InPath) : Path(std::move(InPath))
		{
			std::filesystem::create_directory(Path);
			std::filesystem::permissions(Path, std::filesystem::perms::none);
		}

		~FUnreadableDirectoryScope()
		{
			// Restore access before the fixture's directory tree is cleaned up.
			std::error_code Ignored;
			std::filesystem::permissions(Path, std::filesystem::perms::owner_all, Ignored);
		}

	private:
		std::filesystem::path Path;
	};
	#endif

	bool IsLoaded(const std::filesystem::path& Path)
	{
		#if defined(_WIN32)
		return GetModuleHandleW(Path.c_str()) != nullptr;
		#else
		void* Handle = dlopen(Path.c_str(), RTLD_NOW | RTLD_NOLOAD);
		if (Handle)
		{
			dlclose(Handle);
		}
		return Handle != nullptr;
		#endif
	}

	void CheckProductVersionHandshake(const std::filesystem::path& Path)
	{
		#if defined(_WIN32)
		const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>
		    Module(LoadLibraryW(Path.c_str()), &FreeLibrary);
		#else
		const std::unique_ptr<void, decltype(&dlclose)> Module(dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL), &dlclose);
		#endif
		Require(Module != nullptr, "cannot open the backend handshake fixture");
		#if defined(_WIN32)
		const auto Query = reinterpret_cast<FJitModuleQuery>(GetProcAddress(Module.get(), JitModuleQuerySymbol));
		#else
		const auto Query = reinterpret_cast<FJitModuleQuery>(dlsym(Module.get(), JitModuleQuerySymbol));
		#endif
		Require(Query != nullptr, "backend query symbol is missing");
		std::array<unsigned char, sizeof(FJitModuleDescriptor)> Rejected;
		Rejected.fill(0xa5);
		Require(!Query(VEROSCRIPT_VERSION_CODE + 1, Rejected.size(), Rejected.data()) && std::all_of(Rejected.begin(), Rejected.end(), [](const auto Byte)
		{
			return Byte == 0xa5;
		}),
		    "a different product version must be rejected without writing a descriptor");
		Require(!Query(VEROSCRIPT_VERSION_CODE, Rejected.size() - 1, Rejected.data()) && !Query(VEROSCRIPT_VERSION_CODE, Rejected.size(), nullptr),
		    "the product version does not bypass descriptor layout checks");
		FJitModuleDescriptor Descriptor;
		Require(Query(VEROSCRIPT_VERSION_CODE, sizeof(Descriptor), &Descriptor) && Descriptor.CoreVersion == VEROSCRIPT_VERSION_CODE && Descriptor.Abi == BuildJitModuleAbi(),
		    "the JIT module handshake must describe the matching Core product build");
	}

	void Execute(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		std::ostringstream Output, Diagnostics;
		FStreamEngineHost Host(Output, Diagnostics);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.SetExecutionBackend(EExecutionBackend::Jit);
		Session.SetJitBackendFactory(Factory);
		Require(Session.LoadSource("int add(int a,int b){return a+b;}void main(){print(add(20,22));}",
		                   "loader.vs")
		            .bSuccess &&
		        Session.Run().bSuccess && Output.str() == "42\n",
		    Diagnostics.str());
		Require(Session.GetLastJitStats().Dispatches > 0, "dynamic backend did not dispatch");
		std::string Error;
		Require(!Session.SetJitBackendPath("missing-backend", Error) && Session.GetJitBackendFactory() == Factory, "failed selection replaced the active backend");
	}
}

int RunLoaderTests(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() != 4) return 2;
	const auto Root = std::filesystem::temp_directory_path() / std::filesystem::u8path("veroscript-jit-\xe6\xb5\x8b\xe8\xaf\x95-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try
	{
		const auto Source = std::filesystem::u8path(Arguments[1]);
		const auto Library = Root / "asmjit" / "0.1.1" / Source.filename();
		std::filesystem::create_directories(Library.parent_path());
		std::filesystem::copy_file(Source, Library);
		CheckProductVersionHandshake(Library);
		const auto Incompatible = Root / Source.filename();
		std::filesystem::copy_file(std::filesystem::u8path(Arguments[2]), Incompatible);
		const auto InvalidFactory = Root / "0-invalid-factory" / Source.filename();
		std::filesystem::create_directories(InvalidFactory.parent_path());
		std::filesystem::copy_file(std::filesystem::u8path(Arguments[3]), InvalidFactory);
		std::string Error, Selected;
		Require(!LoadJitBackend(PathUtil::ToUtf8(Incompatible), Error) && Error.find("ABI") != std::string::npos,
		    "incompatible ABI was not rejected before factory creation: " + Error);
		Require(!LoadJitBackend(PathUtil::ToUtf8(InvalidFactory), Error) && Error.find("contract") != std::string::npos,
		    "invalid factory was not rejected during loading: " + Error);
		Require(!IsLoaded(InvalidFactory), "rejected factory kept its module loaded");
		std::shared_ptr<const IJitBackendFactory> Factory;
		{
			#if !defined(_WIN32)
			const FUnreadableDirectoryScope Inaccessible(Root / "0-unreadable");
			#endif
			Factory = LoadJitBackend(PathUtil::ToUtf8(Root), Error, &Selected);
			Require(Factory && PathUtil::Equal(Selected, PathUtil::ToUtf8(Library)),
			    "directory failed to skip unusable candidates: " + Error);
		}
		Require(Factory->GetDescription().Id == "asmjit", "wrong backend metadata");
		{
			std::string PathAndError = PathUtil::ToUtf8(Library);
			auto AliasedFactory = LoadJitBackend(PathAndError, PathAndError);
			Require(AliasedFactory && PathAndError.empty(),
			    "core loader rejected a path aliased with the error output: " + PathAndError);
			std::string PathAndSelected = PathUtil::ToUtf8(Root);
			AliasedFactory = LoadJitBackend(PathAndSelected, Error, &PathAndSelected);
			Require(AliasedFactory && Error.empty() && PathUtil::Equal(PathAndSelected, PathUtil::ToUtf8(Library)),
			    "core loader rejected a path aliased with the selected-path output: " + Error);
			const auto Missing = PathUtil::ToUtf8(Root / "missing-backend");
			PathAndError = Missing;
			Require(!LoadJitBackend(PathAndError, PathAndError) && PathAndError.find(Missing) != std::string::npos,
			    "core loader lost an aliased input path from its error diagnostic");
			PathAndSelected = Missing;
			Require(!LoadJitBackend(PathAndSelected, Error, &PathAndSelected) && PathAndSelected.empty() && Error.find(Missing) != std::string::npos,
			    "core loader lost an aliased input path when clearing its selected-path output");
		}
		{
			std::ostringstream Output, Diagnostics;
			FStreamEngineHost Host(Output, Diagnostics);
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.SetJitBackendFactory(Factory);
			std::string PathAndError = "missing-backend";
			Require(!Session.SetJitBackendPath(PathAndError, PathAndError) && PathAndError.find("missing-backend") != std::string::npos && Session.GetJitBackendFactory() == Factory,
			    "aliased failed selection lost its path or replaced the active backend");
			PathAndError = PathUtil::ToUtf8(Library);
			Require(Session.SetJitBackendPath(PathAndError, PathAndError) && PathAndError.empty() && Session.GetJitBackendFactory(),
			    "aliased selection failed to load an existing backend: " + PathAndError);
			Session.SetExecutionBackend(EExecutionBackend::Jit);
			Require(Session.LoadSource("void main(){print(42);}", "aliased-loader.vs").bSuccess && Session.Run().bSuccess && Output.str() == "42\n" && Session.GetLastJitStats().Dispatches > 0,
			    "aliased backend selection did not execute natively: " + Diagnostics.str());
			Require(Session.SetJitBackendPath(PathAndError, PathAndError) && PathAndError.empty() && !Session.GetJitBackendFactory(),
			    "explicit empty path failed to disable the backend");
		}
		Execute(Factory);
		auto Worker = std::async(std::launch::async,
		    [Factory]
		{
			Execute(Factory);
		});
		Execute(Factory);
		Worker.get();

		// An independently retained artifact keeps the library loaded after its cache retires.
		auto Compiled = VeroScriptCompile::CompileSource("int value(){return 49;}", "artifact.vs");
		Require(Compiled.bSuccess, "artifact fixture compilation failed");
		std::shared_ptr<const FBytecodeModule> Module(std::move(Compiled.Module));
		FJitProgramView Program{Module.get(), Module, {AllocateJitProgramIdentity(), 1, 1}};
		auto Instance = Factory->CreateInstance(Program, {}, Error);
		Require(Instance != nullptr, Error);
		FJitCompileRequest Request;
		Request.Program = Program;
		Request.AvailableServices = ~std::uint64_t{0};
		FJitCompileResponse Response;
		Require(Instance->Compile(Request, Response) == EJitCompilationResult::Ready && Response.Function,
		    "standalone artifact compile failed: " + Response.Reason);
		Require(Instance->Publish(*Response.Function, Error), Error);
		auto OtherInstance = Factory->CreateInstance(Program, {}, Error);
		Require(OtherInstance && !OtherInstance->Publish(*Response.Function, Error), "foreign-instance artifact was accepted");
		OtherInstance.reset();
		auto Executable = Response.Function->Executable;
		std::weak_ptr<const IJitExecutable> RetiredExecutable = Executable;
		std::weak_ptr<const IJitBackendFactory> RetiredFactory = Factory;
		Response = {};
		Instance->Retire();
		Instance.reset();
		Factory.reset();
		Require(IsLoaded(Library), "backend unloaded before its retained executable was destroyed");
		Executable.reset();
		Require(RetiredExecutable.expired() && RetiredFactory.expired(),
		    "dynamic backend owners remained alive after their final release");
		#if !defined(__APPLE__)
		Require(!IsLoaded(Library), "backend remained loaded after its final owner was destroyed");
		#endif
		// Darwin retains Mach-O images containing thread-local variables even
		// after a successful dlclose. The owning wrappers must still be released.
		Require(!LoadJitBackend("", Error) && !Error.empty(), "empty loader path accepted");
		std::filesystem::remove_all(Root);
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		std::error_code Ignored;
		std::filesystem::remove_all(Root, Ignored);
		return 1;
	}
}

#if defined(_WIN32)
int wmain(const int ArgumentCount, wchar_t* const ArgumentValues[])
{
	std::vector<std::string> Arguments;
	Arguments.reserve(static_cast<std::size_t>(ArgumentCount));
	for (int Index = 0; Index < ArgumentCount; ++Index)
	{
		Arguments.push_back(VeroScript::PathUtil::ToUtf8(std::filesystem::path(ArgumentValues[Index])));
	}
	return RunLoaderTests(Arguments);
}
#else
int main(const int ArgumentCount, char* const Arguments[])
{
	return RunLoaderTests(std::vector<std::string>(Arguments, Arguments + ArgumentCount));
}
#endif
