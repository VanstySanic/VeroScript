// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>

namespace VeroScript
{
	struct FSemanticVersion
	{
		constexpr bool operator==(const FSemanticVersion& Other) const
		{
			return Major == Other.Major
				&& Minor == Other.Minor
				&& Patch == Other.Patch;
		}

		constexpr bool operator!=(const FSemanticVersion& Other) const
		{
			return !(*this == Other);
		}

		std::uint16_t Major = 0;
		std::uint16_t Minor = 0;
		std::uint16_t Patch = 0;

	};

	inline constexpr FSemanticVersion VEROSCRIPT_VERSION = {0, 1, 1};
	// Binary descriptors use the product version directly, encoded as
	// major:8/minor:8/patch:16, without an additional version counter.
	static_assert(VEROSCRIPT_VERSION.Major <= 0xff && VEROSCRIPT_VERSION.Minor <= 0xff,
		"The product version must fit its binary encoding");
	inline constexpr std::uint32_t VEROSCRIPT_VERSION_CODE =
		(static_cast<std::uint32_t>(VEROSCRIPT_VERSION.Major) << 24)
		| (static_cast<std::uint32_t>(VEROSCRIPT_VERSION.Minor) << 16)
		| static_cast<std::uint32_t>(VEROSCRIPT_VERSION.Patch);
	// Bytecode format follows the product version. Loading requires an exact
	// version match and the fixed VSBC identifier; development artifacts are rebuilt.
	inline constexpr FSemanticVersion VEROSCRIPT_BC_VERSION = VEROSCRIPT_VERSION;
	std::string FormatVersion(const FSemanticVersion& Version);
	const char* GetVersionString();

	/**
	 * Host triple for packaging/diagnostics: e.g. "win-x64", "linux-arm64", "macos-x64".
	 * Architecture is the process ABI (not necessarily the machine if emulated).
	 */
	const char* GetHostPlatformString();

	// Core JIT service contract support. Concrete backend capabilities are reported
	// by IJitBackendFactory::GetDescription() for the selected backend module.
	const char* GetJitCapabilityString();
}
