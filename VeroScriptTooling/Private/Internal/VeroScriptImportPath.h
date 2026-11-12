// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "Parser/VeroScriptParserDetail.h"

namespace VeroScript::Tooling::Detail
{
	inline bool DecodeImportPathLiteral(std::string_view Literal, bool CompilerExtension, std::string& Path)
	{
		// Compiler imports use the core prefix grammar: ordinary quotes and only
		// escaped quotes/backslashes. Ordinary imports also accept raw strings.
		if (CompilerExtension)
		{
			if (Literal.size() < 2 || Literal.front() != '"' || Literal.back() != '"') return false;
			for (std::size_t I = 1; I + 1 < Literal.size(); ++I)
			{
				if (Literal[I] == '\n' || Literal[I] == '\r') return false;
				if (Literal[I] == '\\' &&
					(++I + 1 >= Literal.size() || (Literal[I] != '\\' && Literal[I] != '"')))
				{
					return false;
				}
			}
		}
		std::string Error;
		return ParserDetail::DecodeModuleImportPath(Literal, Path, Error);
	}
}
