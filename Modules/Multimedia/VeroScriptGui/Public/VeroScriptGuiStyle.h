// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiTypes.h"
#include <map>

namespace VeroScript::Gui
{
	/** Values exchanged by style codecs without depending on the script runtime. */
	using FStyleValue = std::variant<FPropertyValue, std::int64_t, std::uint64_t, std::string>;

	enum class EStyleValueType : std::uint8_t
	{
		String,
		Bool,
		Int,
		Int64,
		UInt64,
		Float,
		Double,
		Length,
		Insets,
		Color,
		Enum,
		Raw
	};

	struct FStyleProperty
	{
		std::string Name, Mark;
		// Decoders validate the entire input. An error never falls through to a lower layer.
		std::function<bool(std::string_view, FStyleValue&, std::string&)> Decode;
	};

	FStyleProperty MakeStyleProperty(std::string Name, std::string Mark, EStyleValueType Type,
									 std::map<std::string, std::int64_t> EnumValues = {});
} // namespace VeroScript::Gui
