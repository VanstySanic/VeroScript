// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>

namespace VeroScript::Network::Test
{
	// Keep data-only tests independent of Core and accept C++17/C++20 path bytes.
	inline std::string PathToUtf8(const std::filesystem::path& Path, bool Generic = false)
	{
		#if defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
		const auto Bytes = Generic ? Path.generic_u8string() : Path.u8string();
		return std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
		#else
		return Generic ? Path.generic_u8string() : Path.u8string();
		#endif
	}
}
