// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEngine.h"
#include "VeroScriptBytecodeVm.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPath.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace VeroScript;
	using FStrings = const std::vector<std::string_view>&;
	using FMacros = const std::vector<FPredefinedMacro>&;

	// Existing consumers may store these exact function pointer types.
	using FLegacySourceEntry = FRunResult (*)(
	    std::string_view, std::string_view, std::ostream&, std::ostream&,
	    ERunMode, FStrings, FStrings, std::istream*, FStrings, FStrings,
	    EExecutionBackend, FMacros, FStrings, bool, bool);
	using FLegacyFileEntry = FRunResult (*)(
	    std::string_view, std::ostream&, std::ostream&, FStrings, FStrings,
	    bool, std::istream*, FStrings, std::string_view, FStrings,
	    EExecutionBackend, const IHostBindingCatalog*, FMacros, FStrings, bool, bool);

	bool ExpectSuccess(const FRunResult& Result, const char* Entry,
	    const std::ostringstream& Diagnostics)
	{
		if (Result.bSuccess && Result.ExitCode == 42) return true;
		std::cerr << Entry << " failed: exit=" << Result.ExitCode << '\n'
		          << Diagnostics.str();
		return false;
	}
}

int RunEngineConsumer(const std::string_view ScriptPath, const std::string_view BackendPath)
{
	using namespace VeroScript;
	if (ScriptPath.empty())
	{
		std::cerr << "Expected a script path and optional JIT backend path\n";
		return 1;
	}
	std::ostringstream Output;
	std::ostringstream Diagnostics;
	const FLegacySourceEntry LegacySource = &VeroScriptEngine::RunSource;
	const FLegacyFileEntry LegacyFile = &VeroScriptEngine::RunFile;
	// Legacy function-pointer signatures remain available. Pure Core now needs an explicit VM.
	const auto OldSource = LegacySource("int main() { return 42; }", "<legacy-sdk-source>",
	    Output, Diagnostics, ERunMode::Script, {}, {}, nullptr, {}, {}, EExecutionBackend::Mixed, {}, {}, true, false);
	const auto OldFile = LegacyFile(ScriptPath, Output, Diagnostics, {}, {}, false,
	    nullptr, {}, {}, {}, EExecutionBackend::Mixed, nullptr, {}, {}, true, false);
	if (OldSource.bSuccess || OldFile.bSuccess || OldSource.Error.MessageId != "diag.vm_backend_required" || OldFile.Error.MessageId != "diag.vm_backend_required")
	{
		return 1;
	}
	const auto VmFactory = BytecodeVm::CreateFactory(GetVmHostServices());
	if (!ExpectSuccess(VeroScriptEngine::RunSource("int main() { return 42; }", "<assembled-sdk-source>",
	                       Output, Diagnostics, ERunMode::Script, {}, {}, nullptr, {}, {}, EExecutionBackend::Mixed,
	                       {}, {}, true, false, {}, VmFactory),
	        "Explicit VM source entry", Diagnostics))
	{
		return 1;
	}

	std::shared_ptr<const IJitBackendFactory> Factory;
	if (!BackendPath.empty())
	{
		std::string Error;
		Factory = LoadJitBackend(BackendPath, Error);
		if (!Factory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	if (!ExpectSuccess(VeroScriptEngine::RunFile(ScriptPath, Output, Diagnostics,
	                       {}, {}, false, nullptr, {}, {}, {}, Factory ? EExecutionBackend::Jit : EExecutionBackend::Mixed,
	                       nullptr, {}, {}, true, false, Factory, VmFactory),
	        "Explicit backend file entry", Diagnostics))
	{
		return 1;
	}
	std::cout << "C++ SDK legacy signatures and file backend entry passed\n";
	return 0;
}

#if defined(_WIN32)
int wmain(const int ArgumentCount, wchar_t* const Arguments[])
{
	// Windows narrow argv uses the active code page; public path APIs use UTF-8.
	const std::string ScriptPath = ArgumentCount > 1
	    ? VeroScript::PathUtil::ToUtf8(std::filesystem::path(Arguments[1]))
	    : std::string{};
	const std::string BackendPath = ArgumentCount > 2
	    ? VeroScript::PathUtil::ToUtf8(std::filesystem::path(Arguments[2]))
	    : std::string{};
	return RunEngineConsumer(ScriptPath, BackendPath);
}
#else
int main(const int ArgumentCount, char* const Arguments[])
{
	return RunEngineConsumer(ArgumentCount > 1 ? Arguments[1] : "",
	    ArgumentCount > 2 ? Arguments[2] : "");
}
#endif
