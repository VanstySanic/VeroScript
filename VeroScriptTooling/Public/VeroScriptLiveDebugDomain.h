// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDapTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Tooling
{
	struct FLiveDebugSourceVersion
	{
		std::string Path;
		std::string Version;
	};

	struct FLiveDebugBreakpoint
	{
		int Id = 0;
		std::uint32_t Line = 1;
		std::string Condition;
	};

	struct FLiveDebugDomainInfo
	{
		std::string HostId;
		std::string DomainId;
		std::string Name;
		std::string SourceRevision;
		std::string BindingRevision;
		std::vector<FLiveDebugSourceVersion> Sources;
		bool bCanPause = false;
		bool bCanStep = false;
		bool bCanEvaluate = false;
		bool bCanUseConditionalBreakpoints = false;
		bool bCanSetVariable = false;
		bool bCanTerminate = false;
	};

	/**
	 * Host-owned, persistent script execution domain for a DAP live attach.
	 * The host prepares its debugger bridge before script execution. Methods may
	 * arrive on the protocol thread while a script is running; Info() must return
	 * a synchronized snapshot, and VM mutations must run at an execution safe point.
	 * In particular, this contract does not permit SetDebugger() on a VM already
	 * executing JIT code. The host owns stopped-frame and variable-reference
	 * invalidation whenever a call resumes, ends, or reloads its code.
	 * Connect() never starts or reloads a program. A completed call does not end
	 * the connection; later calls in the same domain remain covered by breakpoints.
	 * This first protocol shape exposes one active debug context (DAP thread 1).
	 * A host with concurrent script calls must serialize debugged calls within
	 * this domain until the adapter gains thread-aware stack and frame requests.
	 * Event callbacks must not block and must not be emitted from Connect().
	 * A failed Connect() leaves the domain disconnected and retains no callback.
	 * Capability flags in Info() must match the operations the host accepts;
	 * unsupported requests return bSuccess=false without changing VM state.
	 * SetBreakpoints replaces one source atomically, returns one result for each
	 * request in order, and preserves each supplied Id in later breakpoint events.
	 * It does not emit breakpoint events for that same update; the adapter emits
	 * the initial pending-breakpoint changes from the returned bindings.
	 * Disconnect() resumes a paused call according to host policy, removes
	 * session breakpoints and the event callback, and drains callbacks before
	 * returning. The adapter drains its dispatched Pause()/RequestCancel() calls
	 * before invoking Disconnect(), so those controls cannot target a later connection.
	 * Control dispatch must complete without waiting for Disconnect() or for work
	 * queued to the lifecycle thread that can be draining those calls.
	 * If bCanTerminate is true, RequestCancel() followed by
	 * Disconnect() must cancel the target according to host policy rather than
	 * turn that cancellation into a normal resume. The host must keep this
	 * object alive until the adapter has
	 * disconnected (including adapter destruction), and throughout a DAP server
	 * Run() call that borrows it.
	 */
	class IVeroScriptLiveDebugDomain
	{
	public:
		using FEventHandler = std::function<void(const FDapEvent&)>;
		virtual ~IVeroScriptLiveDebugDomain() = default;
		virtual FLiveDebugDomainInfo Info() const = 0;
		virtual bool Connect(FEventHandler Handler, std::string& OutError) = 0;
		virtual bool Activate(std::string& OutError) = 0;
		virtual void Disconnect() noexcept = 0;
		virtual FDapSetBreakpointsResult SetBreakpoints(
			std::string_view Path,
			const std::vector<FLiveDebugBreakpoint>& Breakpoints) = 0;
		virtual FDapContinueResult Resume(EDebugRunMode Mode) = 0;
		virtual FDapContinueResult Pause() = 0;
		virtual void RequestCancel() = 0;
		virtual FDapStackTraceResult StackTrace() const = 0;
		virtual FDapScopesResult Scopes(int FrameId) const = 0;
		// Preserve the core debugger's bReadOnly status on returned variables.
		// Presentation hints do not replace the host's SetVariable validation.
		virtual FDapVariablesResult Variables(int VariablesReference) const = 0;
		virtual FDapEvaluateResult Evaluate(
			std::string_view Expression, int FrameId, std::string_view Context) const = 0;
		virtual FDapSetVariableResult SetVariable(
			int VariablesReference, std::string_view Name, std::string_view Value) = 0;
		virtual bool IsStopped() const = 0;
		virtual bool IsContextRunning() const = 0;
		// Optional immutable generated or memory-module source owned by the host.
		// Called with the same serialized stopped-state rules as StackTrace().
		virtual bool TryGetGeneratedSource(std::string_view, std::string& OutSource) const
		{
			OutSource.clear();
			return false;
		}
	};
}
