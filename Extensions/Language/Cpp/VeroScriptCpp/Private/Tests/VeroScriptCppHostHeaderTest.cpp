// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHost.h"

#include <type_traits>

static_assert(std::is_abstract_v<VeroScript::IEngineHost>);
