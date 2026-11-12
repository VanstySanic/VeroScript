// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string_view>
namespace VeroScript::Json::Detail::Formats
{
	bool Known(std::string_view);
	bool Validate(std::string_view Format, std::string_view Text);
} // namespace VeroScript::Json::Detail::Formats
