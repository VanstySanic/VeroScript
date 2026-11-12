// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Protocol-agnostic document analysis for IDE / tooling frontends.
	 * Depends on VeroScript only; does not pull UI or LSP/DAP protocols.
	 */
	class VeroScriptAnalyzer
	{
	public:
		static FAnalyzeResult AnalyzeSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FAnalyzeOptions& Options = {});

		static FAnalyzeResult AnalyzeFile(
			std::string_view FilePath,
			const FAnalyzeOptions& Options = {});
	};
}
