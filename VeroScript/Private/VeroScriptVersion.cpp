// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVersion.h"
#include "VeroScriptPlatform.h"

#include <string>

namespace VeroScript
{
	std::string FormatVersion(const FSemanticVersion& Version)
	{
		return std::to_string(Version.Major) + "."
			+ std::to_string(Version.Minor) + "."
			+ std::to_string(Version.Patch);
	}

	const char* GetVersionString()
	{
		static const std::string Version = FormatVersion(VEROSCRIPT_VERSION)
		+ " (bytecode " + FormatVersion(VEROSCRIPT_BC_VERSION) + ")";
		return Version.c_str();
	}

	const char* GetHostPlatformString()
	{
		return GetHostPlatformInfo().Platform;
	}

	const char* GetJitCapabilityString()
	{
		return "backend-services";
	}
}
