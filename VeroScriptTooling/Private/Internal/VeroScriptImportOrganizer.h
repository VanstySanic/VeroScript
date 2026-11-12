// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptToolingTypes.h"

namespace VeroScript::Tooling
{
	class VeroScriptWorkspace;
}

namespace VeroScript::Tooling::Detail
{
	// Confirm complete global declarations against actual module preprocessing.
	// Only unchanged physical lines may be deleted; macros and inactive code remain.
	std::vector<FTextRange> FindDuplicateImportLines(std::string_view Source,
		std::string_view Path, const VeroScriptWorkspace& Workspace);
}
