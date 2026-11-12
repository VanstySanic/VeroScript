// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRun.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitDiagnostics.h"
#include "VeroScriptVmBackend.h"
#include "VeroScriptToolingTypes.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VeroScript
{
	struct FBytecodeModule;
	class VeroScriptDebugger;
}

namespace VeroScript::Tooling
{
	/**
	 * Protocol-agnostic debug session for IDE adapters (DAP etc.).
	 * Compiles source, owns breakpoints / step mode, runs with a synchronous stop callback.
	 * Requested EExecutionBackend / FJitPolicy are applied to the VM before Run; attaching
	 * the debugger still forces the effective backend to Vm and invalidates JIT
	 * (same contract as CLI --debug / embed).
	 * Inspect or edit paused VM state from the stop callback on the Run thread;
	 * host getters and memory inspection must obey the execution thread contract.
	 * Serialize lifecycle operations on the owning thread. While Run is active,
	 * including its callbacks, loads and nested Run are rejected and Clear is a no-op.
	 */
	class VeroScriptDebugSession
	{
	public:
		using FStopHandler = std::function<void(VeroScriptDebugSession&, EDebugStopReason)>;

		/** Receives output as it is flushed by the debugged VM.  bIsError distinguishes
		 * stderr from stdout; Run() still captures both streams in FDebugRunResult. */
		using FOutputHandler = std::function<void(std::string_view Text, bool bIsError)>;

		struct FBreakpointBinding
		{
			int Id = 0;
			std::uint32_t Line = 0; // Physical source line when bound.
			std::string Message;
		};

		VeroScriptDebugSession();
		// Core has no implicit interpreter. Select a VM before execution begins.
		// Clear/reload preserve this domain and do not thaw its backend selection.
		bool SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error);
		bool SetVmBackendPath(std::string_view Path, std::string& Error);
		std::shared_ptr<FVmExecutionDomain> GetVmExecutionDomain() const;

		~VeroScriptDebugSession();

		VeroScriptDebugSession(const VeroScriptDebugSession&) = delete;

		VeroScriptDebugSession& operator=(const VeroScriptDebugSession&) = delete;

		// A failed load clears the compiled program, source maps and breakpoints.
		// Run cannot execute an older program with the failed load's configuration.
		// Cancellation/other load exceptions propagate after clearing that state.
		// A load rejected during an active Run preserves the current session.
		FDebugLoadResult LoadSource(
			std::string_view Source,
			std::string_view FileLabel = "<source>",
			const FDebugLoadOptions& Options = {});

		FDebugLoadResult LoadFile(
			std::string_view FilePath,
			const FDebugLoadOptions& Options = {});

		// Does nothing during an active Run; clear after Run returns or unwinds.
		void Clear();

		bool IsLoaded() const;
		// Read-only generated source captured during the successful load. Does not
		// recompile or execute processors; unavailable after Clear/a failed load.
		bool TryGetGeneratedSource(std::string_view FileLabel, std::string& OutSource) const;
		// Frozen in-memory or generated source from the loaded compilation.
		bool TryGetSource(std::string_view FileLabel, std::string& OutSource) const;
		// Convert a one-based physical UTF-8 byte column using the loaded snapshot.
		// ASCII/unknown source locations retain their column; never reads changed disk files.
		std::uint32_t GetUtf16Column(std::string_view FileLabel, std::uint32_t Line,
			std::uint32_t ByteColumn) const;

		int SetBreakpoint(
			std::string FileLabel,
			std::uint32_t Line,
			std::string Condition = {});

		// Resolve a physical source line against executable bytecode locations.
		// Unlike SetBreakpoint, an unresolvable request is not registered.
		FBreakpointBinding BindBreakpoint(
			std::string_view SourcePath,
			std::uint32_t PhysicalLine,
			std::string Condition = {});

		void ClearBreakpoints();

		// Replacing or clearing the handler affects later stops. An active handler
		// retains its captures until it returns, including mutable callback state.
		void SetStopHandler(FStopHandler Handler);

		void SetOutputHandler(FOutputHandler Handler);

		void SetRunMode(EDebugRunMode Mode);

		EDebugRunMode GetRunMode() const;

		// Preferred execution backend (vm / mixed / jit). Debugger attach forces Vm.
		void SetExecutionBackend(EExecutionBackend Backend);

		EExecutionBackend GetExecutionBackend() const;

		// Per-run recursion/resource guard. 0 reloads VERO_MAX_CALL_DEPTH/default.
		void SetMaxCallDepth(std::size_t Limit);

		std::size_t GetMaxCallDepth() const;

		/** Adaptive JIT thresholds/budgets applied on the next Run.
		 * Debugger attach keeps the policy but forces effective Vm and invalidates the JIT cache.
		 * Debug runs do not compile or dispatch through an optional JIT backend. */
		void SetJitPolicy(FJitPolicy Policy);

		const FJitPolicy& GetJitPolicy() const;

		/** Snapshot from the most recent Run() (zeros until the first run; cleared by Clear()).
		 * CompileAttempts and Dispatches remain zero while debugging (effective backend is Vm).
		 * CacheInvalidations is not a count of debugger lifecycle operations. */
		const FJitStats& GetLastJitStats() const;

		// Optional per-function snapshot from the last executed Run, retained after
		// its temporary VM is destroyed. Empty with diagnostics disabled or after Clear.
		const FJitDiagnostics& GetLastJitDiagnostics() const;

		// Run snapshots these strings; changes in a callback apply to the next Run.
		void SetScriptArguments(std::vector<std::string> Arguments);

		void SetEnvironment(std::vector<std::pair<std::string, std::string>> Environment);

		void SetWorkingDirectory(std::string Directory);

		void SetInputFromString(std::string Input);

		void ResetRunControl() noexcept;

		// Thread-safe cooperative controls used by asynchronous DAP hosts.
		void RequestPause() noexcept;

		void RequestCancel() noexcept;

		std::vector<FDebugStackFrame> GetStackTrace() const;

		std::vector<FDebugVariable> GetLocals(std::size_t FrameIndex = 0) const;

		std::vector<FDebugVariable> GetChildren(const FDebugVariable& Variable) const;

		FDebugEvaluateResult Evaluate(
			std::string_view Expression,
			std::size_t FrameIndex = 0) const;

		FDebugEvaluateResult SetLocal(
			std::string_view Name,
			std::string_view ValueExpression,
			std::size_t FrameIndex = 0);

		FDebugEvaluateResult SetChild(
			const FDebugVariable& Variable,
			std::string_view ValueExpression,
			std::size_t FrameIndex = 0);

		// Execute loaded bytecode. Stop handler runs synchronously while paused.
		FDebugRunResult Run();

	private:
		struct FState;
		std::unique_ptr<FState> State;
	};
}
