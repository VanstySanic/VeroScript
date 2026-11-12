// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptFont.h"
#include "VeroScriptFontDefaultFont.h"
#include <iterator>

namespace VeroScript::Font
{
	// Keep the asset in its own archive member so backend-only users need not link it.
	const std::vector<std::uint8_t> &GetDefaultFontData()
	{
		static const std::vector<std::uint8_t> Bytes(std::begin(VeroScriptDefaultFont), std::end(VeroScriptDefaultFont));
		return Bytes;
	}
} // namespace VeroScript::Font
