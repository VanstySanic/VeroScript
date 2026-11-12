// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	/**
	 * Link dependency modules and one root module into one closed VM image.
	 *
	 * Dependencies initialize in the supplied order, before the root. The root
	 * may be an application or another library. A dependency may satisfy an empty
	 * root function stub with the same
	 * canonical name and signature. Other duplicate definitions are rejected.
	 */
	bool LinkBytecodeModules(
		const FBytecodeModule& RootModule,
		const std::vector<const FBytecodeModule*>& Dependencies,
		std::unique_ptr<FBytecodeModule>& OutModule,
		std::string& OutError);
}
