// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDapStdio.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace VeroScript::Tooling
{
	/**
	 * Stop control for one active TCP Run call. RequestStop is thread-safe, idempotent,
	 * and also prevents later Run calls with this control from starting.
	 */
	class FDapTcpRunControl
	{
	public:
		FDapTcpRunControl();
		~FDapTcpRunControl();
		FDapTcpRunControl(const FDapTcpRunControl&) = delete;
		FDapTcpRunControl& operator=(const FDapTcpRunControl&) = delete;

		void RequestStop() noexcept;
		bool IsStopRequested() const noexcept;

	private:
		friend class VeroScriptDapTcp;
		struct FState;
		std::unique_ptr<FState> State;
	};

	/**
	 * TCP DAP host for embed processes: listen → accept client(s) → same Content-Length
	 * protocol as VeroScriptDapStdio (and the same LiveBindings preference).
	 *
	 * Typical host:
	 *   Session.RegisterNative(...);
	 *   FDapTcpRunOptions Opt;
	 *   Opt.Port = 4711;
	 *   Opt.MaxSessions = 0;  // unlimited until process exit
	 *   Opt.Stdio.LiveBindings = &Session.GetBindingCatalog();
	 *   VeroScriptDapTcp::Run(Opt);
	 *
	 * IDE: Debug → Attach to Host sends DAP attach + configurationDone (not launch).
	 * Stop/disconnect with terminateDebuggee=false keeps MaxSessions listening;
	 * terminateDebuggee=true stops the listen loop.
	 */
	struct FDapTcpRunOptions
	{
		// Bind address (IPv4). Empty / "0.0.0.0" = all interfaces; prefer "127.0.0.1".
		std::string BindAddress = "127.0.0.1";
		// TCP port. 0 = ephemeral (actual port reported via OnListening).
		std::uint16_t Port = 4711;
		/**
		 * How many client sessions to serve before returning.
		 * 1 = single attach (CLI default); 0 = unlimited (embed dap-tcp demo).
		 */
		int MaxSessions = 1;
		/**
		 * When true, print "listening on …" to stderr so hosts/CI can wait for readiness.
		 * Default true for CLI; embed hosts may set false.
		 */
		bool bAnnounceListen = true;
		// Invoked after listen succeeds with the bound port (useful when Port == 0).
		std::function<void(std::uint16_t BoundPort)> OnListening;
		FDapStdioRunOptions Stdio;
		// Optional host stop signal. Join the Run thread before releasing host services.
		// A live domain is detached and resumed under its existing disconnect contract.
		std::shared_ptr<FDapTcpRunControl> Control;
	};

	class VeroScriptDapTcp
	{
	public:
		/**
		 * Listen and serve MaxSessions DAP clients (or until a session fails fatally).
		 * Returns 0 when sessions complete cleanly, a client requests host termination,
		 * or the host requests stop. Returns non-zero on endpoint or protocol failure.
		 */
		static int Run(const FDapTcpRunOptions& Options);
		// Domain must outlive the listening loop and each connected client session.
		static int Run(
			const FDapTcpRunOptions& Options,
			IVeroScriptLiveDebugDomain* LiveDomain);
	};
}
