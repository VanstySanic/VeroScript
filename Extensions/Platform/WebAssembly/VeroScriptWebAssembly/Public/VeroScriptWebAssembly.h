// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptWebAssemblyVersion.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::WebAssembly
{
	inline constexpr std::string_view AbiVersion = VERO_WEB_ABI_VERSION;

	enum class EBackend
	{
		Vm = 0,
		Mixed,
		Jit,
	};

	struct FResult
	{
		struct FJitStatistics
		{
			std::size_t CompileAttempts = 0;
			std::size_t CompiledEntries = 0;
			std::size_t BridgeTrampolineEntries = 0;
			std::size_t LinearTrampolineEntries = 0;
			std::size_t VmTrampolineEntries = 0;
			std::size_t UnsupportedEntries = 0;
			std::size_t DispatchAttempts = 0;
			std::size_t Dispatches = 0;
			std::size_t Fallbacks = 0;
			std::size_t HotFunctions = 0;
			std::size_t DirectNativeCalls = 0;
		};

		bool bSuccess = false;
		int ExitCode = 1;
		bool bExitRequested = false;
		std::optional<EBackend> Backend;
		std::string Output;
		std::string ErrorOutput;
		std::vector<FScriptError> Diagnostics;
		std::optional<std::uint32_t> ProgramHandle;
		std::optional<double> CompileMilliseconds;
		std::optional<double> ExecuteMilliseconds;
		std::optional<FJitStatistics> Jit;
	};

	struct FSourceFile
	{
		std::string_view Path;
		std::string_view Source;
	};

	/**
	 * Browser-safe, in-memory entry points without an injected JIT backend.
	 * Vm and Mixed execute through the VM; Jit reports the core's structured
	 * missing-backend diagnostic. Backend identifiers remain ABI-compatible.
	 */
	class FRuntime
	{
	public:
		// WebAssembly runtimes are deterministic by default instead of inheriting the
		// browser/Node process locale. Available tags are discovered from deployed catalogs.
		bool SetLanguage(std::string_view LanguageTag);
		void SetStandardLibraryLimits(FStandardLibraryLimits Limits)
		{
			StandardLibraryLimits = Limits;
		}
		FStandardLibraryLimits GetStandardLibraryLimits() const
		{
			return StandardLibraryLimits;
		}
		std::string_view GetLanguage() const
		{
			return LanguageTag;
		}
		std::vector<std::string> GetAvailableLanguages() const;

		FResult RunSource(
		    std::string_view Source,
		    std::string_view FileLabel = "<webassembly>",
		    std::string_view Input = {},
		    const std::vector<std::string_view>& ScriptArgs = {},
		    EBackend Backend = EBackend::Mixed,
		    const std::vector<FSourceFile>& SourceFiles = {}) const;

		FResult RunSourceWithInputStream(
		    std::string_view Source,
		    std::string_view FileLabel,
		    std::istream& InputStream,
		    const std::vector<std::string_view>& ScriptArgs = {},
		    EBackend Backend = EBackend::Mixed,
		    const std::vector<FSourceFile>& SourceFiles = {}) const;

		FResult CheckSource(
		    std::string_view Source,
		    std::string_view FileLabel = "<webassembly>",
		    const std::vector<FSourceFile>& SourceFiles = {}) const;

	private:
		void ApplyLanguage() const;
		std::string LanguageTag = "en-US";
		FStandardLibraryLimits StandardLibraryLimits;
	};

	std::string_view BackendName(EBackend Backend);
	std::string ResultToJson(const FResult& Result, std::string_view Operation);
}
