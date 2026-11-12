// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace VeroScript::CppFrontend::Path
{
	inline std::filesystem::path FromUtf8(const std::string_view Text)
	{
		#if defined(_WIN32) && defined(__cpp_char8_t) && __cpp_char8_t >= 201811L
		std::u8string Utf8Text;
		Utf8Text.reserve(Text.size());
		for (const unsigned char Byte : Text)
		{
			Utf8Text.push_back(static_cast<char8_t>(Byte));
		}
		return std::filesystem::path(Utf8Text);
		#elif defined(_WIN32)
		return std::filesystem::u8path(Text.begin(), Text.end());
		#else
		return std::filesystem::path(std::string(Text));
		#endif
	}

	inline std::string ToUtf8(const std::filesystem::path& Value)
	{
		#if defined(_WIN32)
		const auto Bytes = Value.generic_u8string();
		std::string Result;
		Result.reserve(Bytes.size());
		for (const auto Byte : Bytes)
		{
			Result.push_back(static_cast<char>(Byte));
		}
		return Result;
		#else
		return Value.generic_string();
		#endif
	}
}
