// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Position-based navigation queries (go-to-definition data).
	 * Resolves identifier tokens against enclosing locals/parameters first,
	 * then entry-module declarations by simple name.
	 */
	class VeroScriptNavigation
	{
	public:
		static FNavigationResult QueryAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FNavigationOptions& Options = {});

		static FNavigationResult QueryAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FNavigationOptions& Options = {});
	};
}
