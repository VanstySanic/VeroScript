// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetworkProviderHost.h"
#include <atomic>
#include <future>
#include <iostream>
#include <thread>

namespace
{
	using namespace VeroScript;
	struct FState
	{
		int Attempts = 0;
		bool bAvailable = false;
	};
	FNativeServiceProviderDesc Provider(const char* Id, const std::shared_ptr<FState>& State)
	{
		FNativeServiceProviderDesc Result;
		Result.ServiceId = "network";
		Result.BackendId = Id;
		Result.ContractId = "test.contract";
		Result.ContractVersion = "0.1.1";
		Result.Owner = State;
		Result.UserData = State.get();
		Result.Create = [](const auto&, void* Data, std::string& Error) -> void*
		{
			auto& Value = *static_cast<FState*>(Data);
			++Value.Attempts;
			if (!Value.bAvailable)
			{
				Error = "factory is unavailable";
				return nullptr;
			}
			return new int(42);
		};
		Result.Destroy = [](void* Value, void*)
		{
			delete static_cast<int*>(Value);
		};
		return Result;
	}

	bool CheckConcurrentFactoryRelease()
	{
		// Keep both factories in flight, then publish one before the other completes.
		struct FConcurrentState
		{
			std::weak_ptr<Network::FBackendCatalog> Catalog;
			std::atomic<int> Created{0}, Destroyed{0};
			std::atomic<bool> bPublished{false}, bReentered{false};
			std::promise<void> SecondCreated, FirstPublished;
			std::shared_future<void> SecondReady = SecondCreated.get_future().share();
			std::shared_future<void> FirstReady = FirstPublished.get_future().share();
		};
		auto State = std::make_shared<FConcurrentState>();
		auto Catalog = std::make_shared<Network::FBackendCatalog>();
		State->Catalog = Catalog;
		auto Entry = Provider("ready", std::make_shared<FState>());
		Entry.Owner = State;
		Entry.UserData = State.get();
		Entry.Create = [](const auto&, void* Data, std::string&) -> void*
		{
			auto& Value = *static_cast<FConcurrentState*>(Data);
			const auto Id = ++Value.Created;
			if (Id == 1)
			{
				Value.SecondReady.wait();
			}
			else
			{
				Value.SecondCreated.set_value();
				Value.FirstReady.wait();
			}
			return new int(Id);
		};
		Entry.Destroy = [](void* Handle, void* Data)
		{
			auto& Value = *static_cast<FConcurrentState*>(Data);
			delete static_cast<int*>(Handle);
			++Value.Destroyed;
			std::string Error;
			if (auto Current = Value.Catalog.lock())
			{
				Value.bReentered = !Current->Resolve("network", "none", "test.contract", "0.1.1", Error);
			}
		};
		Catalog->Update({Entry});
		std::shared_ptr<void> First, Second;
		const auto Acquire = [&](std::shared_ptr<void>& Result)
		{
			std::string Error;
			Result = Catalog->Resolve("network", "ready", "test.contract", "0.1.1", Error);
			if (!State->bPublished.exchange(true))
			{
				State->FirstPublished.set_value();
			}
		};
		std::thread FirstThread(Acquire, std::ref(First)), SecondThread(Acquire, std::ref(Second));
		FirstThread.join();
		SecondThread.join();
		if (!First || First != Second || State->Created != 2 || State->Destroyed != 1 || !State->bReentered)
		{
			std::cerr << "concurrent factory release: same=" << (First == Second)
			          << ", created=" << State->Created << ", destroyed=" << State->Destroyed
			          << ", reentered=" << State->bReentered << '\n';
			return false;
		}
		First.reset();
		Second.reset();
		return State->Destroyed == 2;
	}

	bool CheckProviderOwnerRelease()
	{
		bool bReentered = false;
		auto Catalog = std::make_shared<Network::FBackendCatalog>();
		auto State = std::shared_ptr<FState>(new FState(),
		    [Weak = std::weak_ptr<Network::FBackendCatalog>(Catalog), &bReentered](FState* Value)
		{
			delete Value;
			try
			{
				if (auto Current = Weak.lock())
				{
					std::string Error;
					bReentered = !Current->Resolve("network", "none", "test.contract", "0.1.1", Error);
				}
			}
			catch (...)
			{
				bReentered = false;
			}
		});
		Catalog->Update({Provider("ready", State)});
		State.reset();
		Catalog->Update({});
		return bReentered;
	}
}

int main()
{
	auto First = std::make_shared<FState>(), Ready = std::make_shared<FState>();
	Ready->bAvailable = true;
	auto Catalog = std::make_shared<VeroScript::Network::FBackendCatalog>();
	const std::vector<FNativeServiceProviderDesc> Providers{Provider("z-ready", Ready), Provider("a-unavailable", First)};
	Catalog->Update(Providers);
	std::string Error;
	auto Automatic = Catalog->Resolve("network", {}, "test.contract", "0.1.1", Error);
	auto Named = Catalog->Resolve("network", "z-ready", "test.contract", "0.1.1", Error);
	if (!Automatic || Automatic != Named || First->Attempts != 1 || Ready->Attempts != 1) return 1;
	if (Catalog->Resolve("network", "a-unavailable", "test.contract", "0.1.1", Error) || First->Attempts != 2 || Ready->Attempts != 1)
	{
		return 2;
	}
	if (Catalog->Resolve("network", "none", "test.contract", "0.1.1", Error) || First->Attempts != 2 || Ready->Attempts != 1)
	{
		return 3;
	}
	if (Catalog->Resolve("network", "z-ready", "wrong.contract", "0.1.1", Error) || Error.find("contract_mismatch") == std::string::npos || Ready->Attempts != 1)
	{
		return 4;
	}
	Catalog->Update(Providers, {}, {{"network", "none"}});
	if (Catalog->Resolve("network", {}, "test.contract", "0.1.1", Error)) return 5;
	if (!Catalog->Resolve("network", "auto", "test.contract", "0.1.1", Error)) return 6;
	if (*static_cast<int*>(Automatic.get()) != 42) return 7;
	auto Qualified = Provider("z-ready", Ready);
	Qualified.ContractId = "test:contract";
	Catalog->Update({Qualified});
	Named = Catalog->Resolve("network", "z-ready", "test:contract", "0.1.1", Error);
	if (!Named || Catalog->Resolve("network", "z-ready", "test", "contract:0.1.1", Error)) return 8;
	if (!CheckConcurrentFactoryRelease()) return 9;
	if (!CheckProviderOwnerRelease()) return 10;
	std::cout << "Network catalog selection, ownership and concurrent release checks passed\n";
	return 0;
}
