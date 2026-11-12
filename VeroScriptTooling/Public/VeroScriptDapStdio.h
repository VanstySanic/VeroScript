// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptScriptCompileExtensions.h"

#include "VeroScriptHostBinding.h"
#include "VeroScriptRun.h"

#include <atomic>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace VeroScript::Tooling
{
	class IVeroScriptLiveDebugDomain;
	/**
	 * Optional CLI/host defaults applied when a launch/program-attach request omits fields.
	 *
	 * Binding resolution order on `launch` / program-mode `attach`:
	 * 1. `LiveBindings` (in-process host registry — real Callbacks)
	 * 2. request JSON `bindingCatalogPaths`
	 * 3. `DefaultBindingCatalogPaths`
	 *
	 * Catalog files use stub natives (IDE/out-of-process). LiveBindings is how
	 * embed hosts debug scripts that call RegisterNative / RegisterHostMember.
	 *
	 * IDE: F6/local debug uses `launch` + catalog stubs; program-mode TCP `attach`
	 * can use LiveBindings on the embed/CLI DAP server. These paths load a new program.
	 * Connecting an existing runtime instead requires the IVeroScriptLiveDebugDomain
	 * Run overload and `attachMode: "domain"`; LiveBindings alone does not do this.
	 */
	struct FDapStdioRunOptions
	{
		std::string DefaultProgram;
		std::vector<std::string> DefaultImportPaths;
		std::vector<std::string> DefaultDefines;
		std::vector<VeroScript::FPredefinedMacro> DefaultPredefinedMacros;
		// Used when launch/attach omits `backend` (debugger still forces effective Vm).
		EExecutionBackend DefaultExecutionBackend = EExecutionBackend::Mixed;
		// Used when launch/attach omits `maxCallDepth`; 0 resolves env/default.
		std::size_t DefaultMaxCallDepth = 0;
		// Ordered default catalogs merged when a request omits bindingCatalogPaths.
		std::vector<std::string> DefaultBindingCatalogPaths;
		/**
		 * Non-owned live catalog from the host process (e.g. &Session.GetBindingCatalog()).
		 * When set, catalog JSON is ignored and host natives execute for real.
		 * Must outlive VeroScriptDapStdio::Run.
		 */
		const VeroScript::IHostBindingCatalog* LiveBindings = nullptr;
		// Appended to preserve positional aggregate initialization of older callers.
		std::vector<std::string> DefaultImplicitImportPaths;
		bool bDefaultAllowCircularImports = true;
		bool bDefaultAllowProjectWideAccess = false;
		std::vector<std::string> DefaultScriptArgs;
		std::vector<std::string> DefaultStdlibSearchPaths;
		std::vector<std::pair<std::string, std::string>> DefaultEnvironment;
		std::string DefaultWorkingDirectory;
		std::string DefaultInput;
		VeroScript::FAnnotationProcessingOptions AnnotationProcessing;
		VeroScript::FCompileProcessingOptions CompileProcessing;
		VeroScript::FScriptCompileExtensionsOptions ScriptCompileExtensions;
		std::shared_ptr<const VeroScript::FModuleEnvironment> ModuleEnvironment;
		std::shared_ptr<const VeroScript::IVmBackendFactory> VmBackendFactory;
		std::string VmBackendPath;
	};

	/**
	 * Stdio DAP host: Content-Length framing + JSON over istream/ostream.
	 * Drives VeroScriptDapAdapter on an execution worker so the protocol reader can
	 * process pause, terminate and stopped-state requests throughout a run.
	 *
	 * For newly loaded programs, embed hosts can supply real native callbacks through
	 * FDapStdioRunOptions::LiveBindings; IDE tooling can use bindingCatalogPaths stubs.
	 * For an existing persistent runtime, pass its IVeroScriptLiveDebugDomain to Run
	 * and require veroscriptSupportsDomainAttach=true before sending domain attach.
	 *
	 * Exit codes:
	 * - 0 — clean session end (TCP may accept another client when MaxSessions allows)
	 * - 1 — protocol/IO failure
	 * - 2 — client requested host terminate (`disconnect` with terminateDebuggee=true)
	 */
	class VeroScriptDapStdio
	{
	public:
		// See class comment for exit codes.
		static int Run(
			std::istream& In,
			std::ostream& Out,
			const FDapStdioRunOptions& Options = {});
		// Host-prepared domain is borrowed for Run(); existing Run options stay unchanged.
		static int Run(
			std::istream& In,
			std::ostream& Out,
			const FDapStdioRunOptions& Options,
			IVeroScriptLiveDebugDomain* LiveDomain);

		static constexpr int ExitOk = 0;
		static constexpr int ExitError = 1;
		static constexpr int ExitTerminateHost = 2;

	private:
		friend class VeroScriptDapTcp;
		static int Run(
			std::istream& In,
			std::ostream& Out,
			const FDapStdioRunOptions& Options,
			IVeroScriptLiveDebugDomain* LiveDomain,
			const std::atomic<bool>* StopRequested);
	};
}
