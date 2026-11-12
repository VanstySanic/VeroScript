// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Intentionally consumes only public SDK headers and the host service vtable.
#include "VeroScriptCommandLine.h"
#include <cstring>
#include <algorithm>
#include <ostream>
#include <stdexcept>

using namespace VeroScript;
namespace
{
	class FModuleError final : public std::exception
	{
	public:
		const char* what() const noexcept override
		{
			return "fixture native initialization exception";
		}
	};
	#if defined(VEROSCRIPT_CLI_THROW_QUERY) || defined(VEROSCRIPT_CLI_THROW_CREATE)
	void ThrowModuleError()
	{
		throw FModuleError{};
	}
	#endif
	struct FState
	{
		std::string Message = "default";
		bool Flag = false;
		std::vector<std::string> Items;
	};

	bool Configure(const FCommandLineContext&, const FCommandLineValues& V, void* P, std::string& E)
	{
		auto& S = *static_cast<FState*>(P);
		if (V.count("module-message"))
		{
			S.Message = V.at("module-message").front();
		}

		S.Flag = V.count("module-flag") != 0;
		if (V.count("module-item"))
		{
			S.Items = V.at("module-item");
		}

		if (S.Message == "invalid")
		{
			E = "fixture configuration rejected";
			return false;
		}
		if (S.Message == "throw")
		{
			throw std::runtime_error("fixture configure exception");
		}

		return true;
	}

	int Run(const FCommandLineContext& C, const std::vector<std::string>& Args, void* P)
	{
		const auto& S = *static_cast<FState*>(P);
		if (!Args.empty() && Args.front() == "throw")
		{
			throw std::runtime_error("fixture command exception");
		}

		if (!Args.empty() && Args.front() == "exit") return 7;
		*C.Output << S.Message << ':' << (S.Flag ? "on" : "off") << '\n';
		for (const auto& Item : S.Items)
		{
			*C.Output << "item=" << Item << '\n';
		}

		for (const auto& A : Args)
		{
			*C.Output << "arg=" << A << '\n';
		}

		return 0;
	}

	bool Value(const FNativeCallContext& C, FValue& R, std::string&)
	{
		R = static_cast<FState*>(C.UserData)->Message;
		return true;
	}

	void* VEROSCRIPT_EXTENSION_CALL Create(const void* P, char* Buffer, std::uint32_t Capacity)
	{
		#if defined(VEROSCRIPT_CLI_THROW_CREATE)
		ThrowModuleError();
		#endif
		auto& Host = *const_cast<INativeExtensionHost*>(static_cast<const INativeExtensionHost*>(P));
		auto State = std::make_unique<FState>();
		std::string Error;
		FHostFunctionDesc F;
		F.Name = "native_cli_value";
		F.ReturnType = ENativeType::String;
		if (!Host.RegisterFunction(F, Value, State.get(), Error)) return nullptr;
		auto Base = Host.GetService(CommandLineServiceId, CommandLineServiceVersion, Error);
		if (Base)
		{
			const auto* Cli = static_cast<const ICommandLineExtensionService*>(Base.get());
			FCommandLineModule M;
			M.Id = "cli-fixture";
			M.Description = "Example CLI module";
			M.Options = {{"module-message", "Set fixture message", "text"},
				{"module-flag", "Set fixture flag", "", false},
				{"module-item", "Append item", "text", true, true}};
			M.Commands = {{"cli-echo", "Echo arguments without shell parsing", "[args...]", Run, State.get()}};
			M.Configure = Configure;
			M.UserData = State.get();
			#if defined(VEROSCRIPT_CLI_CONFLICT)
			M.Commands.front().Name = "install";
			#endif
			if (!Cli->Register(std::move(M), Error))
			{
				if (Capacity)
				{
					const auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
					std::memcpy(Buffer, Error.data(), Count);
					Buffer[Count] = 0;
				}
				return nullptr;
			}
		}
		return State.release();
	}

	void VEROSCRIPT_EXTENSION_CALL Destroy(void* P)
	{
		delete static_cast<FState*>(P);
	}
}
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(
	std::uint32_t Version, std::uint32_t Size, void* Out)
{
	#if defined(VEROSCRIPT_CLI_THROW_QUERY)
	ThrowModuleError();
	#endif
	if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Out) return 0;
	FNativeExtensionDescriptor D;
	const auto Abi = BuildNativeExtensionAbi();
	std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
	std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
	std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
	std::memcpy(D.Id, "cli-fixture", 12);
	std::memcpy(D.Version, "0.1.1", 6);
	D.Roles = NativeExtensionRole(ENativeExtensionRole::CommandLine) | NativeExtensionRole(ENativeExtensionRole::Bindings);
	D.Flags = NativeExtensionParallelRegistrations;
	D.Create = Create;
	D.Destroy = Destroy;
	std::memcpy(Out, &D, sizeof(D));
	return Version;
}
