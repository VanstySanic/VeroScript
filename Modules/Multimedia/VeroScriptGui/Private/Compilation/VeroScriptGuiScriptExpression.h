// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiMarkup.h"
#include <unordered_set>

namespace VeroScript::Gui
{
	/** Qualify external-context members using the Core AST, preserving lexical locals and source text. */
	bool RewriteMarkupExpression(const FMarkupExpression& Expression, bool bExternal,
								 const std::unordered_set<std::string>& Members, std::string& Code, std::string& Error,
								 std::vector<FMarkupTextSpan>* CopiedSpans = nullptr);
} // namespace VeroScript::Gui
