// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "JsonInternal.h"

namespace VeroScript::Json::Detail
{
	// Exact decimal arithmetic shared by the Schema adapter and its qualification tests.
	bool IsInteger(const FDecimal& Value) noexcept;
	bool IsMultiple(const FDecimal& Value, const FDecimal& Divisor, FBudget& Budget);
} // namespace VeroScript::Json::Detail
