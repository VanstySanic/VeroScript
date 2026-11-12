// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"
#include "VeroScriptExecutionGovernance.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace VeroScript
{
	// Read-only live flags owned by the core and valid only during the invocation.
	// Nested calls may change them; never snapshot the pointed-to values or retain
	// the view after returning. Empty pointers select the virtual query fallback.
	struct FJitExecutionStatusView
	{
		const bool* Error = nullptr;
		const bool* PendingException = nullptr;
		const bool* ExitRequested = nullptr;
		const bool* InstanceInterceptor = nullptr;
		const bool* MutationObserver = nullptr;
	};

	class IJitFrameServices;
	class IJitValueServices;
	class IJitCallServices;
	class IJitScalarServices;
	class ICompiledFunctionServices;

	/**
	 * Invocation-scoped language operations. Implementations belong to the core;
	 * backends must not recover a VM pointer from this interface. No service may
	 * retain arguments or an invocation's borrowed storage after returning.
	 */
	class IJitRuntimeServices
	{
	public:
		virtual ~IJitRuntimeServices() = default;

		virtual FJitExecutionStatusView BorrowExecutionStatus() const noexcept
		{
			return {};
		}

		virtual bool HasError() const noexcept = 0;

		virtual bool HasPendingControlTransfer() const noexcept = 0;

		virtual bool IsExitRequested() const noexcept
		{
			return false;
		}

		// Backends call this at loop/call safe points; false forbids further effects.
		virtual bool CheckExecutionGovernance() noexcept = 0;

		virtual IExecutionGovernanceServices* GetExecutionGovernanceServices() noexcept
		{
			return nullptr;
		}

		virtual ICompiledFunctionServices* GetCompiledFunctionServices() noexcept
		{
			return nullptr;
		}

		// Optional service groups are negotiated by the backend's RequiredServices.
		virtual IJitFrameServices* GetFrameServices() noexcept
		{
			return nullptr;
		}

		virtual IJitValueServices* GetValueServices() noexcept
		{
			return nullptr;
		}

		virtual IJitCallServices* GetCallServices() noexcept
		{
			return nullptr;
		}

		virtual IJitScalarServices* GetScalarServices() noexcept
		{
			return nullptr;
		}

		// Implementations retain an allocation-free failure state if formatting fails.
		virtual void ReportError(std::string_view Message) noexcept = 0;

		// Refine an existing diagnostic against generation-owned source metadata.
		// Nested diagnostics already pinned by the core retain their own location.
		virtual void RefineErrorLocation(std::uint16_t, std::size_t,
		    const FDebugLine*, std::size_t) noexcept
		{
		}

		virtual void ReportErrorId(std::string_view Id, std::string_view = {}) noexcept
		{
			ReportError(Id);
		}

		// Request generation-scoped compilation through the core's cumulative
		// budgets and transactional publication. false also means unsupported or
		// deferred; HasError distinguishes a diagnosed compilation failure.
		virtual bool PrepareFunction(std::uint16_t, bool) noexcept
		{
			return false;
		}

		// Validate and normalize an owned internal return, preserving reference cells.
		virtual bool PrepareReturn(
		    const FBytecodeFunction& Function,
		    FValue& Value) noexcept = 0;

		// Arguments are prepared bytecode operands, including explicit CloneStruct.
		// By-value roots are shared for the call; reference operands retain bindings.
		virtual bool InvokeFunction(
		    std::uint16_t FunctionIndex,
		    const FObjectHandle& Receiver,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    FValue& OutReturn) noexcept = 0;

		virtual bool CopyValue(const FValue& Value, FValue& OutCopy) noexcept = 0;

		virtual bool ReleaseValue(FValue& Value) noexcept = 0;
	};
}
