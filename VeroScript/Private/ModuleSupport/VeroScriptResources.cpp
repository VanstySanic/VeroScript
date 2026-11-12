// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptResources.h"
#include "VeroScriptModuleSupport.h"
#include <limits>

namespace VeroScript
{
	namespace
	{
		constexpr const char* Prefix = "veroscript-resource:";
	}
	bool ValidateResourceRequest(const FResourceRequest& R, std::string& Error)
	{
		if (!ModuleSupport::Text(R.ProviderId) || !ModuleSupport::Text(R.ModuleId) ||
			!ModuleSupport::Text(R.Group) || !ModuleSupport::Relative(R.Path))
		{
			Error = "invalid resource identity or relative path";
			return false;
		}
		Error.clear();
		return true;
	}
	bool IsResourceDependencyId(const std::string& S) noexcept
	{
		return S.compare(0, 20, Prefix) == 0;
	}
	std::string MakeResourceDependencyId(const FResourceRequest& R, const std::string& From)
	{
		std::string S = Prefix;
		for (const auto* V : {&From, &R.ProviderId, &R.ModuleId, &R.Group, &R.Path})
		{
			ModuleSupport::Append(S, *V);
		}

		return S;
	}
	bool ParseResourceDependencyId(const std::string& S, FResourceRequest& Out, std::string& From)
	{
		if (!IsResourceDependencyId(S)) return false;
		std::size_t At = 20;
		FResourceRequest R;
		std::string Origin;
		for (auto* V : {&Origin, &R.ProviderId, &R.ModuleId, &R.Group, &R.Path})
		{
			std::size_t N = 0, Digits = 0;
			while (At < S.size() && S[At] >= '0' && S[At] <= '9')
			{
				if (++Digits > 7 || N > (1024u * 1024u - static_cast<unsigned>(S[At] - '0')) / 10) return false;
				N = N * 10 + static_cast<unsigned>(S[At++] - '0');
			}
			if (!Digits || At == S.size() || S[At++] != ':' || N > S.size() - At) return false;
			*V = S.substr(At, N);
			At += N;
		}
		std::string Error;
		if (At != S.size() || !ModuleSupport::Text(Origin) || !ValidateResourceRequest(R, Error) ||
			MakeResourceDependencyId(R, Origin) != S)
		{
			return false;
		}

		Out = std::move(R);
		From = std::move(Origin);
		return true;
	}
	std::string ResourceDependencyContent(const FResourceData& R)
	{
		std::string S;
		ModuleSupport::Append(S, R.ContentIdentity);
		ModuleSupport::Append(S, R.PhysicalPath);
		if (!R.Bytes.empty())
		{
			S.append(reinterpret_cast<const char*>(R.Bytes.data()), R.Bytes.size());
		}

		return S;
	}
}
