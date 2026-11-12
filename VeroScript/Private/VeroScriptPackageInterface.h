// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <unordered_set>

namespace VeroScript
{
	struct FProgram;
	struct FBytecodeModule;

	/**
	 * Build the declaration-only source embedded in a binary library.
	 * Open generic declarations are supplied by PackageTemplateModules; this
	 * declaration surface contains closed bytecode symbols only.
	 */
	bool BuildPackageInterfaceSource(
		const FProgram& Program,
		const std::unordered_set<std::string>& SurfaceModules,
		std::string& OutSource,
		std::string& OutError, const FBytecodeModule* Compiled = nullptr);
}
