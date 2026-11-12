// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#if defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace VeroScript::PathDetail
{
	inline bool IsTrustedDarwinSystemAlias(const std::filesystem::path& Path)
	{
		#if defined(__APPLE__)
		const char* Expected = Path == "/tmp" ? "/private/tmp" : Path == "/var" ? "/private/var" :
			Path == "/etc" ? "/private/etc" : nullptr;
		if (!Expected) return false;
		struct stat Status{};
		if (lstat(Path.c_str(), &Status) != 0 || Status.st_uid != 0 || !S_ISLNK(Status.st_mode)) return false;
		std::error_code Error;
		const auto Target = std::filesystem::canonical(Path, Error);
		return !Error && Target == Expected;
		#else
		(void)Path;
		return false;
		#endif
	}
}
