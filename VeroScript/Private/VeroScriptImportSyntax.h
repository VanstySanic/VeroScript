// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>

namespace VeroScript
{
// The emitted-token and module-discovery paths share the import-head grammar.
// The AST parser remains responsible for the complete surrounding declaration.
class FModuleImportHeadState
{
public:
	bool ExpectsPath() const
	{
		return bImport;
	}
	bool IsReExport() const
	{
		return bExport;
	}
	bool Consume(std::string_view Token, bool bStringLiteral = false)
	{
		if (bStringLiteral && bImport)
		{
			bStart = true;
			bImport = bExport = false;
			return true;
		}
		const bool Top = Braces == 0 && Parens == 0 && Brackets == 0;
		if (Top && bStart && Token == "export" && !bExport && !bImport)
		{
			bExport = true;
			return false;
		}
		if (Top && bStart && Token == "import" && !bImport)
		{
			bImport = true;
			return false;
		}
		if (Token == "{")
		{
			++Braces;
		}

		else if (Token == "}")
		{
			--Braces;
		}

		else if (Token == "(")
		{
			++Parens;
		}

		else if (Token == ")")
		{
			--Parens;
		}

		else if (Token == "[")
		{
			++Brackets;
		}

		else if (Token == "]")
		{
			--Brackets;
		}

		bStart = (Token == "}" || Token == ";") && Braces == 0 && Parens == 0 && Brackets == 0;
		bImport = bExport = false;
		return false;
	}

private:
	int Braces = 0, Parens = 0, Brackets = 0;
	bool bStart = true, bExport = false, bImport = false;
};
} // namespace VeroScript
