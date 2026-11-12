// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"

namespace VeroScript::Gui
{
	/** Preserve published host identities and append GUI types in a disjoint id range. */
	std::shared_ptr<const IHostBindingCatalog> MergeGuiScriptCatalog(std::shared_ptr<const IHostBindingCatalog> Base,
																	 std::shared_ptr<const FHostBindingRegistry> Gui,
																	 std::uint32_t& TypeOffset, std::string& Error);
} // namespace VeroScript::Gui
