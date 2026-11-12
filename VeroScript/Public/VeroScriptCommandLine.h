// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"
#include <iosfwd>

namespace VeroScript
{
	inline constexpr const char* CommandLineServiceId = "veroscript.cli";
	inline constexpr const char* CommandLineServiceVersion = "0.1.1";

	struct FCommandLineContext
	{
		std::string WorkingDirectory, Executable;
		std::istream* Input = nullptr;
		std::ostream* Output = nullptr;
		std::ostream* Errors = nullptr;
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::string LanguageTag;
	};

	struct FCommandLineOption
	{
		// Bare, case-sensitive ASCII long name, e.g. "gui-backend".
		std::string Name, Description, ValueName;
		bool RequiresValue = true, Repeatable = false;
	};
	using FCommandLineValues = std::map<std::string, std::vector<std::string>>;

	struct FCommandLineCommand
	{
		std::string Name, Description, Usage;
		// Arguments exclude the command name and preserve UTF-8 and token boundaries.
		int (*Run)(const FCommandLineContext&, const std::vector<std::string>&, void*) = nullptr;
		void* UserData = nullptr;
	};

	struct FCommandLineModule
	{
		std::string Id, Description;
		std::vector<FCommandLineOption> Options;
		std::vector<FCommandLineCommand> Commands;
		// Called once, after parsing, before execution. Absent options have no entry;
		// a present flag contains one empty string. Borrowed state is owned by the
		// native registration, not by this descriptor. Configuration must not run scripts.
		bool (*Configure)(const FCommandLineContext&, const FCommandLineValues&, void*, std::string&) = nullptr;
		void* UserData = nullptr;
	};

	class ICommandLineExtensionService : public INativeExtensionService
	{
	public:
		// Register during native Create only. The CLI closes registration before
		// dispatch. A failure invalidates the entire CLI registration batch.
		virtual bool Register(FCommandLineModule, std::string& Error) const = 0;
	};
}
