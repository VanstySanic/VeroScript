// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

namespace VeroScript
{
	// Decode an already sign-extended bit pattern without an out-of-range
	// unsigned-to-signed conversion (implementation-defined in C++17).
	inline std::int64_t DecodeConstantSignedBits(std::uint64_t Bits)
	{
		return (Bits & (std::uint64_t{1} << 63)) ? -1 - static_cast<std::int64_t>(~Bits) : static_cast<std::int64_t>(Bits);
	}
}
