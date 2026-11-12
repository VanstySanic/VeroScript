// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptInterop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Mobile
{
	// Uses process locale initialization via the C ABI; mobile hosts load catalogs explicitly.
	// Unknown messages are copied unchanged.
	std::string TranslateMessage(std::string_view EnglishMessage);

	struct FSourceResource
	{
		std::string Path;
		std::string Source;
	};

	struct FAotProgramRegistration
	{
		std::string Id;
		// Create with RetainAotProgram; never construct an owner from a borrowed handle.
		std::shared_ptr<vs_interop_aot_program> Program;
	};
	std::shared_ptr<vs_interop_aot_program> RetainAotProgram(const vs_interop_aot_program* Program);
	struct FExecutionResult
	{
		bool bSuccess = false;
		bool bOutputTruncated = false;
		std::int32_t ExitCode = 0;
		std::string Output;
		std::string Diagnostics;
		std::string Error;
	};

	class FAotProgramCatalog final
	{
	public:
		explicit FAotProgramCatalog(std::vector<FAotProgramRegistration> Programs);
		const vs_interop_aot_program* Find(std::string_view Id) const noexcept;
	private:
		std::vector<FAotProgramRegistration> Programs;
	};

	struct FSessionOptions
	{
		// Absolute application paths, or absolute virtual paths backed by Sources.
		std::string StdlibDirectory;
		// Empty disables persistent cache; no environment or cwd fallback is used.
		std::string CacheDirectory;
		std::vector<FSourceResource> Sources;
		std::uint64_t MaxInstructions = 1000000;
		std::uint64_t TimeoutMilliseconds = 100;
		std::uint64_t MaxCallDepth = 256;
		std::size_t MaxCapturedOutputBytes = 1024 * 1024;
		std::size_t MaxSourceBytes = 16 * 1024 * 1024;
		std::size_t MaxProgramBytes = 64 * 1024 * 1024;
		std::uint64_t MaxBufferBytes = 16 * 1024 * 1024;
		std::uint64_t MaxRetainedBytes = 32 * 1024 * 1024;
		vs_interop_execution_backend ExecutionBackend = VS_INTEROP_BACKEND_VM;
		// Explicit absolute file/directory. Android hosts must deploy an accessible .so.
		std::string JitBackendPath;
		std::string AotProgramId;
		std::shared_ptr<const FAotProgramCatalog> AotPrograms;
		// Empty keeps the product's statically assembled VM. Otherwise an absolute native file/directory.
		std::string VmBackendPath;
	};

	/**
	 * A small mobile embedding policy over the existing C ABI. Construction,
	 * Execute and destruction belong to one worker/owner thread. Direct mobile
	 * hosts must provide at least 8 MiB of native stack for source compilation.
	 * FRunner supplies that stack when platform adapters use a small caller stack.
	 * RequestCancel may run on another thread while the caller keeps this object alive.
	 * No UI dispatch occurs here. Host callbacks remain responsible for their
	 * own authority and cooperative cancellation; this is not a memory sandbox.
	 */
	class FSession final
	{
	public:
		explicit FSession(FSessionOptions Options);
		~FSession();
		FSession(const FSession&) = delete;
		FSession& operator=(const FSession&) = delete;

		// Cancellation remains latched for this session, as in the underlying C ABI.
		// Relative imports share the same absolute labels as resource overlays.
		FExecutionResult Execute(std::string_view Source, std::string_view FileLabel);
		FExecutionResult ExecutePackage(vs_interop_byte_view Package, std::string_view FileLabel);
		void RequestCancel() noexcept;
		// Borrowed owner-thread handle for advanced existing C ABI host bindings.
		vs_interop_session* GetNativeSession() const;

	private:
		FExecutionResult ExecuteImpl(std::string_view Input, std::string_view FileLabel, bool bPackage);
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	/**
	 * Platform adapters use a fresh owner-thread session per Execute. Calls may
	 * originate on any worker thread, but concurrent Execute calls are rejected.
	 * On Android/Apple a small caller stack uses a joined compiler worker;
	 * the session is created, executed and destroyed on that worker throughout.
	 * Cancellation is safe during creation/compilation/execution. Destruction
	 * must wait for Execute and RequestCancel to return (the adapters enforce it).
	 */
	class FRunner final
	{
	public:
		explicit FRunner(FSessionOptions Options);
		~FRunner();
		FExecutionResult Execute(std::string_view Source, std::string_view FileLabel);
		FExecutionResult ExecutePackage(vs_interop_byte_view Package, std::string_view FileLabel);
		void RequestCancel() noexcept;
		bool IsExecuting() const noexcept;

	private:
		FExecutionResult ExecuteImpl(std::string_view Input, std::string_view FileLabel, bool bPackage);
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
}
