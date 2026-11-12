// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCommandLine.h"
#include "VeroScriptNativeExtensionLoader.h"
#include <set>

namespace VeroScript::Cli
{
	class FCommandLineExtensions final : public ICommandLineExtensionService
	{
	public:
		bool Register(FCommandLineModule, std::string&) const override;

		FCommandLineExtensions();
		~FCommandLineExtensions();
		bool Load(const std::string& Path, const std::string& WorkingDirectory, std::string&);
		FNativeExtensionServiceBinding Binding() const;
		// Additional host-owned services supplied to explicitly loaded CLI modules.
		std::vector<FNativeExtensionServiceBinding> Services;
		// Returns 0 for an unknown option, -1 on error, otherwise consumed tokens.
		int ParseOption(std::string_view, std::string_view Next, bool HasNext, std::string&);
		bool Configure(const FCommandLineContext&, std::string&);
		bool RunCommand(const std::string&, const std::vector<std::string>&, const FCommandLineContext&, int&, std::string&);
		void PrintHelp(std::ostream&) const;
		bool HasCommand(const std::string&) const;
		bool HasOptions() const;
		const FCommandLineValues& GetValues(const std::string& ModuleId) const;
		bool Validate(std::string& Error) const;
		void Close();

	private:
		struct FServiceState;
		class FService;
		std::shared_ptr<FServiceState> ServiceState;
		// Modules contain borrowed pointers only; destroy descriptors before leases.
		std::vector<std::shared_ptr<const void>> Owners;
		std::unique_ptr<FNativeExtensionRegistry> Registry;
		mutable std::map<std::string, FCommandLineModule> Modules;
		mutable std::string Failure;
		mutable bool Open = true;
		bool Configured = false;
		std::map<std::string, FCommandLineValues> Values;
		std::set<std::string> Paths;
	};
}
