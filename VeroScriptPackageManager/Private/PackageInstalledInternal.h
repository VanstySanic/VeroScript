// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "PackageInternal.h"

namespace VeroScript::Packages
{
	struct FInstalledEnvironment::FImpl
	{
		// Release provider callbacks and package values before filesystem leases.
		std::vector<std::shared_ptr<const void>> Owners;
		FEnvironmentLayout Layout;
		std::vector<FInstalledPackage> Packages;
		std::uint64_t Generation = 0;
		std::string Fingerprint;
		Detail::J Project, Lock, Profile, Composition;
	};
} // namespace VeroScript::Packages
namespace VeroScript::Packages::Detail
{
	fs::path ManagementRoot(const FEnvironmentLayout&);
	std::string ProfileKey(const FEnvironmentLayout&);
	J ReadRecord(const fs::path&, const FWork&);
	J NewProject(const std::string&);
	J LoadRequirements(const FEnvironmentLayout&, const FWork&, bool AllowMissing);
	J LoadState(const FEnvironmentLayout&, const FWork&);
	std::set<fs::path> TransactionDestinations(const FEnvironmentLayout&);
	void ValidateTransactionJournal(const FEnvironmentLayout&, const J&, const FWork&);
	// Call only after journal validation. Rejects foreign current bytes and reports
	// whether the selected old/new state is already present; never changes files.
	bool TransactionRecordMatches(const FEnvironmentLayout&, const J&, bool New, const FWork&);
	void ConfigureStorage(FEnvironmentLayout&, const FEnvironmentInput&, const J& Project, const J& State);
	FGraph GraphFromLock(const J&, const J& Project, const FWork&, bool ActivateGraph = true);
	std::shared_ptr<FInstalledEnvironment::FImpl> ReadInstalled(const FEnvironmentLayout&, FWork&);
	std::shared_ptr<FInstalledEnvironment::FImpl> BuildInstalled(const FEnvironmentLayout&, FWork&, const J&,
																 const J&, const J&, const std::string&);
	std::string StoreOwner(const FEnvironmentLayout&, const J& State, const FWork&);
	void ValidateLibraries(const FGraph&);
	J PrepareLoaders(const FGraph&, const std::string& Name, const fs::path& Generated, FWork&);
	void VerifyLoaders(const fs::path&, const J&, const FWork&);
} // namespace VeroScript::Packages::Detail
