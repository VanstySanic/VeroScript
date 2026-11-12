// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSourceMetadata.h"
#include "VeroScriptPreprocessor.h"
#include <unordered_map>

namespace VeroScript::Tooling::Detail
{
	// Project source edits and line activity back to the original module.
	// Call once with the loader's unprojected maps for each loaded program.
	void ApplyProcessingSourceMaps(const FSourceMetadataSnapshot* Metadata,
		std::unordered_map<std::string, FPreprocessLineMap>& Maps);
}
