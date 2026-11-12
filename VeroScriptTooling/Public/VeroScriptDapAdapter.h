// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDapTypes.h"
#include "VeroScriptDebugSession.h"
#include "VeroScriptLiveDebugDomain.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VeroScript::Tooling
{
	/**
	 * Thin in-process DAP adapter over VeroScriptDebugSession.
	 * No JSON / stdio framing — hosts map wire DAP to these calls.
	 *
	 * For live host natives, set FDapLaunchArgs::Bindings to
	 * &Session.GetBindingCatalog() (same process as RegisterNative).
	 * Out-of-process IDE DAP uses bindingCatalogPaths stubs instead.
	 *
	 * Lifecycle: Initialize → Launch|Attach|AttachDomain → SetBreakpoints* → ConfigurationDone.
	 * For program loading, in-process ConfigurationDone blocks through Run; wire hosts execute it
	 * on a worker so pause/terminate and stopped-state requests remain responsive.
	 * Attach loads a program the same way as Launch; it is the
	 * wire-protocol request used when the IDE connects to an embed/TCP host that
	 * already holds LiveBindings (not mid-run VM attach).
	 * Continue / Next / StepIn / StepOut / StackTrace / Scopes / Variables are valid
	 * while the adapter reports IsStopped(). Pause and Terminate are thread-safe
	 * cooperative controls for an executing VM.
	 * Hosts must serialize stopped-state queries/edits with resume and disconnect.
	 * For program loading, service queries/edits on the Run thread inside the Stopped
	 * handler; memory regions and host callbacks retain their thread affinity.
	 * VeroScriptDapStdio marshals protocol requests to that paused execution thread.
	 * Wire hosts should emit DAP `initialized` after the initialize response (not from Initialize()).
	 * AttachDomain connects a host-owned persistent domain. Its host bridge must
	 * serialize VM operations at safe points; no Run() worker is started here.
	 * Disconnect() while Run is active only resumes and clears after Run returns.
	 * For a live domain, Disconnect drains dispatched pause/terminate controls
	 * before releasing the host connection. Controls arriving while it closes fail.
	 * The host still serializes attach/resume/disconnect and stopped-state access.
	 * Controls are dispatched to a live domain only after attachment succeeds.
	 */
	class VeroScriptDapAdapter
	{
	public:
		using FEventHandler = std::function<void(const FDapEvent&)>;

		VeroScriptDapAdapter();
		~VeroScriptDapAdapter();

		VeroScriptDapAdapter(const VeroScriptDapAdapter&) = delete;
		VeroScriptDapAdapter& operator=(const VeroScriptDapAdapter&) = delete;

		// Replace on the serialized owner/callback thread. An active delivery keeps
		// its handler alive; subsequent events use the replacement.
		void SetEventHandler(FEventHandler Handler);

		FDapInitializeResult Initialize();
		FDapLaunchResult Launch(const FDapLaunchArgs& Args);
		/**
		 * Same load semantics as Launch. Prefer this for TCP/embed IDE Attach so the
		 * wire request is DAP `attach` rather than `launch`.
		 */
		FDapLaunchResult Attach(const FDapLaunchArgs& Args);
		/** Connect to a host-prepared domain without loading or executing a program. */
		FDapLaunchResult AttachDomain(
			IVeroScriptLiveDebugDomain& Domain, std::string_view ExpectedDomainId);
		bool IsDomainAttached() const;
		FLiveDebugDomainInfo AttachedDomainInfo() const;

		// Replace breakpoints for Path (other paths kept). Verified when session is loaded.
		FDapSetBreakpointsResult SetBreakpoints(
			std::string_view Path,
			const std::vector<FDapSourceBreakpoint>& Breakpoints);

		/**
		 * Starts execution (blocks until the program finishes). Protocol hosts must
		 * call this on an execution worker, as VeroScriptDapStdio does.
		 * Emits Stopped (from handler), then Output / Exited / Terminated.
		 */
		FDebugRunResult ConfigurationDone();

		FDapContinueResult Continue();
		FDapContinueResult Next();
		FDapContinueResult StepIn();
		FDapContinueResult StepOut();
		FDapContinueResult Pause();
		// Cooperatively cancels the running VM. Safe to call from the protocol thread.
		void Terminate();

		FDapStackTraceResult StackTrace() const;
		bool TryGetGeneratedSource(std::string_view FileLabel, std::string& OutSource) const;
		bool TryGetSource(std::string_view FileLabel, std::string& OutSource) const;
		// Frame ids and variablesReference values are opaque, not stack indices.
		// They expire on resume/reload/disconnect; editing a value also expires
		// child pages. Obtain current ids from StackTrace/Scopes/Variables again.
		FDapScopesResult Scopes(int FrameId) const;
		FDapVariablesResult Variables(int VariablesReference) const;
		FDapEvaluateResult Evaluate(
			std::string_view Expression,
			int FrameId,
			std::string_view Context = {}) const;
		FDapSetVariableResult SetVariable(
			int VariablesReference,
			std::string_view Name,
			std::string_view Value);

		void Disconnect();

		bool IsLoaded() const;
		bool IsStopped() const;
		bool IsRunning() const;

		static constexpr int MainThreadId = 1;

	private:
		struct FState;

		void Emit(FDapEvent Event) const;

		void ReapplyBreakpoints();

		void ClearSessionState();

		FDapContinueResult Resume(EDebugRunMode Mode);

		EDapStopReason MapStopReason(EDebugStopReason Reason);

		std::unique_ptr<FState> State;

	};
}
