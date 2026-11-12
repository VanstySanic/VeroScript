// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCommandLine.h"
#include "VeroScriptGuiVersion.h"

namespace VeroScript
{
	struct FEngineConfig;
	struct FPackageData;
} // namespace VeroScript

namespace VeroScript::Gui
{
	struct FScriptServices;
	inline constexpr const char* ToolServiceId = "veroscript.gui.tools";
	inline constexpr const char* ToolServiceVersion = Version;
	struct FCommandLineRegistration
	{
		// Keep the owner until the command registry has finished using its borrowed UserData.
		std::shared_ptr<const void> Owner;
		FCommandLineModule Module;
		// Host hooks use this registration's configuration, including ordinary script runs.
		// Borrowed engine string views must remain alive for the invocation.
		std::function<void(const FEngineConfig&)> SetEngineOptions;
		std::function<void(FScriptServices)> SetRuntimeServices;
		std::function<bool(FHostBindingRegistry&, std::string&)> RegisterBindings;
		std::function<bool(FEngineConfig&, const FHostBindingRegistry*, std::string&)> ConfigureEngine;
		std::function<bool(const FPackageData&, std::string&)> UseArtifact;
	};
	class ICommandLineService : public INativeExtensionService
	{
	public:
		/** Independent invocation state; registration/configuration creates no UI services. */
		virtual FCommandLineRegistration CreateRegistration() const = 0;
		// A package CLI adapter can reuse a registration already owned by this host.
		virtual bool HasHostRegistration() const noexcept
		{
			return false;
		}
	};
} // namespace VeroScript::Gui
