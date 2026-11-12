// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace VeroScript::Environment
{
	/**
	 * Read an environment variable through VeroScript's UTF-8 boundary.
	 * A disengaged optional means the variable is absent or the name is invalid;
	 * an engaged empty string is an explicitly empty value.
	 */
	std::optional<std::string> GetUtf8(std::string_view Name);
	// Distinguish absence from invalid names or platform/encoding failures.
	// On false, Output is unchanged. Allocation failure propagates to the caller.
	bool ReadUtf8(std::string_view Name, std::optional<std::string>& Output);

	// Preserve an explicitly empty value when setting a UTF-8 environment variable.
	bool SetUtf8(std::string_view Name, std::string_view Value);

	// Remove a UTF-8 environment variable.
	bool UnsetUtf8(std::string_view Name);
}
