// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

// IDNA rules adapted from python-idna 3.12; see ThirdParty/idna/LICENSE.

#include <cstdint>
#include <string_view>
#include <vector>
namespace VeroScript::Json::Detail::Formats
{
	bool DecodeUtf8(std::string_view, std::vector<std::uint32_t>&);
	std::vector<std::uint32_t> Normalize(const std::vector<std::uint32_t>&);
	bool Hostname(std::string_view, bool International);
} // namespace VeroScript::Json::Detail::Formats
