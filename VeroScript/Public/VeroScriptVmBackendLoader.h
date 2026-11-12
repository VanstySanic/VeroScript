// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVmBackend.h"
#include <string_view>

namespace VeroScript
{
	// Explicit file or bounded sorted directory search. No implicit locations.
	std::shared_ptr<const IVmBackendFactory> LoadVmBackend(std::string_view Path,
		std::string& Error, std::string* LibraryPath = nullptr);
}
