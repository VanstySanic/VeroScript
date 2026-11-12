// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplication.h"
#include "ApplicationInternal.h"
#include <algorithm>

namespace VeroScript::Application
{
	bool ConfigureApplicationServices(std::string_view Text, FApplicationServices& Services, FDiagnostic& Error)
	{
		using namespace Detail;
		try
		{
			const auto O = Parse(Text, {});
			Keys(O, {"cacheRoot", "stdlibSearchPaths", "cryptoLibrary", "keyring", "requireSignature", "minimumSignatures", "requiredKeys", "policyIdentity"}, "applicationOptions");
			auto Candidate = Services;
			if (O.contains("cacheRoot"))
			{
				Candidate.CacheRoot = O.at("cacheRoot").get<std::string>();
				Physical(PathUtil::FromUtf8(Candidate.CacheRoot), true);
			}
			if (O.contains("stdlibSearchPaths"))
			{
				const auto Roots = O.at("stdlibSearchPaths").get<std::vector<std::string>>();
				for (const auto& Root : Roots)
				{
					Physical(PathUtil::FromUtf8(Root));
					if (!fs::is_directory(PathUtil::FromUtf8(Root)))
					{
						Fail("stdlib-path", "standard library path is not a directory");
					}
					const auto Path = PathUtil::ToUtf8(PathUtil::FromUtf8(Root).lexically_normal());
					if (std::find(Candidate.StandardLibraryPaths.begin(), Candidate.StandardLibraryPaths.end(), Path) == Candidate.StandardLibraryPaths.end())
					{
						Candidate.StandardLibraryPaths.push_back(Path);
					}
				}
			}
			const bool HasProtection = O.contains("cryptoLibrary") || O.contains("keyring") || O.contains("requireSignature") ||
			    O.contains("minimumSignatures") || O.contains("requiredKeys") || O.contains("policyIdentity");
			if (HasProtection)
			{
				if (!O.contains("cryptoLibrary") || !O.contains("keyring"))
				{
					Fail("protection-options", "protection options require a crypto library and trusted keyring");
				}
				FTrustPolicy Policy;
				Policy.RequireSignature = O.value("requireSignature", false);
				if (O.contains("minimumSignatures"))
				{
					if (!O.at("minimumSignatures").is_number_unsigned() || O.at("minimumSignatures").get<std::uint64_t>() < 1 || O.at("minimumSignatures").get<std::uint64_t>() > 16)
					{
						Fail("signature-threshold", "signature threshold must be between one and sixteen");
					}
					Policy.MinimumSignatures = O.at("minimumSignatures").get<std::size_t>();
				}
				Policy.Identity = O.value("policyIdentity", "");
				const auto Required = O.value("requiredKeys", std::vector<std::string>{});
				for (const auto& Key : Required)
				{
					const auto At = Key.find('@');
					if (At == std::string::npos || !At || At + 1 == Key.size() || Key.find('@', At + 1) != std::string::npos || !Policy.RequiredKeys.insert(Key).second)
					{
						Fail("signature-key", "required key must be a unique id@version reference");
					}
				}
				auto Crypto = LoadSodiumCrypto(O.at("cryptoLibrary").get<std::string>(), Error);
				if (!Crypto) return false;
				auto Keys = LoadProtectionKeys(O.at("keyring").get<std::string>(), Error);
				if (!Keys) return false;
				Candidate.Protection = std::make_shared<FProtection>(std::move(Crypto), std::move(Keys), std::move(Policy));
			}
			Services = std::move(Candidate);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			if (Error.Code == "json")
			{
				Error.Message = "invalid application service JSON";
			}
			return false;
		}
		catch (const std::exception&)
		{
			Error = {"application-options", "invalid application service options", {}, {}};
			return false;
		}
	}
}
