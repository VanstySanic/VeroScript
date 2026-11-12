// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRuntime.h"

namespace VeroScript::VmDetail
{
	inline bool IsTrivialLifetimeValue(const FValue& Value)
	{
		if (std::holds_alternative<std::int32_t>(Value) || Runtime::IsNumericValue(Value)
		    || std::holds_alternative<bool>(Value) || std::holds_alternative<std::string>(Value)
		    || std::holds_alternative<std::u16string>(Value) || std::holds_alternative<FEnumValue>(Value))
		{
			return true;
		}
		const auto* Object = std::get_if<FObjectHandle>(&Value);
		// A null alias may retain a custom host owner with a lifetime hook.
		return Object && !*Object && Object->use_count() == 0;
	}

	inline bool IsFlatArrayLifetime(const FValue& Value)
	{
		const auto* Array = std::get_if<FDynamicArrayHandle>(&Value);
		return Array && (!*Array || (*Array)->Elements.empty()
		    || ((*Array)->Elements.size() == 1 && IsTrivialLifetimeValue((*Array)->Elements.front())));
	}
}
