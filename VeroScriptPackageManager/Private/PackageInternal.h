// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPackageManager.h"
#include "VeroScriptPath.h"
#include "json/json.hpp"
#include <filesystem>
#include <set>
#include <stdexcept>

namespace VeroScript::Packages::Detail
{
	using J = nlohmann::json;
	namespace fs = std::filesystem;
	inline fs::path U8Path(std::string_view S)
	{
		return PathUtil::FromUtf8(S);
	}

	// Receipts and journals store native path spellings, including Windows separators.
	inline std::string NativePathText(const fs::path& Path)
	{
		#if defined(__cpp_char8_t)
		const auto Bytes = Path.u8string();
		return std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
		#else
		return Path.u8string();
		#endif
	}

	struct FFailure : std::runtime_error
	{
		FFailure(std::string Code, std::string Message, std::string Field = {});

		FDiagnostic Diagnostic;
	};
	[[noreturn]] void Fail(const std::string& Code, const std::string& Message,
						   const std::string& Field = {});
	struct FWork
	{
		void Check() const;

		void ChargeMetadata(std::size_t Bytes) const;

		const FClientOptions& Options;
		std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
		std::uint64_t Steps = 0, Candidates = 0, Edges = 0;
		mutable std::uint64_t MetadataBytes = 0;
	};
	struct FInstallInput
	{
		std::string Name, Version, Location, Source;
		std::size_t Line = 0;
	};
	struct FInstallInputs
	{
		std::vector<FInstallInput> Items;
		std::map<fs::path, std::string> Files;
	};
	FInstallInputs ReadInstallInputs(const FRequest&, const FWork&);
	std::string IntersectInstallVersions(const std::string&, const std::string&);
	void VerifyInstallInputs(const FInstallInputs&, const FWork&);
	void LocateInstallFailure(FFailure&, const FInstallInput&);
	J Parse(std::string_view, const FLimits&);
	void Keys(const J&, std::initializer_list<const char*>, const std::string&);
	void String(const J&, const std::string&);
	void Strings(const J&, const std::string&);
	bool Name(std::string_view);
	bool Digest(std::string_view);
	std::string Hash(std::string_view);
	std::string HashFile(const fs::path&, const FWork&, std::uint64_t Limit);
	std::string Read(const fs::path&, std::uint64_t Limit, const FWork* = nullptr);
	void Write(const fs::path&, std::string_view);
	void AtomicWrite(const fs::path&, std::string_view);
	// Commit a staged regular file or directory on the same filesystem without
	// replacement. Directories require native atomic no-replace rename support.
	void MovePathNoReplace(const fs::path& From, const fs::path& To);
	fs::path Absolute(const std::string&, const std::string& Base);
	std::string FindProjectRoot(const fs::path& StartDirectory, const std::string& Boundary);
	void CheckPath(const std::string&, bool Directory = false);
	bool Under(const fs::path&, const fs::path&);
	fs::path Relative(const fs::path&, const fs::path&);
	std::string PathReference(const fs::path&, const fs::path&);
	void NoLinks(const fs::path&, bool MissingAllowed = false);
	std::string FileIdentity(const fs::path&);
	std::string Id(const J&);
	J TargetJson(const FTargetContext&);
	FTargetContext ReadTarget(const J&);
	void ValidateTarget(const J&, const std::string&);
	bool MatchesTarget(const J&, const FTargetContext&);
	bool Satisfies(const std::string&, const std::string&);
	int CompareVersions(const std::string&, const std::string&);
	void ValidateRequirements(const std::string&);
	void ValidateDependency(const J&, const std::string&, bool Distribution);
	J Manifest(const J&);
	bool NeedsInstallationProcessing(const J& Manifest);
	J Project(const J&);
	J Inventory(const fs::path&, const FWork&, bool Filter = false, const J& Spec = {}, bool RequirePackageManifest = true);
	std::string InventoryHash(const J&);
	void VerifyInventory(const fs::path&, const J&, const FWork&);
	void CopyInventory(const fs::path&, const fs::path&, const J&, const FWork&);
	bool IsPackageArchive(const fs::path&);
	void Extract(const fs::path&, const fs::path&, const FWork&);
	void SnapshotArchive(const fs::path&, const fs::path&, const FWork&, const std::string& Sha = {},
						 std::uint64_t Size = 0);
	void Pack(const fs::path&, const fs::path&, const J&, const FWork&);
	struct FTempDirectory
	{
		explicit FTempDirectory(const fs::path&);
		~FTempDirectory();

		fs::path Path, Parent;
	};

