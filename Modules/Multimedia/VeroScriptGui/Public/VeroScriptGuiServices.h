// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiApplication.h"
#include "VeroScriptNativeExtension.h"

namespace VeroScript::Gui
{
	FResourceReader MakeGuiResourceReader(std::shared_ptr<const FModuleEnvironment> Environment = {});
	/** Providers are copied; the immutable resource environment is retained by created services. */
	FApplicationFactories MakeRegisteredServiceFactories(std::vector<FNativeServiceProviderDesc> Providers,
														 std::shared_ptr<const FModuleEnvironment> Environment = {});
	std::unique_ptr<IBackendContext> CreateRegisteredGuiBackend(const FNativeServiceProviderDesc& Provider,
																FBackendOptions Options,
																const FNativeServiceCreateContext& Context,
																std::string& Error);
} // namespace VeroScript::Gui
