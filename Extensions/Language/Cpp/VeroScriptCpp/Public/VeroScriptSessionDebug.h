// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRun.h"
#include "VeroScriptValue.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace VeroScript
{
	struct FBytecodeModule;
	class IHostBindingCatalog;
	class VeroScriptDebugger;
	class FModuleEnvironment;

	enum class ESessionDebugExecutionKind : std::uint8_t
	{
		Run,
		ConstructObject,
		CallMethod,
		CallFunction,
		InvokeCallable,
		ApplyClassDefaults,
	};

	/** Borrowed owner-thread view; copy needed source/binding data before returning. */
	struct FSessionDebugProgramView
	{
		const FBytecodeModule* Module = nullptr;
		const IHostBindingCatalog* Bindings = nullptr;
		// Current compile configuration, not a frozen copy of compiled source.
		// Source-free packages may expose only Module's source labels/debug lines.
		const std::vector<FSourceOverlay>* SourceOverlays = nullptr;
		std::string_view FilePath;
		std::uint64_t Generation = 0;
		// Identity only: retained callable values can outlive the Session.
		std::weak_ptr<const void> Owner;
		// Borrowed snapshot containing mapped memory sources and provider contracts.
		// Copy needed data, or retain Session::GetModuleEnvironment on its owner thread.
		const FModuleEnvironment* ModuleEnvironment = nullptr;
	};

	struct FSessionDebugExecutionContext
	{
		FSessionDebugProgramView Program;
		ESessionDebugExecutionKind Kind = ESessionDebugExecutionKind::Run;
		std::string_view EntryName;
		FObjectHandle Object;
		std::thread::id ThreadId;
		std::shared_ptr<FExecutionControl> ExecutionControl;
		// Relevant to Kind == Run; Eval/EvalOnce execute module initialization.
		ERunMode RunMode = ERunMode::Script;
	};

	/**
	 * Native Session boundary for a host/Tooling-owned live debug domain.
	 * One bridge belongs to one Session. All callbacks run synchronously on the
	 * Session's controlling thread and must not re-enter Session APIs that mutate
	 * or execute it. There is no protocol thread or network service in this layer.
	 *
	 * BeginExecution runs before VM entry (including JIT preparation). Return a
	 * shared debugger to interpret this call, or nullptr to leave it undebugged.
	 * The Session retains that debugger through execution and detaches its VM
	 * before EndExecution, including C++ exception unwinding. Each
	 * BeginExecution callback has one EndExecution callback, even when it returns
	 * nullptr. EndExecution invalidates stopped frames and variable references.
	 * Views/contexts are borrowed only for the callback; never retain raw pointers
	 * to the VM, program, bindings, source overlays, or entry name across calls.
	 *
	 * The debugger's stop callback runs on the executing thread. A live-domain
	 * implementation must pump queued inspect/evaluate/mutate/resume requests
	 * there while paused. Only RequestPause and cancellation may be called directly
	 * from a protocol thread. Use a shared FExecutionControl's RequestCancel, or
	 * Session::RequestCancel when the Session's lifetime is guaranteed. It must not
	 * replace a debugger in a running VM. Spawned script workers are not debug
	 * contexts of this bridge; do not share this debugger with their VMs.
	 *
	 * ProgramChanged invalidates code-dependent state after load/reload/rollback/
	 * Clear and when installed. Failed compilation leaves the program unchanged.
	 * Binding changes are visible in each BeginExecution context; callers using
	 * the mutable binding registry must publish their own binding revisions.
	 * Detached is called once on replacement/removal or before Session destruction
	 * releases script state. Drain event callbacks and remove registrations there;
	 * never wait for work queued to this same controlling thread. Teardown script
	 * destructors are deliberately not debugged, so cleanup cannot stop at a breakpoint.
	 *
	 * Disconnecting DAP is the bridge's operation: release a paused stop callback
	 * and disable its debug state without destroying the Session or cancelling its
	 * execution. Cancellation is a separate explicit host operation. Before host
	 * destruction: disconnect/cancel as appropriate, await execution and protocol
	 * shutdown, then destroy the Session on its controlling thread.
	 */
	class ISessionDebugBridge
	{
	public:
		virtual ~ISessionDebugBridge() = default;
		virtual void ProgramChanged(const FSessionDebugProgramView& Program) noexcept = 0;
		virtual std::shared_ptr<VeroScriptDebugger> BeginExecution(
			const FSessionDebugExecutionContext& Context) noexcept = 0;
		virtual void EndExecution() noexcept = 0;
		virtual void Detached() noexcept = 0;
	};
}
