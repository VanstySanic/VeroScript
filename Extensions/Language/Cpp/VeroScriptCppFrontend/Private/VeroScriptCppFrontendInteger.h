// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace VeroScript::CppFrontend::Detail
{
	// Internal shared C++ literal representation. Value stores the type's bits;
	// arithmetic uses the promoted width rather than erasing everything to int64.
	struct FIntegerLiteral
	{
		std::uint64_t Mask() const
		{
			return Width == 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << Width) - 1;
		}
		std::int64_t SignedValue() const
		{
			const auto Bits = Value & Mask();
			return Bits & (std::uint64_t{1} << (Width - 1))
				? -1 - static_cast<std::int64_t>(Mask() - Bits) : static_cast<std::int64_t>(Bits);
		}
		static FIntegerLiteral Signed(std::int64_t Value, unsigned Width = 32)
		{
			FIntegerLiteral Result{static_cast<std::uint64_t>(Value), Width, false};
			Result.Value &= Result.Mask();
			return Result;
		}

		std::uint64_t Value = 0;
		unsigned Width = 32;
		bool bUnsigned = false;
	};
	bool ParseIntegerLiteral(std::string_view Text, FIntegerLiteral& Out, std::string& Error);
}
