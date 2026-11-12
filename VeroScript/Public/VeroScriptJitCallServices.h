// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBuiltins.h"
#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>

namespace VeroScript
{
	inline constexpr std::uint64_t JitCallServiceCapability = 1u << 2u;

	// Optional bounded counter view for generated direct calls. It remains valid
	// for this invocation, including synchronous nested services. Only balanced
	// increments below the applicable backend/direct bound and total limit are permitted; the core validates restoration
	// before accepting any return. No VM layout or native calling ABI is exposed.
	struct FJitCallDepthBudget
	{
		std::size_t* ActiveDepth = nullptr;
		std::size_t MaximumBackendDepth = 0;
		std::size_t MaximumTotalDepth = 0;
		std::size_t MaximumDirectScalarDepth = 0;
	};

	/**
	 * Calls preserve the interpreter's access, const, generation, interception,
	 * reference and exception rules. Arguments are prepared bytecode operands.
	 * Method and indirect calls produce the ordinary bytecode result or sentinel.
	 * Builtins report whether an instruction result exists; native void calls
	 * produce the bytecode's i32 zero sentinel. All outputs are owned core roots.
	 */
	class IJitCallServices
	{
	public:
		virtual ~IJitCallServices() = default;

		virtual bool InvokeBuiltin(EBuiltinId Builtin, const FValue* Arguments,
			std::uint8_t ArgumentCount, bool& OutHasResult, FValue& OutResult) noexcept = 0;

		virtual bool InvokeNative(std::uint16_t NativeIndex, const FValue* Arguments,
			std::uint8_t ArgumentCount, FValue& OutResult) noexcept = 0;

		virtual bool InvokeMethod(std::uint16_t NameIndex, const FValue& Receiver,
			const FValue* Arguments, std::uint8_t ArgumentCount,
			std::uint32_t CallSiteOffset, FValue& OutResult) noexcept = 0;

		virtual bool InvokeIndirect(const FValue& Callable, const FValue* Arguments,
			std::uint8_t ArgumentCount, FValue& OutResult) noexcept = 0;

		virtual bool MakeClosure(std::uint16_t FunctionIndex, const FValue* Captures,
			std::uint8_t CaptureCount, FValue& OutClosure) noexcept = 0;

		// Execute the selected invocation through the interpreter without charging
		// a second call or recursively selecting its backend. No live backend frame.
		virtual bool InterpretInvocation(const FValue* Arguments, std::uint8_t ArgumentCount,
			const FObjectHandle& Receiver, FValue& OutResult) noexcept = 0;

		// Resolve ordinary script virtual dispatch without executing the selected
		// body. An interceptor or a non-script receiver declines with OutHandled=false.
		virtual bool ResolveMethod(const FObjectHandle& Receiver, std::uint16_t NameIndex,
			std::uint8_t ArgumentCount, std::uint32_t CallSiteOffset,
			std::uint16_t& OutFunctionIndex, bool& OutHandled) noexcept = 0;

		// Account for a nested body executed entirely by an optimized backend.
		// Every successful entry needs one leave, including failure/control transfer.
		virtual FJitCallDepthBudget BorrowCallDepthBudget() noexcept = 0;

		virtual bool EnterOptimizedCall() noexcept = 0;

		virtual void LeaveOptimizedCall() noexcept = 0;

		virtual bool HasInstanceInterceptor() const noexcept = 0;

		virtual bool HasMutationObserver() const noexcept = 0;

		virtual std::size_t GetRemainingCallDepth() const noexcept = 0;
	};
}
