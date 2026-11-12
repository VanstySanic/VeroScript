// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJit.h"
#include "VeroScriptRun.h"
#include "VeroScriptJitRuntimeServices.h"
#include "VeroScriptVersion.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace VeroScript
{
	enum class EJitCompilationResult : std::uint8_t
	{
		Ready,
		Unsupported,
		Deferred,
		Failed,
	};

	enum class EJitInvocationResult : std::uint8_t
	{
		NotEntered,
		Returned,
		ControlTransfer,
		Failed,
	};

	struct FJitProgramIdentity
	{
		bool operator==(const FJitProgramIdentity& Other) const noexcept
		{
			return Program == Other.Program && Generation == Other.Generation
				&& NativeBindingsGeneration == Other.NativeBindingsGeneration;
		}

		bool operator!=(const FJitProgramIdentity& Other) const noexcept
		{
			return !(*this == Other);
		}

		std::uint64_t Program = 0;
		std::uint64_t Generation = 0;
		std::uint64_t NativeBindingsGeneration = 0;
	};

	struct FJitProgramView
	{
		// Borrowed modules restrict the complete cache to the caller's borrow scope.
		const FBytecodeModule* Module = nullptr;
		std::shared_ptr<const FBytecodeModule> Owner;
		FJitProgramIdentity Identity;
	};

	inline constexpr std::uint64_t JitNativeCodeCapability = 1ull << 0;
	inline constexpr std::uint64_t JitPortableExecutionCapability = 1ull << 1;
	inline constexpr std::uint64_t JitNativeTypedLoopsCapability = 1ull << 2;
	inline constexpr std::uint64_t JitDirectScalarCallsCapability = 1ull << 3;
	inline constexpr std::uint64_t JitControlledExecutionCapability = 1ull << 4;

	struct FJitBackendDescription
	{
		std::string Id;
		std::string DisplayName;
		std::string Target;
		std::uint32_t CoreVersion = VEROSCRIPT_VERSION_CODE;
		std::uint64_t RequiredServices = 0;
		std::uint64_t Capabilities = 0;
		std::size_t MaxInvocationDepth = 8;
		// Separate bound for verified direct scalar kernels with no host reentry.
		// Zero retains the ordinary invocation bound.
		std::size_t MaxDirectScalarDepth = 0;
		bool bNativeCode = false;
		std::string NativeUnavailableReason;
	};

	struct FJitResourceUsage
	{
		std::size_t ExecutableBytes = 0;
		std::size_t RetainedBytes = 0;
		std::size_t ScratchBytes = 0;
		// Latched for this cache generation, including rejected/reclaimed candidates.
		bool bResourceLimitReached = false;
	};

	struct FJitResourceLimits
	{
		std::size_t ExecutableBytes = std::numeric_limits<std::size_t>::max();
		std::size_t RetainedBytes = std::numeric_limits<std::size_t>::max();
	};

	struct FJitCompileRequest
	{
		FJitProgramView Program;
		FJitResourceLimits AvailableResources;
		std::uint64_t AvailableServices = 0;
		std::uint16_t FunctionIndex = 0;
		std::uint64_t RequiredGovernance = 0;
		FExecutionCheck ExecutionCheck;
	};

	struct FJitCallContext
	{
		void MarkEntered() noexcept
		{
			bEntered = true;
		}

		const FJitProgramView* Program = nullptr;
		IJitRuntimeServices* Services = nullptr;
		const FValue* Arguments = nullptr;
		FObjectHandle Receiver;
		FValue ReturnValue = std::int32_t{0};
		std::uint16_t FunctionIndex = 0;
		std::uint8_t ArgumentCount = 0;
		bool bEntered = false;
		bool bHasReturn = false;
		EJitFallbackReason FallbackReason = EJitFallbackReason::None;
		EJitExecutionKind ExecutionKind = EJitExecutionKind::Unknown;
	};

	class IJitExecutable;

	struct FJitCompiledFunction
	{
		std::shared_ptr<const IJitExecutable> Executable;
		std::shared_ptr<const FBytecodeModule> ProgramOwner;
		FJitProgramIdentity Identity;
		std::uint64_t RequiredServices = 0;
		std::uint64_t GovernanceCapabilities = 0;
		std::uint16_t FunctionIndex = 0;
		bool bSuppressNestedBackendOnFallback = false;
		// Core can use ordinary VM dispatch without activating backend services.
		// This is an input-independent cost decision, not a partial invocation.
		bool bPreferVmForExecution = false;
		// Optional cost hint for an existing, ungoverned interpreter caller.
		// Standalone and native callers retain the executable entry.
		bool bPreferVmForNestedExecution = false;
		EJitExecutionKind ExecutionKind = EJitExecutionKind::Unknown;
		EJitFallbackReason FallbackReason = EJitFallbackReason::None;
	};

	struct FJitCompileResponse
	{
		std::shared_ptr<const FJitCompiledFunction> Function;
		std::string Reason;
		EJitFallbackReason FallbackReason = EJitFallbackReason::None;
	};

	/**
	 * Owns every code/data object required by its entry. Invoke uses the host C++
	 * ABI; generated code calls only backend-owned ABI adapters that cannot throw.
	 */
	class IJitExecutable
	{
	public:
		virtual ~IJitExecutable() = default;

		virtual EJitInvocationResult Invoke(FJitCallContext& Context) const = 0;
	};

	class IJitBackendInstance
	{
	public:
		virtual ~IJitBackendInstance() = default;

		virtual const FJitBackendDescription& GetDescription() const noexcept = 0;

		virtual EJitCompilationResult Compile(
			const FJitCompileRequest& Request,
			FJitCompileResponse& Response) = 0;

		// Publish direct targets only after the core validates ownership and budget.
		// Failure/exception must leave all previously published targets unchanged.
		virtual bool Publish(const FJitCompiledFunction&, std::string&)
		{
			return true;
		}

		// Reclaim an unpublished candidate or retain/measure it until safely retired.
		virtual void Discard(const std::shared_ptr<const FJitCompiledFunction>&) noexcept
		{
		}

		virtual void ResetExecutionStatistics() noexcept
		{
		}

		virtual FJitStats GetStatistics() const noexcept
		{
			return {};
		}

		// Unique resource categories include allocator reservation and retired code.
		virtual FJitResourceUsage GetResourceUsage() const noexcept = 0;

		// Called only while idle. Executables independently keep their storage alive.
		virtual void Retire() noexcept = 0;
	};

	class IJitBackendFactory
	{
	public:
		virtual ~IJitBackendFactory() = default;

		virtual const FJitBackendDescription& GetDescription() const noexcept = 0;

		// Immutable factories may be shared; creation must support concurrent workers.
		virtual std::unique_ptr<IJitBackendInstance> CreateInstance(
			const FJitProgramView& Program,
			const FJitResourceLimits& Limits,
			std::string& OutError) const = 0;
	};

	/**
	 * Core-owned scheduling/cache boundary. One owner thread uses an instance;
	 * workers have independent caches. Dense slots never expose container addresses.
	 */
	class FJitBackendCache
	{
	public:
		static std::unique_ptr<FJitBackendCache> Create(
			std::shared_ptr<const IJitBackendFactory> Factory,
			FJitProgramView Program,
			FJitPolicy Policy,
			FJitResourceLimits Limits,
			std::uint64_t AvailableServices,
			std::string& OutError);

		~FJitBackendCache();

		FJitBackendCache(const FJitBackendCache&) = delete;

		FJitBackendCache& operator=(const FJitBackendCache&) = delete;

		void BeginRun() noexcept;

		void NotifyResourcesChanged() noexcept;

		bool ShouldCompileOnCall(std::uint16_t FunctionIndex) noexcept;

		EJitCompilationResult EnsureCompiled(std::uint16_t FunctionIndex, FExecutionCheck Check = {},
			std::uint64_t RequiredGovernance = 0) noexcept;

		bool Prepare(EExecutionBackend Mode, std::size_t PreferredFunctionIndex, FExecutionCheck Check = {},
			std::uint64_t RequiredGovernance = 0) noexcept;

		std::shared_ptr<const FJitCompiledFunction> Find(std::uint16_t FunctionIndex) const noexcept;
		// Cached terminal rejection; unvisited and deferred functions return false.
		bool IsUnsupported(std::uint16_t FunctionIndex) const noexcept;

		EJitInvocationResult Invoke(std::uint16_t FunctionIndex, FJitCallContext& Context) noexcept;

		const FJitProgramView& GetProgram() const noexcept;

		const FJitBackendDescription& GetDescription() const noexcept;

		const std::shared_ptr<const IJitBackendFactory>& GetFactory() const noexcept;

		static std::size_t MinimumRetainedBytes(std::size_t FunctionCount) noexcept;

		FJitResourceUsage GetResourceUsage() const noexcept;

		FJitStats GetStats() const noexcept;

		void ResetExecutionStatistics() noexcept;

		void AccumulateTransientStatistics(const FJitStats& Statistics) noexcept;

		std::string_view GetLastError() const noexcept;

		std::string_view GetFunctionReason(std::uint16_t FunctionIndex) const noexcept;
		EJitFallbackReason GetFunctionFallbackReason(std::uint16_t FunctionIndex) const noexcept;

	private:
		friend class VeroScriptVm;

		struct FImpl;

		explicit FJitBackendCache(std::unique_ptr<FImpl> Impl) noexcept;

		// Inspect metadata while the caller owns the cache and cannot reenter.
		const FJitCompiledFunction* FindBorrowed(std::uint16_t FunctionIndex) const noexcept;

		std::unique_ptr<FImpl> Impl;
	};

	// Process-unique program identity; unrelated to product or bytecode versions.
	std::uint64_t AllocateJitProgramIdentity() noexcept;

	// Host target for native artifacts. Portable C++ backends use "portable-cpp".
	std::string_view GetJitHostTarget() noexcept;
}
