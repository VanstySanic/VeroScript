// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVersion.h"
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace VeroScript
{
	struct FBytecodeModule;
	struct FVmExecutionView;
	struct FVmHostServices;
	class IVmExecutionServices;
	inline constexpr std::uint64_t VmBytecodeCapability = 1ull << 0;
	inline constexpr std::uint64_t VmGovernanceCapability = 1ull << 1;
	inline constexpr std::uint64_t VmDebugCapability = 1ull << 2;
	inline constexpr std::uint64_t VmHotReloadCapability = 1ull << 3;
	inline constexpr std::uint64_t VmCleanupCapability = 1ull << 4;
	inline constexpr std::uint64_t VmAllCapabilities = (1ull << 5) - 1;
	struct FVmBackendDescription
	{
		std::string Id;
		std::string DisplayName;
		std::string Version;
		std::string Target;
		std::string ImplementationId;
		std::uint32_t CoreVersion = VEROSCRIPT_VERSION_CODE;
		std::uint64_t Capabilities = 0;
	};
	enum class EVmExecutionResult : std::uint8_t
	{
		NotEntered,
		Returned,
		ControlTransfer,
		Failed
	};
	/** Prepared Core frames: arguments and frame entry must not be repeated. */
	struct FVmInvocation
	{
		IVmExecutionServices& Services;
		FVmExecutionView& State;
		const void* const DomainIdentity;
		const void* const ContextIdentity;
		const FBytecodeModule* const Program;
		const std::uint64_t ProgramGeneration;
		const std::uint64_t CallToken;
		const std::size_t FrameBoundary;
		const bool bContinuation;
		const bool bInstrumented;
		const bool bGoverned;
		// Written before the first semantic operation. NotEntered leaves it false.
		bool bEntered = false;
	};
	struct FVmBackendStats
	{
		std::uint64_t Entries = 0;
		std::uint64_t Continuations = 0;
		std::uint64_t Instances = 0;
		std::uint64_t Rebindings = 0;
		std::size_t RetainedBytes = 0;
	};
	class IVmBackendInstance
	{
	public:
		virtual ~IVmBackendInstance() = default;
		virtual bool Bind(const void* ContextIdentity, const FBytecodeModule* Program,
		    std::uint64_t Generation, std::string& Error) = 0;
		virtual EVmExecutionResult Execute(FVmInvocation& Invocation) = 0;
		virtual bool SupportsIdleRebinding() const noexcept
		{
			return false;
		}
		virtual void Unbind() noexcept = 0;
		virtual std::size_t GetRetainedBytes() const noexcept
		{
			return 0;
		}
	};
	class IVmBackendFactory
	{
	public:
		virtual ~IVmBackendFactory() = default;
		virtual const FVmBackendDescription& GetDescription() const noexcept = 0;
		virtual const void* GetHostIdentity() const noexcept = 0;
		virtual IVmBackendInstance* CreateInstance(std::string& Error) const = 0;
		// The allocating module destroys its own instance.
		virtual void DestroyInstance(IVmBackendInstance* Instance) const noexcept = 0;
	};
	/** Owner-thread domain; Session Clear does not thaw its selection. */
	class FVmExecutionDomain final
	{
		friend class VeroScriptVm;
		friend class VeroScriptSession;

	public:
		bool IsFrozen() const noexcept
		{
			return bFrozen;
		}
		const std::shared_ptr<const IVmBackendFactory>& GetFactory() const noexcept
		{
			return Factory;
		}
		const std::string& GetLibraryPath() const noexcept
		{
			return LibraryPath;
		}

	private:
		std::shared_ptr<const IVmBackendFactory> Factory;
		std::string LibraryPath;
		bool bFrozen = false;
		std::size_t ActiveDepth = 0;
		std::shared_ptr<IVmBackendInstance> IdleInstance;
		std::thread::id IdleThread;
	};
}
