// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"

#include "NetworkProvider.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace
{
	template <class T, class = void>
	struct TIsComplete : std::false_type
	{
	};
	template <class T>
	struct TIsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type
	{
	};
	static_assert(!TIsComplete<VeroScript::Network::Detail::FContext>::value);
	static_assert(!TIsComplete<VeroScript::Network::Detail::FOperation>::value);

	void Check(bool Value, const char* Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
} // namespace

int main()
{
	try
	{
		namespace N = VeroScript::Network;
		namespace P = N::Detail;
		N::FContextOptions Options;
		Options.Mode = N::EDriveMode::HostDriven;
		N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
		auto Provider = P::FAccess::Context(Context);
		Check(!P::IsClosed(Provider) && P::Options(Provider).Mode == Options.Mode, "Provider context access failed");
		unsigned Posted = 0, Closed = 0;
		N::FError Error;
		P::Post(Provider,
			[&Posted]
			{
				++Posted;
			});
		Context.PollIo(Error, 16);
		Check(Posted == 1, "Provider post did not use the owning executor");
		const auto Resource = P::AddResource(Provider,
			[&Closed]
			{
				++Closed;
			});
		Check(Resource != 0, "Provider resource was not registered");
		N::FOperationOptions Work;
		Work.Timeout = std::nullopt;
		auto Operation = P::Begin(Provider, Work);
		Check(P::OperationContext(Operation) == Provider && P::OperationId(Operation) != 0,
			"Provider operation owner failed");
		P::Running(Operation);
		Check(P::State(Operation) == N::EOperationState::Running, "Provider operation state failed");
		Check(P::Complete(Operation, std::uint64_t(42)), "Provider completion failed");
		const auto Public = P::FAccess::Wrap(Operation);
		Check(Public.Result<std::uint64_t>() && *Public.Result<std::uint64_t>() == 42, "Provider result was lost");
		{
			const P::FOperationErrorRead Error(Operation);
			Check(!Error.Get(), "Successful provider operation has an error");
		}
		auto Lease = P::ReserveBytes(Provider, 64, Error);
		Check(Lease && Lease->Size() == 64 && Lease->OwnerContext() == Provider, "Provider byte lease failed");
		Context.Shutdown();
		Check(P::IsClosed(Provider) && Closed == 1, "Provider shutdown did not close the shared resource once");
		Check(Context.MemoryUsage().RetainedBytes >= 64, "Shutdown released an externally retained byte lease");
		Lease.reset();
		Check(Context.MemoryUsage().RetainedBytes == 0, "Provider byte lease did not release its quota");
		std::cout << "Opaque foundation provider contract passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
