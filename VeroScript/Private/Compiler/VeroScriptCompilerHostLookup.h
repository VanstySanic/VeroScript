// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"

namespace VeroScript::CompilerDetail
{
	const FHostTypeDesc* FindHostTypeWithNamespaceSpelling(const IHostBindingCatalog* Bindings, std::string_view Name);
}
