// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptMemory.h"
#include "VeroScriptSession.h"
#include "VeroScriptHost.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace VeroScript;
static void Require(FMemoryResult Result)
{
	if (!Result)
	{
		throw std::runtime_error(Result.MessageId ? Result.MessageId : "memory operation failed");
	}
}
struct FLeaseScope
{
	FLeaseScope() = default;
	FLeaseScope(const FLeaseScope&) = delete;
	FLeaseScope& operator=(const FLeaseScope&) = delete;
	~FLeaseScope()
	{
		if (Lease.Token.Serial)
		{
			(void)Memory::ReleaseLease(Lease.Token);
		}
	}
	FMemoryLease Lease;
};
int main()
{
	try
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		int Released = 0; // Callback data outlives the Session and region aliases.
		VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		Session.SetHotReloadEnabled(true);
		auto& Context = Session.GetMemoryContext();
		Require(Session.SetMemoryLimits({1024, 2048}));
		FValue Bytes;
		Require(Memory::CreateBuffer(Context, 8, Bytes));
		{
			FLeaseScope Write;
			Require(Memory::AcquireLease(Context, Bytes, true, Write.Lease));
			Write.Lease.WritableData[0] = 42;
		}
		auto Loaded =
			Session.LoadSource("std::memory::buffer packet=new std::memory::buffer(); void main(){} void "
							   "verify(){auto header=packet.readonly_slice(0,1);assert(header.get(0)==42u);}",
				"memory-host.vs");
		if (!Loaded.bSuccess || !Session.Run().bSuccess || !Session.SetGlobal("packet", Bytes).bSuccess ||
			!Session.CallFunction("verify", {}, nullptr).bSuccess)
		{
			throw std::runtime_error("script memory integration failed: " + Errors.str());
		}
		auto External = std::make_shared<std::array<std::uint8_t, 2>>(
			std::array<std::uint8_t, 2>{1, 2});
		FValue Region;
		Require(Memory::WrapExternalRegion(
			Context, External->data(), External->size(), false, External,
			[](void* User)
			{
				++*static_cast<int*>(User);
			},
			&Released, Region));
		FValue View;
		Require(Memory::Slice(Context, Region, 0, 2, true, View));
		Require(Memory::InvalidateExternalRegion(Context, Region));
		std::int64_t Size = 0;
		if (Memory::Size(Context, View, Size).Status != EMemoryStatus::Expired)
		{
			throw std::runtime_error("view should expire");
		}
		Region = std::int32_t{0};
		View = std::int32_t{0};
		if (Released != 1)
		{
			throw std::runtime_error("external release should run exactly once");
		}
		std::cout << "memory host example passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
