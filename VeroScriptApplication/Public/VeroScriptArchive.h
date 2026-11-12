// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArtifact.h"

#include <map>

namespace VeroScript::Application
{
	struct FDiagnostic
	{
		std::string Code, Message, Path, Stage;
	};
	struct FLimits
	{
		std::uint64_t MaxArchiveBytes = 1ull << 30;
		std::uint64_t MaxFileBytes = 640ull << 20;
		std::uint64_t MaxExpandedBytes = 4ull << 30;
		std::uint64_t MaxMetadataBytes = 4ull << 20;
		std::size_t MaxFiles = 100000, MaxJsonDepth = 64;
		std::uint64_t MaxExpansionRatio = 200;
		std::function<void()> CheckCancelled;
	};
	struct FContentFile
	{
		std::string Path;
		std::vector<std::uint8_t> Bytes;
		bool Executable = false;
	};
	struct FContentEntry
	{
		std::string Path;
		std::uint64_t Bytes = 0;
		bool Executable = false;
	};
	bool ValidateContentPath(std::string_view Path, std::string& Error);
	std::string ContentSha256(const std::vector<std::uint8_t>& Bytes);
	std::string ContentSha256(std::string_view Bytes);
	bool FileSha256(const std::string& AbsolutePath, std::string& Sha256,
		FDiagnostic&, const FLimits& = {});
	bool CheckPhysicalPath(const std::string& AbsolutePath, bool MissingAllowed, FDiagnostic&);

	class FArchive final
	{
	public:
		struct FImpl;
		static std::shared_ptr<const FArchive> Open(const std::string& AbsolutePath,
			FDiagnostic&, const FLimits& = {});
		// Retains immutable owned bytes; caller mutations cannot change the view.
		static std::shared_ptr<const FArchive> Open(std::vector<std::uint8_t> Bytes,
			FDiagnostic&, const FLimits& = {});
		~FArchive();
		const std::vector<FContentEntry>& GetEntries() const noexcept;
		const std::string& GetIdentity() const noexcept;
		const std::vector<std::uint8_t>* GetMemoryBytes() const noexcept;
		bool Read(const std::string& Path, std::vector<std::uint8_t>&,
			FDiagnostic&, const FArtifactWork& = {}) const;
	private:
		static std::shared_ptr<const FArchive> Initialize(std::shared_ptr<FImpl>,
			const std::string& Name, FDiagnostic&, const FLimits&);
		explicit FArchive(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};
	bool WriteArchive(const std::vector<FContentFile>&, const std::string& NewAbsolutePath,
		FDiagnostic&, const FLimits& = {});
	bool WriteArchive(const std::vector<FContentFile>&, std::vector<std::uint8_t>&,
		FDiagnostic&, const FLimits& = {});
	bool ReadContentTree(const std::string& AbsoluteRoot, std::vector<FContentFile>&,
		FDiagnostic&, const FLimits& = {});
	bool WriteContentTree(const std::vector<FContentFile>&, const std::string& NewAbsoluteRoot,
		FDiagnostic&, const FLimits& = {});
}
