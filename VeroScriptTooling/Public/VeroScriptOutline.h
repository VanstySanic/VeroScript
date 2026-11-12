// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>
#include <vector>

namespace VeroScript::Tooling
{
	/**
	 * Document outline (symbol tree) for IDE navigation panes.
	 * Parse-only: does not require a successful compile.
	 */
	class VeroScriptOutline
	{
	public:
		static FOutlineResult CollectFromSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FOutlineOptions& Options = {});

		static FOutlineResult CollectFromFile(
			std::string_view FilePath,
			const FOutlineOptions& Options = {});
	};
}
