// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiBackend.h"
#include <RmlUi/Core/Element.h>

namespace VeroScript::RmlUi
{
	class FRenderer;
	class FGuiElement final : public Rml::Element
	{
	public:
		~FGuiElement() override;
		explicit FGuiElement(const Rml::String& Tag);
		std::uint64_t GetIdentity() const noexcept
		{
			return Identity;
		}
		void Configure(FRenderer& Renderer, std::uint64_t Identity, double Scale, Gui::FPaintCallback Callback);
		bool SynchronizeFlow(Rml::ElementDocument& Document, const std::vector<Rml::Element*>& Items,
			double Gap, std::string& Error);

	protected:
		void OnRender() override;

	private:
		FRenderer* Renderer = nullptr;
		std::uint64_t Identity = 0;
		double Scale = 1;
		Gui::FPaintCallback Callback;
		std::vector<Rml::Element*> FlowGaps;
	};
	void RegisterGuiElement();
} // namespace VeroScript::RmlUi
