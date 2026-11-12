// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"

namespace VeroScript::Http
{
	struct FRouteLimits
	{
		std::uint32_t MaxRoutes = 1024, MaxSegments = 256, MaxQueryParameters = 1024, MaxMethodBytes = 256;
		std::uint64_t MaxPatternBytes = 16384, MaxTotalPatternBytes = 1024 * 1024, MaxTargetBytes = 65536;
	};
	struct FRoute
	{
		std::uint64_t Id = 0;
		std::string Method, Pattern;
	};
	struct FRouteMatch
	{
		bool bFound = false;
		std::uint64_t Id = 0;
		std::string RawTarget, Path;
		std::vector<std::pair<std::string, std::string>> Parameters;
		std::vector<Network::FQueryParameter> Query;
	};
	namespace Detail
	{
		struct FRouteTable;
	}
	class FRouter;
	class FRouteSnapshot
	{
		friend class FRouter;

	public:
		FRouteSnapshot() = default;
		std::uint64_t Size() const noexcept;
		bool TryMatch(std::string_view Method, std::string_view Target, FRouteMatch& Out, Network::FError& Error) const;

	private:
		std::shared_ptr<const Detail::FRouteTable> Table;
	};
	// Copies share the mutable registration handle; snapshots remain immutable.
	class FRouter
	{
	public:
		explicit FRouter(FRouteLimits Limits = {});
		bool TryAdd(FRoute Route, Network::FError& Error);
		bool TryRemove(std::uint64_t Id, bool& Removed, Network::FError& Error);
		FRouteSnapshot Snapshot() const noexcept;

	private:
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};
} // namespace VeroScript::Http
