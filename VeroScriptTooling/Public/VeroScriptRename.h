// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

#include <string_view>

namespace VeroScript::Tooling
{
	/**
	 * Prepare workspace text edits to rename the identifier under the caret.
	 * FindReferences supplies lexical candidates; each hit is re-resolved with
	 * SymbolQuery so unrelated homonyms are not rewritten.
	 */
	class VeroScriptRename
	{
	public:
		/**
		 * Resolve and validate whether the identifier under the caret is renameable.
		 * This deliberately performs no reference search and creates no edits, so it
		 * is suitable for LSP textDocument/prepareRename.
		 */
		static FRenameResult CanRenameAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view FileLabel = "<source>",
			const FRenameOptions& Options = {});

		static FRenameResult PrepareAtSource(
			std::string_view Source,
			FTextPosition Position,
			std::string_view NewName,
			std::string_view FileLabel = "<source>",
			const FRenameOptions& Options = {});

		static FRenameResult PrepareAtFile(
			std::string_view FilePath,
			FTextPosition Position,
			std::string_view NewName,
			const FRenameOptions& Options = {});
	};
}
