// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptStandardLibraryServices.h"

#include "VeroScriptScriptCompileExtensions.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompileProcessing.h"

#include "VeroScriptDiagnostics.h"
#include "VeroScriptPreprocessor.h"
#include "VeroScriptMemory.h"
#include "VeroScriptNumerics.h"
#include "VeroScriptI18n.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string_view>
#include <vector>

namespace VeroScript
{
	class FExecutionControl;
	class IJitBackendFactory;
	class IVmBackendFactory;
	class IAotProgram;
	class FModuleEnvironment;

	// The VM executes interpreted frames iteratively, so this is a resource
	// default rather than a language restriction. Embedders may override it per
	// VM/session through SetMaxCallDepth or FEngineConfig::MaxCallDepth.
	inline constexpr std::size_t VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH = 16384;

	/**
	 * How the engine/VM runs source.
	 * Eval preserves the traditional incremental/REPL initialization path.
	 * EvalOnce is a one-shot source evaluation whose module initializer may use
	 * the selected JIT backend. Script additionally invokes the module main entry.
	 */
	enum class ERunMode : std::uint8_t
	{
		Eval,
		Script,
		// Appended to preserve the public numeric values of Eval and Script.
		EvalOnce,
	};

	/**
	 * Execution backend selection (orthogonal to ERunMode).
	 * Vm: bytecode interpreter only (full semantics + debug + hot reload).
	 * Mixed: JIT compile when supported, otherwise fall back to the VM (default).
	 * Jit: prefer compiling all functions up front; unsupported bodies still fall
	 *      back to the VM until a future strict mode exists.
	 * Debugger attachment, retained initialization ranges and bounded cleanup use Vm.
	 * Ordinary functions in a persistent session may use its generation-bound JIT.
	 */
	enum class EExecutionBackend : std::uint8_t
	{
		Vm = 0,
		Mixed,
		Jit,
	};

	/**
	 * Per-execution cooperative resource limits. Zero means unlimited. Limits
	 * are deliberately opt-in so ordinary trusted scripts retain their existing
	 * execution backend and performance characteristics.
	 */
	struct FExecutionLimits
	{
		std::uint64_t MaxInstructions = 0;
		std::uint64_t TimeoutMilliseconds = 0;
	};

	/**
	 * Permissions for script-provided standard-library side effects. These
	 * govern builtins only; the embedding host remains responsible for exposing
	 * native callbacks with suitable authority. Defaults preserve trusted-script
	 * behavior.
	 */
	struct FScriptCapabilities
	{
		bool bAllowFileRead = true;
		bool bAllowFileWrite = true;
		bool bAllowFileDelete = true;
		bool bAllowEnvironmentRead = true;
		bool bAllowEnvironmentWrite = true;
		bool bAllowConsoleInput = true;
		bool bAllowConsoleOutput = true;
		bool bAllowTaskSpawn = true;
		// chdir changes the host process working directory, including other sessions.
		bool bAllowProcessDirectoryChange = true;
		bool bAllowMemoryAddress = true;
		bool bAllowNondeterminism = true;
	};

	inline bool TryParseExecutionBackend(const std::string_view Text, EExecutionBackend& OutBackend)
	{
		if (Text == "vm" || Text == "interpret" || Text == "interpreter")
		{
			OutBackend = EExecutionBackend::Vm;
			return true;
		}
		if (Text == "mixed" || Text == "hybrid")
		{
			OutBackend = EExecutionBackend::Mixed;
			return true;
		}
		if (Text == "jit")
		{
			OutBackend = EExecutionBackend::Jit;
			return true;
		}
		return false;
	}

	inline const char* ExecutionBackendName(const EExecutionBackend Backend)
	{
		switch (Backend)
		{
		case EExecutionBackend::Vm:
			return "vm";
		case EExecutionBackend::Mixed:
			return "mixed";
		case EExecutionBackend::Jit:
			return "jit";
		default:
			{
				break;
			}
		}
		return "vm";
	}

	/**
	 * Outcome of check/run entry points; ExitCode follows CLI conventions.
	 */
	struct FRunResult
	{
		// True means compilation/execution completed normally; a non-zero ExitCode
		// returned by int main or exit(n) does not turn the run into an engine failure.
		bool bSuccess = false;
		// On success: the process result from int main()/exit(n). On failure:
		// 1 VM/runtime, 2 IO/CLI, 3 compile/parse, or another API-specific code.
		int ExitCode = 1;
		// True when script called exit(n) (including n==0). Distinguishes exit(0) from normal success.
		bool bExitRequested = false;
		// Primary / first failure (CLI + legacy consumers). Same as Diagnostics.front() when non-empty.
		FScriptError Error;
		// Collected failures (parse recovery may report several; often size 1).
		std::vector<FScriptError> Diagnostics;
	};

