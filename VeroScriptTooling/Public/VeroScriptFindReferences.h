// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Find references for the identifier under the caret (lexical; overlays scanned too).
	 */
	class VeroScriptFindReferences
	{
	public:
		static FReferencesResult FindAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FReferencesOptions& Options = {});

		static FReferencesResult FindAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FReferencesOptions& Options = {});
	};
}
