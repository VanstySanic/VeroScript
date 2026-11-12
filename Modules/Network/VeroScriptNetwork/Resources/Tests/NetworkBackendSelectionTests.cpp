// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkTestProviders.h"
#include "VeroScriptNetworkBackend.h"
#include <iostream>

int main()
{
	using namespace VeroScript;
	namespace N = Network;
	std::string Error;
	auto Empty = std::make_shared<N::FBackendCatalog>();
	if (Empty->Resolve("network", "boost", N::Backend::ContractId, N::Backend::ContractVersion, Error) || Error.empty()) return 1;
	auto Registry = std::make_unique<FNativeExtensionRegistry>();
	auto Catalog = N::Test::AddProviders(*Registry, Error);
	if (!Catalog)
	{
		std::cerr << Error << '\n';
		return 2;
	}
	auto Service = Catalog->Resolve("network", "", N::Backend::ContractId, N::Backend::ContractVersion, Error);
	auto Again = Catalog->Resolve("network", "boost", N::Backend::ContractId, N::Backend::ContractVersion, Error);
	if (!Service || Service.owner_before(Again) || Again.owner_before(Service)) return 3;
	if (Catalog->Resolve("network", "missing", N::Backend::ContractId, N::Backend::ContractVersion, Error) || Error.empty()) return 4;
	if (Catalog->Resolve("network", "boost", "wrong-contract", N::Backend::ContractVersion, Error) || Error.empty()) return 5;
	if (Catalog->Resolve("network", "boost", N::Backend::ContractId, "9.0.0", Error) || Error.empty()) return 6;
	N::FContextOptions Options;
	Options.Mode = N::EDriveMode::HostDriven;
	auto* Handle = static_cast<N::Backend::IService*>(Service.get());
	N::FContext Context(std::shared_ptr<N::Backend::IService>(Service, Handle), Options);
	if (std::string_view(Context.GetBackendId()) != "boost") return 7;
	Catalog->Update(Registry->GetProviders(), {}, {{"network", "missing"}});
	if (Catalog->Resolve("network", "", N::Backend::ContractId, N::Backend::ContractVersion, Error)) return 8;
	if (!Catalog->Resolve("network", "boost", N::Backend::ContractId, N::Backend::ContractVersion, Error)) return 9;
	std::weak_ptr<N::FBackendCatalog> CatalogProbe = Catalog;
	Catalog.reset();
	if (!CatalogProbe.expired()) return 10;
	Registry.reset();
	Service.reset();
	Again.reset();
	Context.Shutdown();
	std::cout << "Backend selection, cache and context ownership passed\n";
	return 0;
}
