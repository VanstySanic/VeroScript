// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageSources.h"
#include <algorithm>
namespace VeroScript::Packages::Detail
{
		void StableUrl(const std::string& S)
		{
			std::string Lower = S;
			std::transform(Lower.begin(), Lower.end(), Lower.begin(),
				[](unsigned char C)
				{
					return char(std::tolower(C));
				});
			if (Lower.find('@') != std::string::npos || Lower.find('#') != std::string::npos)
			{
				Fail("source-url", "persistent URL must not contain credentials or fragments");
			}
			auto Q = Lower.find('?');
			if (Q != std::string::npos)
			{
				auto Query = Lower.substr(Q + 1);
				std::string Decoded;
				for (std::size_t I = 0; I < Query.size(); ++I)
				{
					if (Query[I] == '%' && I + 2 < Query.size())
					{
						auto Digit = [](char C)
						{
							return C >= '0' && C <= '9' ? C - '0' : C >= 'a' && C <= 'f' ? C - 'a' + 10 : -1;
						};
						auto A = Digit(Query[I + 1]), B = Digit(Query[I + 2]);
						if (A < 0 || B < 0)
						{
							Fail("source-url", "invalid percent escape in persistent URL");
						}
						Decoded += char(A * 16 + B);
						I += 2;
					}
					else
					{
						Decoded += Query[I];
					}
				}
				Query = Decoded;
				std::transform(Query.begin(), Query.end(), Query.begin(),
					[](unsigned char C)
					{
						return char(std::tolower(C));
					});
				for (const auto* K : {"token=", "sig=", "signature=", "x-amz-", "x-goog-",
						 "credential=", "password=", "access_key=", "auth="})
				{
					if (Query.find(K) != std::string::npos)
					{
						Fail("transient-url", "download signed URLs as local archives before installing");
					}
				}
			}
		}
	J RegistrySettings(const J& Settings, const FClientOptions& Options)
	{
		auto R = Settings.value("_lockedSources", J::object());
		auto Explicit = Settings.value("packageManager", J::object()).value("registries", J::object());
		for (auto I = Explicit.begin(); I != Explicit.end(); ++I)
		{
			R[I.key()] = I.value();
		}
		if (!R.contains("official") && !Options.OfficialApi.empty())
		{
			R["official"] = {{"id", "veroscript.official"}, {"api", Options.OfficialApi}};
		}
		std::map<std::string, std::string> Origins;
		for (auto I = R.begin(); I != R.end(); ++I)
		{
			std::string Identity = I.value().at("id"), Api = I.value().at("api");
			while (!Api.empty() && Api.back() == '/')
			{
				Api.pop_back();
			}
			if (Identity.empty() || Identity.size() > 128 || Identity.rfind("local.", 0) == 0 ||
				std::any_of(Identity.begin(), Identity.end(),
					[](unsigned char C)
					{
						return C < 33;
					}))
			{
				Fail("source-identity", "invalid or reserved logical registry ID");
			}
			auto Prior = Origins.emplace(Identity, Api);
			if (!Prior.second && Prior.first->second != Api)
			{
				Fail("source-identity", "one logical registry ID has multiple origin APIs", Identity);
			}
			StableUrl(I.value().at("api"));
			if (I.value().at("api").get<std::string>().find('?') != std::string::npos)
			{
				Fail("source-url", "registry API base cannot contain a query");
			}
			if (I.value().contains("mirror"))
			{
				StableUrl(I.value()["mirror"]);
				if (I.value()["mirror"].get<std::string>().find('?') != std::string::npos)
				{
					Fail("source-url", "mirror API base cannot contain a query");
				}
			}
		}
		return R;
	}
}
