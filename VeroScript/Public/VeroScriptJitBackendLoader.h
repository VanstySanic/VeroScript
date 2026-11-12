// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitBackend.h"

namespace VeroScript
{
	/**
	 * Loads an explicit UTF-8 file or directory. Directories are searched in sorted
	 * relative-path order, at most two subdirectory levels (backend/version).
	 * Only platform libraries named VeroScriptJit* are directory candidates.
	 * No environment, default installation path, or process working directory is
	 * searched implicitly. Returned factories/artifacts keep the module loaded.
	 */
	std::shared_ptr<const IJitBackendFactory> LoadJitBackend(
		std::string_view Path, std::string& OutError, std::string* OutLibraryPath = nullptr);
}
