// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Same-file reference highlights for the identifier under the caret.
	 * Reuses FindReferences / internal References and filters to the current file.
	 */
	class VeroScriptDocumentHighlight
	{
	public:
		static FDocumentHighlightResult HighlightsAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FDocumentHighlightOptions& Options = {});

		static FDocumentHighlightResult HighlightsAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FDocumentHighlightOptions& Options = {});
	};
}
