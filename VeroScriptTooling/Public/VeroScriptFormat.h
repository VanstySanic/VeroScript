// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Token-based source formatter (stable whitespace / indent; preserves string lexemes).
	 * Access labels (`public:` / `protected:` / `private:`) outdent one level vs members.
	 */
	class VeroScriptFormat
	{
	public:
		static FFormatResult FormatSource(
			std::string_view Source,
			const FFormatOptions& Options = {});

		/**
		 * Format a source selection. The returned replacement always covers whole
		 * lines, preserving the surrounding document and the enclosing indentation
		 * context. This is the operation used by LSP rangeFormatting.
		 */
		static FRangeFormatResult FormatRangeSource(
			std::string_view Source,
			FTextRange Range,
			const FFormatOptions& Options = {});

		static FFormatResult FormatFile(
			std::string_view FilePath,
			const FFormatOptions& Options = {});
	};
}
