// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptApplication.h"
#include "VeroScriptPackageManager.h"

namespace VeroScript::Application
{
	using FApplicationBackendSelector = std::function<std::string(std::string_view)>;
	// Optional adapter target, linking Application and read-only PackageRuntime.
	// Does not solve, install, access HTTP or execute installation hooks.
	// ProvidersReady publishes factories and the effective backend choices after registration;
	// it must not create UI services. Choices follow host, application, then project precedence.
	void ConfigurePackageApplicationServices(FApplicationServices&,
		std::shared_ptr<const Packages::FInstalledEnvironment> External = {},
		Packages::FCoreEnvironmentOptions Options = {},
		std::function<void(std::vector<FNativeServiceProviderDesc>, std::shared_ptr<const FModuleEnvironment>,
			FApplicationBackendSelector)>
			ProvidersReady = {});
}
