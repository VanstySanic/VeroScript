// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArchive.h"

namespace VeroScript::Application
{
	// Native PE/ELF bootstrap plus a ZIP application and a versioned, bounded locator.
	// PE certificate tables may follow the locator; Mach-O wrapping needs a provider.
	bool WriteExecutableApplication(const std::string& AbsoluteLauncher,
		const std::vector<FContentFile>&, const std::string& NewAbsoluteOutput,
		FDiagnostic&, const FLimits& = {});
	bool ReadExecutableApplication(const std::string& AbsoluteExecutable,
		std::vector<std::uint8_t>& ArchiveBytes, FDiagnostic&, const FLimits& = {});
}
