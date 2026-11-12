// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VeroScript
{
// Owned compile-time interface. Replacement text is expanded by the caller.
struct FPublicMacro
{
	std::string Name;
	bool bFunctionLike = false;
	bool bVariadic = false;
	std::vector<std::string> Parameters;
	std::string Body;
	std::string OriginModule;
	std::uint64_t Revision = 0;
	std::uint64_t DefinitionOrdinal = 0;
	std::string SourceName;
	std::uint32_t Line = 0;
	std::uint32_t Column = 0;
	std::uint32_t DirectiveStartLine = 0;
	std::uint32_t DirectiveEndLine = 0;
};

constexpr std::size_t MaxPublicMacroCount = 65536;
constexpr std::size_t MaxPublicMacroParameters = 1024;
constexpr std::size_t MaxPublicMacroBodyBytes = 1024 * 1024;
constexpr std::size_t MaxPublicMacroTableBytes = 16 * 1024 * 1024;
bool ValidatePublicMacros(const std::vector<FPublicMacro> &Macros, std::string &OutError);
std::size_t PublicMacroEncodedSize(const FPublicMacro &Macro);
bool SamePublicMacroDefinition(const FPublicMacro &Left, const FPublicMacro &Right);
} // namespace VeroScript
