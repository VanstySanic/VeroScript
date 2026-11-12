// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace VeroScript
{
	struct FBytecodeModule;

	namespace CompilerDetail
	{
		// Inspect lowered execution dependencies while the initializer is detached.
		// Callbacks may append descriptors and initializer code to the module.
		bool PrepareStaticInitializationDependencies(
			FBytecodeModule& Module,
			const std::vector<std::uint8_t>& Code,
			const std::function<bool(const std::string&)>& PrepareOwner,
			const std::function<void()>& CheckCancelled);
	}
}