	/**
	 * Optional run/check settings for embedding and CLI.
	 * ImportPaths: extra module search paths.
	 * ScriptArgs: passed to main(string[] args) for RunSource/RunFile.
	 * bUseBytecodeCache: only affects RunFile; default false for embed-safe Host API
	 * (ostream RunFile still defaults to true for CLI).
	 * InputStream: optional stdin for read_line; null uses std::cin.
	 * Host owns the pointed stream; it must outlive every Run that may call read_line
	 * until ApplyConfig sets a non-null InputStream (which clears owned input) or
	 * Session takes ownership via SetOwnedInputStream / SetInputFromString.
	 * SourceOverlays: open-buffer contents that override disk during import load.
	 * MaxCallDepth: zero resolves VERO_MAX_CALL_DEPTH, then the built-in default.
	 * ExecutionLimits: optional cooperative instruction/time limits. Supplying
	 * either a non-zero limit or ExecutionControl requires artifacts with complete
	 * governance support. Missing capabilities select the VM before entry.
	 * ExecutionControl: optional, thread-safe host cancellation handle. It is not
	 * reset automatically; call Reset before reusing it for another run.
	 * Capabilities: default-all permission policy for standard-library side
	 * effects. Restricted execution requires permission-aware Core services on
	 * every selected backend. Governed Mixed native code is opt-in via FJitPolicy.
	 */
	struct FEngineConfig
	{
		std::vector<std::string_view> ImportPaths;
		// Conditional-compilation / object-like macros (`-DNAME` / `-DNAME=value`).
		std::vector<std::string_view> Defines;
		// Structured function-like macros supplied by the embedding host.
		std::vector<FPredefinedMacro> PredefinedMacros;
		std::vector<std::string_view> ScriptArgs;
		bool bUseBytecodeCache = false;
		/**
		 * Empty = VERO_CACHE_DIR, then the platform user cache, with a per-script
		 * `.verocache` fallback when no usable user location is available.
		 */
		std::string_view BytecodeCacheDirectory;
		// Extra stdlib search roots (prepended before built-in defaults).
		std::vector<std::string_view> StdlibSearchPaths;
		std::istream* InputStream = nullptr;
		std::vector<FSourceOverlay> SourceOverlays;
		// Interpreter / mixed / jit. Default Mixed (demand JIT; fall back to Vm).
		EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed;
		// Per-execution override. 0 = VERO_MAX_CALL_DEPTH, then the safe default.
		std::size_t MaxCallDepth = 0;
		/**
		 * Modules implicitly imported by every source module compiled for this run.
		 * Equivalent to ordinary `import`, without modifying or re-exporting source.
		 * Consumers also receive public macros before preprocessing. Provider macro
		 * interfaces are established first through their explicit dependencies.
		 *
		 * Kept at the end to preserve positional aggregate initialization of the
		 * pre-existing public configuration.
		 */
		std::vector<std::string_view> ImplicitImportPaths;
		// Permit cyclic module graphs. Disable for projects that require strict DAG imports.
		bool bAllowCircularImports = true;
		// Share non-exported declarations within entry + ImplicitImportPaths.
		bool bAllowProjectWideAccess = false;
		FExecutionLimits ExecutionLimits;
		std::shared_ptr<FExecutionControl> ExecutionControl;
		FScriptCapabilities Capabilities;
		// Immutable selection shared with workers; each VM creates its own instance.
		std::shared_ptr<const IJitBackendFactory> JitBackendFactory;
		// Prevent environment defaults from enabling an optional JIT backend.
		bool bDisableJitBackend = false;
		// Static, application-linked code. Independent of the dynamic JIT factory.
		std::shared_ptr<const IAotProgram> AotProgram;
		FMemoryLimits MemoryLimits;
		FAnnotationProcessingOptions AnnotationProcessing;
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		// App resources are independent from the process diagnostic locale.
		// The service selection is inherited by workers; zero output limit is unlimited.
		std::shared_ptr<I18n::FI18nServices> I18nServices;
		I18n::FI18nLimits I18nLimits;
		// Explicit immutable module/resource/native contract supplied by the host.
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
		FStandardLibraryLimits StandardLibraryLimits;
		FNumericLimits NumericLimits;
		// Core has no implicit interpreter. Factory and explicit path are exclusive.
		std::shared_ptr<const IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
		// Product-provided interpreter used only when the environment has no
		// usable candidate. Explicit selections and disables take precedence.
		std::shared_ptr<const IVmBackendFactory> FallbackVmBackendFactory;
		std::string FallbackVmBackendPath;
		// Omitted selection preserves the Session's domain; explicit clear is distinct.
		bool bClearVmBackend = false;
	};

	/**
	 * Cooperative cancellation token shared by an embedding host and one or more
	 * VMs. RequestCancel is thread-safe; it never forcefully terminates a host
	 * thread or a native callback.
	 */
	class FExecutionControl final
	{
	public:
		void RequestCancel() noexcept
		{
			bCancellationRequested.store(true, std::memory_order_release);
		}

		void Reset() noexcept
		{
			bCancellationRequested.store(false, std::memory_order_release);
		}

		bool IsCancellationRequested() const noexcept
		{
			return bCancellationRequested.load(std::memory_order_acquire);
		}

	private:
		std::atomic<bool> bCancellationRequested{false};
	};
}
