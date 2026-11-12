// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

namespace VeroScript
{
	// Already resolved identities. Core compares them exactly; version solving
	// and registry/installation history belong to the package manager.
	struct FExternalDependency
	{
		bool operator==(const FExternalDependency& Other) const noexcept
		{
			return Id == Other.Id && Version == Other.Version && Abi == Other.Abi &&
				BuildIdentity == Other.BuildIdentity && ContentIdentity == Other.ContentIdentity;
		}
		bool operator!=(const FExternalDependency& Other) const noexcept
		{
			return !(*this == Other);
		}

		std::string Id, Version, Abi, BuildIdentity, ContentIdentity;
	};

	struct FBindingOrigin
	{
		bool operator==(const FBindingOrigin& Other) const noexcept
		{
			return ProviderId == Other.ProviderId && ModulePath == Other.ModulePath &&
				bExported == Other.bExported;
		}
		bool operator!=(const FBindingOrigin& Other) const noexcept
		{
			return !(*this == Other);
		}

		std::string ProviderId;
		// Optional ordinary module visibility, including export import.
		std::string ModulePath;
		bool bExported = true;
	};

	bool ValidateExternalDependencies(const std::vector<FExternalDependency>& Dependencies, std::string& Error);
	bool MergeExternalDependencies(std::vector<FExternalDependency>& Into,
		const std::vector<FExternalDependency>& Dependencies, std::string& Error);
	bool CheckExternalDependencies(const std::vector<FExternalDependency>& Required,
		const std::vector<FExternalDependency>& Available, std::string& Error);
}
