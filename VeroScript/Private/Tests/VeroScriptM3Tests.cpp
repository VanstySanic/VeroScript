// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#else
#include <stdlib.h>
#include <unistd.h>
#endif

namespace
{
	class FScopedTestTempRoot
	{
	public:
		FScopedTestTempRoot()
		{
			#if defined(_WIN32)
			const auto ProcessId = static_cast<unsigned long>(_getpid());
			#else
			const auto ProcessId = static_cast<unsigned long>(getpid());
			#endif
			Root = std::filesystem::temp_directory_path()
				/ ("VeroScriptM3Tests-" + std::to_string(ProcessId));

			std::error_code Error;
			std::filesystem::remove_all(Root, Error);
			Error.clear();
			std::filesystem::create_directories(Root, Error);
			if (Error)
			{
				throw std::filesystem::filesystem_error(
					"create isolated VeroScript test temporary directory",
					Root,
					Error);
			}

			#if defined(_WIN32)
			_wputenv_s(L"TEMP", Root.c_str());
			_wputenv_s(L"TMP", Root.c_str());
			#else
			const std::string RootText = Root.string();
			setenv("TMPDIR", RootText.c_str(), 1);
			#endif
		}

		~FScopedTestTempRoot()
		{
			std::error_code Error;
			std::filesystem::remove_all(Root, Error);
		}

		FScopedTestTempRoot(const FScopedTestTempRoot&) = delete;
		FScopedTestTempRoot& operator=(const FScopedTestTempRoot&) = delete;

	private:
		std::filesystem::path Root;
	};

	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptM3Tests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool ExpectOutput(
		const std::string_view Source,
		const VeroScript::ERunMode RunMode,
		const std::string_view ExpectedOutput,
		const char* Message)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			RunMode);
		if (!Expect(Result.bSuccess, Message))
		{
			std::cerr << Errors.str();
			return false;
		}
		if (!Expect(Errors.str().empty(), "expected no errors"))
		{
			std::cerr << Errors.str();
			return false;
		}
		if (!Expect(Output.str() == ExpectedOutput, Message))
		{
			std::cerr << "expected output:\n"
					  << ExpectedOutput
				<< "actual output:\n"
					  << Output.str();
			return false;
		}
		return true;
	}
}

int main()
{
	// Ordered domain suites share this executable's temp-root and helper fixtures.
	#include "Suites/VeroScriptM3/01_syntax_and_control_flow.inl"
	#include "Suites/VeroScriptM3/02_types_and_operators.inl"
	#include "Suites/VeroScriptM3/03_objects_and_containers.inl"
	#include "Suites/VeroScriptM3/04_session_and_reload.inl"
	#include "Suites/VeroScriptM3/05_advanced_containers.inl"
	#include "Suites/VeroScriptM3/06_standard_library_and_runtime.inl"
}
