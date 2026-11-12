// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"

#include "VeroScriptNetwork.h"
#ifdef boost
#error Private backend configuration leaked into the SDK consumer
#endif
#include <boost/asio.hpp>
#include <iostream>
#include <thread>

int main()
{
	using namespace VeroScript;
	for (const auto Mode : {Network::EDriveMode::HostDriven, Network::EDriveMode::NativeBackground})
	{
		Network::FContextOptions Options;
		Options.Mode = Mode;
		Network::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
		auto Operation = Network::Resolve(Context, "localhost", 80);
		boost::asio::io_context Host;
		bool Ok = false;
		boost::asio::post(Host,
			[&]
			{
				if (!Host.get_executor().running_in_this_thread())
				{
					return;
				}
				Network::FError Error;
				const auto End = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (!Operation.IsComplete() && std::chrono::steady_clock::now() < End)
				{
					if (Mode == Network::EDriveMode::HostDriven)
					{
						Context.PollIo(Error);
					}
					else
					{
						Operation.Wait(std::chrono::milliseconds(10));
					}
					if (Error)
					{
						return;
					}
					std::this_thread::yield();
				}
				auto Addresses = Operation.Result<std::vector<Network::FAddress>>();
				Ok = Operation.IsComplete() && !Operation.Error() && Addresses && !Addresses->empty() &&
					 Host.get_executor().running_in_this_thread();
			});
		Host.run();
		Context.Shutdown();
		if (!Ok)
		{
			std::cerr << "Host Asio configuration interfered with the private network runtime\n";
			return 1;
		}
	}
	std::cout << "Independent host/private Asio configurations passed\n";
	return 0;
}
