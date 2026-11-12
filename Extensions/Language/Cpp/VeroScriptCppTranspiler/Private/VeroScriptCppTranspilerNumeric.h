// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace VeroScript::CppTranspiler::Detail
{
	// Select the C++ literal's host integer type before using VeroScript suffixes.
	bool NormalizeNumberLiteral(std::string_view Text, std::string& Output, std::string& Error);
}
