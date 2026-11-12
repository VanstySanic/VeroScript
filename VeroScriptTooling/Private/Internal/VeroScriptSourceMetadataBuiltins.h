// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	using FSourceMetadataMethod = std::pair<const char*, const char*>;
	// Canonical builtin owner names; ordinary reflection owners include only the
	// metadata additions. Details mirror the core source-metadata template surface.
	const std::vector<FSourceMetadataMethod>& SourceMetadataBuiltinMethods(std::string_view Owner);
}
