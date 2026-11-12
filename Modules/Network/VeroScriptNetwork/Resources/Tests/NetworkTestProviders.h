// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetworkProviderHost.h"
#include "VeroScriptNativeExtensionLoader.h"
#include "NetworkTestPaths.h"
#include "VeroScriptBoostModule.h"
#if defined(VEROSCRIPT_NETWORK_TEST_CURL_PROVIDER)
#include "VeroScriptCurlModule.h"
#endif

namespace VeroScript::Network::Test
{
	inline std::shared_ptr<FBackendCatalog> AddProviders(FNativeExtensionRegistry& Registry, std::string& Error, bool Dynamic = false)
	{
		auto Catalog = std::make_shared<FBackendCatalog>();
		FNativeExtensionLoadOptions Options;
		Options.ContentIdentity = Boost::GetImplementationIdentity();
		std::shared_ptr<const FLoadedNativeExtension> BoostModule;
		#if defined(VEROSCRIPT_TEST_BOOST_MODULE)
		if (Dynamic)
		{
			BoostModule = LoadNativeExtension(PathToUtf8(std::filesystem::absolute(std::filesystem::u8path(VEROSCRIPT_TEST_BOOST_MODULE)), true), Error);
		}
		else
		#else
		if (Dynamic)
		{
			Error = "Dynamic Boost provider is unavailable in this build";
			return {};
		}
		#endif
			BoostModule = QueryNativeExtension(Boost::QueryModule, std::make_shared<int>(0), Error, Options);
		FNativeExtensionRegistrationOptions Registration;
		Registration.Origin.ProviderId = "boost";
		if (!BoostModule || !Registry.Add(BoostModule, Registration, Error)) return {};
		#if defined(VEROSCRIPT_NETWORK_TEST_CURL_PROVIDER)
		Options.ContentIdentity = Curl::GetImplementationIdentity();
		std::shared_ptr<const FLoadedNativeExtension> CurlModule;
		#if defined(VEROSCRIPT_TEST_CURL_MODULE)
		if (Dynamic)
		{
			CurlModule = LoadNativeExtension(PathToUtf8(std::filesystem::absolute(std::filesystem::u8path(VEROSCRIPT_TEST_CURL_MODULE)), true), Error);
		}
		else
		#else
		if (Dynamic)
		{
			Error = "Dynamic curl provider is unavailable in this build";
			return {};
		}
		#endif
			CurlModule = QueryNativeExtension(Curl::QueryModule, std::make_shared<int>(0), Error, Options);
		Registration.Origin.ProviderId = "curl";
		if (!CurlModule || !Registry.Add(CurlModule, Registration, Error)) return {};
		#endif
		Catalog->Update(Registry.GetProviders());
		return Catalog;
	}
}
