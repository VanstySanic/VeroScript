// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCommandLine.h"
#include <algorithm>
#include <cstring>
#include <ostream>

using namespace VeroScript;
namespace
{
	struct FState
	{
		std::string Greeting = "Hello";
	};

	bool Configure(const FCommandLineContext&, const FCommandLineValues& V, void* P, std::string&)
	{
		if (V.count("greeting"))
		{
			static_cast<FState*>(P)->Greeting = V.at("greeting").front();
		}

		return true;
	}

	int Greet(const FCommandLineContext& C, const std::vector<std::string>& Args, void* P)
	{
		if (Args.size() > 1)
		{
			*C.Errors << "greet expects at most one name\n";
			return 2;
		}
		*C.Output << static_cast<FState*>(P)->Greeting << ", " << (Args.empty() ? "world" : Args.front()) << "!\n";
		return 0;
	}

	void* VEROSCRIPT_EXTENSION_CALL Create(const void* P, char* Buffer, std::uint32_t Capacity)
	{
		const auto& Host = *static_cast<const INativeExtensionHost*>(P);
		std::string Error;
		auto Service = Host.GetService(CommandLineServiceId, CommandLineServiceVersion, Error);
		auto State = std::make_unique<FState>();
		if (Service)
		{
			FCommandLineModule M;
			M.Id = "cli-example";
			M.Options = {{"greeting", "Greeting text", "text"}};
			M.Commands = {{"greet", "Print a greeting", "[name]", Greet, State.get()}};
			M.Configure = Configure;
			M.UserData = State.get();
			if (static_cast<const ICommandLineExtensionService*>(Service.get())->Register(std::move(M), Error))
			{
				return State.release();
			}
		}
		if (Capacity)
		{
			const auto Count = std::min<std::size_t>(Error.size(), Capacity - 1);
			std::memcpy(Buffer, Error.data(), Count);
			Buffer[Count] = 0;
		}
		return nullptr;
	}

	void VEROSCRIPT_EXTENSION_CALL Destroy(void* P)
	{
		delete static_cast<FState*>(P);
	}
}
extern "C" VEROSCRIPT_EXTENSION_EXPORT std::uint32_t VEROSCRIPT_EXTENSION_CALL veroscript_extension_query(
	std::uint32_t Version, std::uint32_t Size, void* Output)
{
	if (Version != NativeExtensionApiVersion || Size != sizeof(FNativeExtensionDescriptor) || !Output) return 0;
	FNativeExtensionDescriptor D;
	const auto Abi = BuildNativeExtensionAbi();
	std::memcpy(D.Abi, Abi.c_str(), Abi.size() + 1);
	std::memcpy(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID, sizeof(VEROSCRIPT_CORE_IMPLEMENTATION_ID));
	std::memcpy(D.Platform, GetHostPlatformInfo().Platform, std::strlen(GetHostPlatformInfo().Platform) + 1);
	std::memcpy(D.Id, "cli-example", 12);
	std::memcpy(D.Version, "0.1.1", 6);
	D.Roles = NativeExtensionRole(ENativeExtensionRole::CommandLine);
	D.Create = Create;
	D.Destroy = Destroy;
	std::memcpy(Output, &D, sizeof(D));
	return Version;
}
