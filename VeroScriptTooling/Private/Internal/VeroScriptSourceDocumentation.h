// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	// Returned views borrow Source; keep it alive while reading documentation.
	std::vector<std::string_view> SplitSourceLines(std::string_view Source);

	// Render supported source documentation tags for editor presentation.
	std::string FormatDocumentationTags(std::string_view Documentation);

	std::string ExtractLeadingDocumentation(
		const std::vector<std::string_view>& Lines,
		std::uint32_t OneBasedDefinitionLine);

	// Read a declaration's adjacent leading //, ///, or /* */ comment.
	std::string ExtractLeadingDocumentation(
		std::string_view Source,
		std::uint32_t OneBasedDefinitionLine);
}
