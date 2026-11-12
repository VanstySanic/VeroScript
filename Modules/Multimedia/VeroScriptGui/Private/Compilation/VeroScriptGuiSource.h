// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>
#include "VeroScriptCompileProcessing.h"

namespace VeroScript::Gui
{
	bool GuiHasTemplateParameters(const FAnnotationDeclarationView& Declaration);
	std::string GuiSourceType(std::string Name);
	std::string GuiQuote(std::string_view Text);
	std::string GuiScopeSuffix(const std::string& TypeName);
	std::string GuiScopedBody(const std::string& Source, const std::string& TypeName, bool bAllowInternalChildren);
	FCompileGenerationOperation GuiBodyOperation(const FCompileProcessingContext& Context,
												 const FAnnotationDeclarationView& Declaration, std::string Body);
	void GuiSubmitMembers(const FCompileProcessingContext& Context, FCompileProcessingOutput& Output,
						  const std::string& Owner, const std::string& Source, FAnnotationSourcePosition Location,
						  const std::vector<FSourceDocument::FMapping>& Mappings = {});
} // namespace VeroScript::Gui
