// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiContext.h"
#include "VeroScriptI18n.h"

namespace VeroScript::Gui
{
	/** The context retains the supplied core service. Changes become visible at the next GUI frame. */
	bool SetLocalization(const std::shared_ptr<FContext>& Context, std::shared_ptr<I18n::FI18nContext> Localization,
						 std::string& Error);
	std::shared_ptr<I18n::FI18nContext> GetLocalization(const std::shared_ptr<FContext>& Context, std::string& Error);
	I18n::FI18nSnapshot GetLocalizationSnapshot(const std::shared_ptr<FContext>& Context, std::string& Error);
} // namespace VeroScript::Gui
