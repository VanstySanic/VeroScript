// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>

namespace VeroScript::CppTools
{
	struct FOutputFile
	{
		std::string Path;
		std::string Contents;
	};

	bool PathsReferToSameFile(
		const std::string& Left,
		const std::string& Right);

	// Pure name ordering for generated-file collision groups. ASCII names compare
	// without case on all platforms; Windows also uses ordinal Unicode comparison.
	// No filesystem state participates in the generated names.
	struct FOutputFileNameLess
	{
		bool operator()(std::string_view Left, std::string_view Right) const;
	};

	// Append-only identity index for one file batch, including hard-link aliases.
	// Indexed paths must remain unchanged until the batch's checks are complete.
	class FFilePathSet
	{
	public:
		FFilePathSet();
		~FFilePathSet();
		FFilePathSet(const FFilePathSet&) = delete;
		FFilePathSet& operator=(const FFilePathSet&) = delete;
		// Returns true only when this path's identity is newly inserted.
		bool Insert(std::string_view Path);
		bool Contains(std::string_view Path) const;
	private:
		struct FState;
		std::unique_ptr<FState> State;
	};

	/**
	 * Writes a set of generated files as one transaction. All contents are staged
	 * before existing targets are moved aside. A failed commit restores the prior
	 * targets and removes newly created targets on a best-effort basis.
	 * Paths must be nonempty and contain no NUL characters. The complete batch
	 * is validated before creating staging directories or files.
	 */
	bool WriteOutputFilesAtomically(
		const std::vector<FOutputFile>& Files,
		std::string& OutError);
}
