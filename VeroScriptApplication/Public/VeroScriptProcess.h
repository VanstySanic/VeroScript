// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArchive.h"

namespace VeroScript::Application
{
	struct FProcessRequest
	{
		std::string Executable, WorkingDirectory;
		std::vector<std::string> Arguments;
		std::map<std::string, std::string> Environment;
		bool InheritEnvironment = false;
		std::uint64_t TimeoutMilliseconds = 60000;
		std::function<void()> CheckCancelled;
		std::uint64_t MaxOutputBytes = 16ull << 20;
		// Borrowed chunks. Both streams are drained while the child is running.
		std::function<void(std::string_view Text, bool StandardError)> Output;
	};
	// Program and arguments are passed directly, without invoking a shell.
	// Cancellation terminates and waits for the process tree owned by this call.
	bool RunProcess(const FProcessRequest&, int& ExitCode, FDiagnostic&);
}
