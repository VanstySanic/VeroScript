// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	class IResourceProvider;

	struct FResourceRequest
	{
		std::string ProviderId, ModuleId, Group, Path;
	};
	struct FResourceWork
	{
		std::function<void()> CheckCancelled;
		std::uint64_t MaxBytes = 64 * 1024 * 1024;
	};
	struct FResourceData
	{
		// The lease outlives all provider-owned storage, even on failure paths.
		std::shared_ptr<const void> Owner;
		std::vector<std::uint8_t> Bytes;
		std::string ContentIdentity, PhysicalPath;
		bool bExists = false;
		// Set only by a trusted provider after applying the matching artifact policy.
		std::string ArtifactTrustIdentity;
	};
	struct FResourceProviderBinding
	{
		std::shared_ptr<const void> Owner;
		std::shared_ptr<const IResourceProvider> Provider;
		std::string Id, OwnerId, ContractIdentity;
		// Resource-only/data packages need no importable source module.
		std::vector<std::string> ModuleIds;
	};

	class IResourceProvider
	{
	public:
		virtual ~IResourceProvider() = default;
		// false is a failure, not a missing resource. A successful missing result
		// has bExists=false and no bytes. Implementations must be concurrency safe
		// and honor Work before allocating/reading; Core checks the result as well.
		virtual bool Read(const FResourceRequest&, const FResourceWork&, FResourceData&, std::string& Error) const = 0;
	};

	bool ValidateResourceRequest(const FResourceRequest&, std::string& Error);
	// An opaque dependency identity; never normalize it as a filesystem path.
	std::string MakeResourceDependencyId(const FResourceRequest&, const std::string& RequestingModule);
	bool ParseResourceDependencyId(const std::string&, FResourceRequest&, std::string& RequestingModule);
	bool IsResourceDependencyId(const std::string&) noexcept;
	std::string ResourceDependencyContent(const FResourceData&);
}
