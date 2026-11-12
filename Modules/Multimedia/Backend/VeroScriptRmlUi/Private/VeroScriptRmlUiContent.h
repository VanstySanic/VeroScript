// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRmlUi.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/FontEngineInterface.h>

namespace VeroScript::RmlUi
{
	Rml::FontEngineInterface* GetFontEngine();
	bool InitializeDefaultFont(std::string& Error);
	void ReleaseFontSources();
	bool LoadFontSource(const Gui::FFontFace& Font, const Gui::FResourceReader& Reader, std::string& Error);

	struct FContentNodes
	{
		Rml::ElementText* Text = nullptr;
		Rml::Element* TextBox = nullptr;
		Rml::Element* Image = nullptr;
	};
	bool SynchronizeContent(Rml::ElementDocument& Document, Rml::Element& Element, FContentNodes& Content,
							const Gui::FResolvedProperties& Properties, const Gui::FResolvedProperties* Previous,
							bool bComposite, std::string& Error);
} // namespace VeroScript::RmlUi
