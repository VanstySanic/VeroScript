// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParserDetail.h"

namespace VeroScript::ParserDetail
{
bool DecodeModuleImportPath(std::string_view Lexeme, std::string &OutValue, std::string &OutError,
							std::uint32_t *OutErrorOffset)
{
	if (!DecodeStringLiteral(Lexeme, OutValue, OutError, OutErrorOffset))
	{
		return false;
	}

	if (OutValue.empty() || OutValue.find('\0') != std::string::npos)
	{
		OutError = "import module path must be nonempty and contain no NUL";
		return false;
	}
	return true;
}
} // namespace VeroScript::ParserDetail
