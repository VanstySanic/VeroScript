// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Hover information at a caret (identifier → locals / document symbols).
	 * Contents is a ready-to-display summary; Definitions carry structured hits.
	 */
	class VeroScriptHover
	{
	public:
		// Render existing structured hits for Markdown clients without another query.
		// Contents remains the plain-text summary used by native and CLI callers.
		static std::string FormatMarkdown(const FHoverResult& Result);

		static FHoverResult HoverAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FHoverOptions& Options = {});

		static FHoverResult HoverAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FHoverOptions& Options = {});
	};
}
