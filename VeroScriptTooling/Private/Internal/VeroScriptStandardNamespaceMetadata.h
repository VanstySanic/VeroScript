// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace VeroScript::Tooling::Detail
{
	/**
	 * Tooling-owned presentation metadata for the reserved `std` namespace tree.
	 * Runtime aliases remain authoritative for language resolution; this table adds
	 * stable documentation, module ownership, and representative members without
	 * creating a second language-level namespace model.
	 */
	struct FStandardNamespaceMetadata
	{
		// Normalized dotted spelling used by the parser/tooling (`std.chrono`).
		std::string_view QualifiedName;
		std::string_view ParentName;
		std::string_view Category;
		std::string_view Module;
		std::string_view BriefEnglish;
		std::string_view DetailEnglish;
		std::string_view BriefZhCN;
		std::string_view DetailZhCN;
		std::string_view RepresentativeMembers;
	};

	const FStandardNamespaceMetadata* FindStandardNamespaceMetadata(
		std::string_view QualifiedName);

	// Presentation spelling (`std::chrono`) for a normalized tooling name.
	std::string FormatNamespaceQualifiedName(std::string_view QualifiedName);

	// Normalize standard names inside source-facing types/signatures, leaving
	// literal text, numeric defaults, and ordinary member access untouched.
	std::string FormatStandardSourceSpelling(std::string_view Text);

	// Locale-aware namespace documentation; unsupported locales fall back to English.
	std::string FormatStandardNamespaceDocumentation(const FStandardNamespaceMetadata& Metadata);

	// Compact completion/detail text carrying both the full name and module owner.
	std::string FormatStandardNamespaceDetail(const FStandardNamespaceMetadata& Metadata);
}
