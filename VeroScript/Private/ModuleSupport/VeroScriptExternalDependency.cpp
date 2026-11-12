// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptExternalDependency.h"
#include "VeroScriptModuleSupport.h"
#include <algorithm>
#include <set>

namespace VeroScript
{
	bool ValidateExternalDependencies(const std::vector<FExternalDependency>& D, std::string& Error)
	{
		if (D.size() > 4096)
		{
			Error = "external dependency count exceeds limit";
			return false;
		}
		std::set<std::string> Seen;
		std::size_t Bytes = 0;
		for (const auto& V : D)
		{
			for (const auto* S : {&V.Id, &V.Version, &V.Abi, &V.BuildIdentity, &V.ContentIdentity})
			{
				if (S->size() > 8u * 1024u * 1024u - Bytes)
				{
					Error = "external dependency bytes exceed limit";
					return false;
				}
				Bytes += S->size();
			}
			if (!ModuleSupport::Text(V.Id) || !ModuleSupport::Text(V.Version) ||
				!ModuleSupport::Text(V.Abi, true) || !ModuleSupport::Text(V.BuildIdentity, true) ||
				!ModuleSupport::Text(V.ContentIdentity, true) || !Seen.insert(V.Id).second)
			{
				Error = "invalid or duplicate external dependency: " + V.Id;
				return false;
			}
		}
		Error.clear();
		return true;
	}
	bool MergeExternalDependencies(std::vector<FExternalDependency>& Into,
		const std::vector<FExternalDependency>& D, std::string& Error)
	{
		if (!ValidateExternalDependencies(Into, Error) || !ValidateExternalDependencies(D, Error)) return false;
		auto Next = Into;
		for (const auto& V : D)
		{
			const auto I = std::find_if(Next.begin(), Next.end(),
				[&](const auto& E)
				{
					return E.Id == V.Id;
				});
			if (I == Next.end())
			{
				Next.push_back(V);
			}

			else if (*I != V)
			{
				Error = "conflicting external dependency: " + V.Id;
				return false;
			}
		}
		if (!ValidateExternalDependencies(Next, Error)) return false;
		std::sort(Next.begin(), Next.end(),
			[](const auto& A, const auto& B)
			{
				return A.Id < B.Id;
			});
		Into = std::move(Next);
		return true;
	}
	bool CheckExternalDependencies(const std::vector<FExternalDependency>& Required,
		const std::vector<FExternalDependency>& Available, std::string& Error)
	{
		if (!ValidateExternalDependencies(Required, Error) || !ValidateExternalDependencies(Available, Error)) return false;
		for (const auto& R : Required)
		{
			const auto I = std::find_if(Available.begin(), Available.end(),
				[&](const auto& D)
				{
					return D.Id == R.Id;
				});
			if (I == Available.end())
			{
				Error = "missing external dependency: " + R.Id;
				return false;
			}
			if (*I != R)
			{
				Error = "external dependency identity mismatch: " + R.Id;
				return false;
			}
		}
		Error.clear();
		return true;
	}
}
