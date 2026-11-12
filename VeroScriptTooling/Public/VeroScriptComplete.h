// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Identifier completion at a caret (locals + document symbols, prefix filtered).
	 */
	class VeroScriptComplete
	{
	public:
		static FCompletionResult CompleteAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FCompletionOptions& Options = {});

		static FCompletionResult CompleteAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FCompletionOptions& Options = {});
	};
}
