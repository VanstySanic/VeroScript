// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformApi.h"
#include "VeroScriptHostBinding.h"
#include <functional>

namespace VeroScript::Platform
{
	using FServiceFactory = std::function<std::shared_ptr<IService>(const std::string& Backend, EEventPumpMode PumpMode,
																	std::string& Error)>;

	/** Register before publishing the binding catalog. The factory must not capture that catalog's environment
	 * strongly. */
	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error);
	// Accepts only handles created by this binding adapter, rather than trusting a display type name.
	std::shared_ptr<IWindow> GetScriptWindow(const FValue& Value, std::string& Error);
}
