// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace VeroScript::PathUtil
{
	/**
	 * Convert the UTF-8 path convention used by the public VeroScript APIs to
	 * std::filesystem::path without passing through the Windows ANSI code page.
	 * The implementation supports both C++17 and C++20 char8_t libraries.
	 */
	std::filesystem::path FromUtf8(std::string_view Text);

	// Convert a native filesystem path to normalized UTF-8 with '/' separators.
	std::string ToUtf8(const std::filesystem::path& Path);

	// Lexically normalize a UTF-8 path while preserving an invalid path verbatim.
	std::string Normalize(std::string Path);

	// Build the platform identity key used for module/import deduplication.
	std::string IdentityKey(std::string Path);

	// Compare two UTF-8 paths using the platform module-identity rules.
	bool Equal(const std::string& Left, const std::string& Right);

	// Find the source module in Directory or a repository's immediate children, independent
	// of the outer folder name. Missing or ambiguous modules return an empty path.
	std::filesystem::path FindSourceModuleRoot(std::string_view Directory);

	// Unique sibling used for write-then-replace operations.
	std::filesystem::path MakeTemporarySiblingPath(
		const std::filesystem::path& FinalPath);

	/**
	 * Atomically replace FinalPath with an already-written sibling TempPath.
	 * Failure preserves an existing FinalPath and leaves Error populated.
	 */
	bool ReplaceFileAtomically(
		const std::filesystem::path& TempPath,
		const std::filesystem::path& FinalPath,
		std::error_code& Error);

	// Directory containing the current process executable, or empty if unknown.
	std::string ExecutableDirectory();
}
