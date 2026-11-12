// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompiledExecution.h"
#include "VeroScriptNative.h"
#include "VeroScriptVersion.h"
#include <string>
#include <vector>

namespace VeroScript
{
	struct FAotProgramDescription
	{
		std::string ProgramDigest;
		std::string NativeBindingsDigest;
		std::string ServiceAbi;
		std::string CoreImplementation;
		std::string Target;
		std::uint32_t CoreVersion = VEROSCRIPT_VERSION_CODE;
	};
	struct FAotFunctionEntry
	{
		std::uint16_t FunctionIndex = 0;
		FCompiledFunctionEntry Entry = nullptr;
		std::uint64_t GovernanceCapabilities = ControlledExecutionCapabilities;
	};
	class IAotProgram
	{
	public:
		virtual ~IAotProgram() = default;
		virtual const FAotProgramDescription& GetDescription() const noexcept = 0;
		virtual const FAotFunctionEntry* Find(std::uint16_t FunctionIndex) const noexcept = 0;
		virtual const std::vector<FAotFunctionEntry>& GetFunctions() const noexcept = 0;
	};

	std::string GetAotServiceAbi();
	bool ComputeAotProgramDigest(const FBytecodeModule&, std::string& Digest,
		std::string& Error, FExecutionCheck Check = {});
	// Input is the final merged native table in bytecode index order.
	std::string ComputeAotNativeBindingsDigest(const std::vector<FNativeFunction>& Natives);
	bool ComputeAotNativeBindingsDigest(const std::vector<FNativeFunction>& Natives,
		std::string& Digest, std::string& Error, FExecutionCheck Check = {});
	std::shared_ptr<const IAotProgram> CreateAotProgram(FAotProgramDescription Description,
		std::vector<FAotFunctionEntry> Functions, std::string& Error,
		std::shared_ptr<const void> CodeOwner = {});
	bool ValidateAotProgram(const IAotProgram&, const FBytecodeModule&,
		const std::vector<FNativeFunction>& Natives, std::string& Error, FExecutionCheck Check = {});
}
