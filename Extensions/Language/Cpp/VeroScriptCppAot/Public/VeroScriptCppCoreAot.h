// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAotProgram.h"
#include "VeroScriptCompile.h"

namespace VeroScript
{
	struct FCoreAotOptions
	{
		// Explicit application target, e.g. android-arm64, ios-arm64, ios-simulator-arm64.
		std::string Target;
		std::string FactoryName = "CreateVeroScriptAotProgram";
		bool bRequireAllNative = false;
		const std::vector<FNativeFunction>* Natives = nullptr;
		FExecutionCheck ExecutionCheck;
	};
	struct FCoreAotCoverage
	{
		std::string Function;
		std::string Reason;
		bool bNative = false;
	};
	struct FCoreAotSourceBundle
	{
		std::string Source;
		// Load these exact bytes in the target application before invoking its factory.
		std::vector<std::uint8_t> Bytecode;
		// Existing application package for Session/C ABI/mobile loaders.
		std::vector<std::uint8_t> Application;
		std::string ManifestJson;
		std::vector<FCoreAotCoverage> Coverage;
	};

	// Consumes a final, already linked and checked module. No source processing or
	// target native libraries are executed by this generator.
	bool GenerateCoreAotSourceBundle(const FBytecodeModule&, const FCoreAotOptions&,
		FCoreAotSourceBundle&, std::string& Error);
	// Convenience preparation entry. Compile extensions run on the build host;
	// business target macros are explicitly derived from Options.Target.
	bool CompileSourceToCoreAot(std::string_view Source, std::string_view FileLabel,
		const FCompileOptions&, const FCoreAotOptions&, FCoreAotSourceBundle&, std::string& Error);
}
