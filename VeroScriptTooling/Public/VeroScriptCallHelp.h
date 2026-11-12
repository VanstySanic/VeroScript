// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Call-site signature help (active parameter + signature labels).
	 */
	class VeroScriptCallHelp
	{
	public:
		static FSignatureHelpResult HelpAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FSignatureHelpOptions& Options = {});

		static FSignatureHelpResult HelpAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			const FSignatureHelpOptions& Options = {});
	};
}
