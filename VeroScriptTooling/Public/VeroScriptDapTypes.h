// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include "VeroScriptMemory.h"
#include "VeroScriptRun.h"
#include "VeroScriptToolingTypes.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript::Tooling
{
	/**
	 * In-process DAP-shaped types (no JSON / framing).
	 * Wire protocol adapters map these to/from DAP messages.
	 */

	enum class EDapStopReason : std::uint8_t
	{
		Breakpoint = 0,
		Step,
		Entry,
		Pause,
	};

	enum class EDapEventKind : std::uint8_t
	{
		Initialized = 0,
		Stopped,
		Output,
		Exited,
		Terminated,
		Breakpoint,
	};

	struct FDapCapabilities
	{
		bool bSupportsConfigurationDoneRequest = true;
		bool bSupportsStepInRequest = true;
		bool bSupportsStepOutRequest = true;
		bool bSupportsStepBackRequest = false;
		bool bSupportsConditionalBreakpoints = true;
		bool bSupportsEvaluateRequest = true;
		bool bSupportsSetVariable = true;
		bool bSupportsTerminateRequest = true;
	};

	struct FDapInitializeResult
	{
		bool bSuccess = true;
		FDapCapabilities Capabilities;
		// Engine version string (same as CLI --version core portion).
		std::string Version;
		// Host triple, e.g. win-x64 / linux-arm64 / macos-x64.
		std::string Platform;
		/** Core JIT service-contract identifier (currently "backend-services").
		 * Does not indicate whether a native backend is installed or selected. */
		std::string JitCapability;
	};

	struct FDapLaunchArgs
	{
		std::string Source;
		// Used when Source is empty.
		std::string Path;
		std::string FileLabel = "<source>";
		std::vector<std::string> ImportPaths;
		std::vector<std::string> Defines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		// Open-buffer overlays for multi-file debug (import resolution).
		std::vector<FDocumentOverlay> SourceOverlays;
		bool bStopOnEntry = false;
		/**
		 * Preferred execution backend (`vm` / `mixed` / `jit`). Default Mixed.
		 * Debugger attachment still forces the effective backend to Vm.
		 */
		EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed;
		// 0 = VERO_MAX_CALL_DEPTH, then the engine default.
		std::size_t MaxCallDepth = 0;
		// Optional host binding catalog (not owned). Used for compile + Execute natives.
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		/**
		 * True when Bindings came from an in-process live registry (not a JSON stub catalog).
		 * Wire hosts may use this to skip the "stubs only" console warning.
		 */
		bool bLiveBindings = false;
		// Appended to preserve positional aggregate initialization of older callers.
		std::vector<std::string> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		// Passed to main(string[] args); the script path itself is not included.
		std::vector<std::string> ScriptArgs;
		// Standard-library roots retain standard-library visibility and localization semantics.
		std::vector<std::string> StdlibSearchPaths;
		// Launch-local process context. Environment entries override inherited values.
		std::vector<std::pair<std::string, std::string>> Environment;
		std::string WorkingDirectory;
		// Initial stdin payload. DAP protocol stdin is never exposed to the script.
		std::string Input;
		// Native host processors are supplied by the host, never by request JSON.
		FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
		std::shared_ptr<const VeroScript::IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
	};

	struct FDapLaunchResult
	{
		bool bSuccess = false;
		std::vector<FDiagnostic> Diagnostics;
	};

	struct FDapSourceBreakpoint
	{
		std::uint32_t Line = 1;
		std::string Condition;
	};

	struct FDapBreakpoint
	{
		int Id = 0;
		bool bVerified = false;
		std::uint32_t Line = 1;
		std::string Path;
		std::string Message;
	};

	struct FDapSetBreakpointsResult
	{
		bool bSuccess = true;
		std::vector<FDapBreakpoint> Breakpoints;
	};

	struct FDapEvaluateResult
	{
		bool bSuccess = false;
		std::string Result;
		std::string Error;
		int VariablesReference = 0;
	};

	struct FDapSetVariableResult
	{
		bool bSuccess = false;
		std::string Value;
		std::string Error;
		int VariablesReference = 0;
	};

	struct FDapStackFrame
	{
		int Id = 0;
		std::string Name;
		std::string Path;
		std::uint32_t Line = 1;
		std::uint32_t Column = 1; // One-based UTF-16 code units, including live-domain frames.
	};

	struct FDapStackTraceResult
	{
		bool bSuccess = true;
		std::vector<FDapStackFrame> StackFrames;
	};

	struct FDapScope
	{
		std::string Name;
		int VariablesReference = 0;
		bool bExpensive = false;
	};

	struct FDapScopesResult
	{
		bool bSuccess = true;
		std::vector<FDapScope> Scopes;
	};

	struct FDapVariable
	{
		std::string Name;
		std::string Value;
		FMemoryInspection Memory;
		int VariablesReference = 0;
		// Serialized as presentationHint.attributes = ["readOnly"].
		bool bReadOnly = false;
	};

	struct FDapVariablesResult
	{
		bool bSuccess = true;
		std::vector<FDapVariable> Variables;
	};

	struct FDapContinueResult
	{
		bool bSuccess = true;
		bool bAllThreadsContinued = true;
	};

	struct FDapEvent
	{
		EDapEventKind Kind = EDapEventKind::Initialized;
		EDapStopReason StopReason = EDapStopReason::Breakpoint;
		int ThreadId = 1;
		std::string Category;
		std::string Output;
		int ExitCode = 0;
		FDapBreakpoint Breakpoint;
	};
}