	struct FCandidate
	{
		J Spec, Source, Manifest, Artifact, Inventory;
		fs::path Content;
		std::string OwnerId;
		bool MetadataOnly = false;
	};

	struct FGraph
	{
		std::map<std::string, FCandidate> Nodes;
		std::map<std::string, std::set<std::string>> Edges, Usage;
		std::set<std::string> Active;
		std::vector<std::string> Order;
	};

	class FFileLock
	{
	public:
		FFileLock(const fs::path&, bool Exclusive, bool Create = true, bool Wait = true,
				  const FWork* = nullptr);
		~FFileLock();
		FFileLock(const FFileLock&) = delete;
		FFileLock& operator=(const FFileLock&) = delete;

	private:
		std::intptr_t Handle = -1;
	};
	class ICandidateRepository
	{
	public:
		virtual ~ICandidateRepository() = default;
		virtual std::vector<FCandidate> Candidates(const std::string&, const J&, const J&) = 0;
		virtual J Source(const J&) const = 0;
		virtual void PrepareCandidate(FCandidate&)
		{
		}
	};
	class FRepository : public ICandidateRepository
	{
	public:
		void PrepareCandidate(FCandidate& C) override
		{
			if (C.MetadataOnly)
			{
				Materialize(C);
			}
		}

		FRepository(FEnvironmentLayout, J Settings, const FClientOptions&, FWork&);
		std::vector<FCandidate> Candidates(const std::string&, const J&, const J& Previous);
		FCandidate Local(const std::string&, J Spec);
		void Materialize(FCandidate&);

		J RequestJson(const std::string& Alias, const std::string& Route);
		J Registries() const;
		J Source(const J&) const;

	private:
		fs::path Download(const std::string&, const std::string&, std::uint64_t, const std::string& = {});

		FEnvironmentLayout Layout;
		J Settings;
		const FClientOptions& Options;
		FWork& Work;
		std::shared_ptr<const IHttpTransport> Http;
		std::vector<std::unique_ptr<FTempDirectory>> Temps;
		std::map<std::string, std::vector<FCandidate>> Cache;

	};
	// InstalledOnly is reserved for repositories backed exclusively by installed snapshots.
	FGraph Solve(const J& Project, const J& Previous, ICandidateRepository&, const FEnvironmentLayout&,
				 FWork&, bool InstalledOnly = false);
	bool DependencyApplies(const J&, const J& Project, const std::string& Name, const FTargetContext&,
						   bool Development);
	void Activate(FGraph&, const J&, const FTargetContext&, bool Development);
	J LockJson(const FGraph&, const J&, const FEnvironmentLayout&, const J& Registries, bool Development);
	std::string RequirementsFingerprint(const J&, const FEnvironmentLayout&, const J&, bool,
										const J& Packages = J::array());
	std::shared_ptr<I18n::FI18nServices> DisplayServices(const std::vector<FInstalledPackage>&, const FWork&);
	std::shared_ptr<I18n::FI18nServices> ScopedDisplayServices(
		std::shared_ptr<const I18n::II18nResourceSnapshot>, std::set<std::string>);
	std::string Render(const FInstalledPackage&, const std::string&, const I18n::FI18nSnapshot&,
					   const FWork&);
	J PrepareInputs(const FCandidate&, const J&, const FClientOptions&, FWork&, J& Secrets,
					const I18n::FI18nSnapshot* Display = nullptr);
	void InstallActions(const FCandidate&, const fs::path&, const J&, FWork&);
	void RunFeatures(const FCandidate&, const std::string& Phase, const J&, const J&, const fs::path&,
					 const FEnvironmentLayout&, const I18n::FI18nSnapshot&, FWork&);
	void RunHooks(const FCandidate&, const std::string&, const FGraph&, const J&, const J&, const fs::path&,
				  const FEnvironmentLayout&, const I18n::FI18nSnapshot&, FWork&, bool PreflightOnly = false);
	void Recover(const FEnvironmentLayout&, const FWork&);
	using FManagementSnapshot = std::map<fs::path, std::string>;
	FManagementSnapshot CaptureManagementSnapshot(const FEnvironmentLayout&, const FWork&);
	void Commit(const FEnvironmentLayout&, const std::vector<std::pair<fs::path, std::string>>&,
				const FManagementSnapshot&, const FWork&);
	std::string EscapeUrl(const std::string&);
	std::string SafeUrl(const std::string&);
	J Publication(const FRequest&, const FEnvironmentLayout&, FWork&);
} // namespace VeroScript::Packages::Detail
