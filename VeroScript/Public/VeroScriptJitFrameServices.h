// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>

namespace VeroScript
{
	inline constexpr std::uint64_t JitFrameServiceCapability = 1u << 0u;

	struct FJitFrameToken
	{
		std::uint64_t Identity = 0;
		std::size_t Index = 0;
	};

	// Direct slots stay at these addresses until the owning frame leaves or a
	// control transfer destroys it, including across nested calls. Slot contents
	// may become references; reread their tags and resolve them through services.
	// Use language assignment/release services when replacing an owned root.
	struct FJitLocalStorageView
	{
		FValue* Data = nullptr;
		std::size_t Count = 0;
	};

	struct FJitFrameDescription
	{
		std::uint16_t FunctionIndex = 0;
		// Optional rewritten body, owned by the active executable for the whole frame.
		// The declaration's callable signature must remain identical.
		const FBytecodeFunction* Body = nullptr;
	};

	/**
	 * Core-owned runtime frames. Tokens identify one activation, including recursion.
	 * Only the active frame may be accessed. Except for FJitLocalStorageView,
	 * borrowed storage survives read-only borrows and expires before allocation,
	 * calls, binding or frame changes; it must never escape the invocation.
	 * Reference results must be promoted by PrepareReturn before leaving their frame.
	 */
	class IJitFrameServices
	{
	public:
		virtual ~IJitFrameServices() = default;

		virtual bool Enter(const FJitFrameDescription& Description,
			const FValue* Arguments, std::uint8_t ArgumentCount,
			const FObjectHandle& Receiver, FJitFrameToken& OutToken) noexcept = 0;

		virtual bool Leave(FJitFrameToken Token) noexcept = 0;

		virtual bool SetInstructionOffset(FJitFrameToken Token, std::size_t Offset) noexcept = 0;

		virtual bool LoadLocal(FJitFrameToken Token, std::uint16_t Slot, FValue& OutValue) noexcept = 0;

		virtual bool StoreLocal(FJitFrameToken Token, std::uint16_t Slot, const FValue& Value) noexcept = 0;

		virtual FJitLocalStorageView BorrowDirectLocalStorage(FJitFrameToken) noexcept
		{
			return {};
		}

		virtual FValue* BorrowLocal(FJitFrameToken Token, std::uint16_t Slot) noexcept = 0;

		virtual FValue* BorrowResolvedLocal(FJitFrameToken Frame, std::uint16_t Slot) noexcept = 0;

		virtual bool MakeLocalReference(FJitFrameToken Token, std::uint16_t Slot, FValue& OutReference) noexcept = 0;

		virtual bool LoadGlobal(std::uint16_t Slot, FValue& OutValue) noexcept = 0;

		virtual bool StoreGlobal(std::uint16_t Slot, FValue Value) noexcept = 0;

		virtual bool MakeGlobalReference(std::uint16_t Slot, FValue& OutReference) noexcept = 0;

		virtual bool EvaluateArithmetic(EOpcode Operation, const FValue& Left,
			const FValue& Right, FValue& OutValue) noexcept = 0;

		virtual bool EvaluateUnary(EOpcode Operation, const FValue& Value, FValue& OutValue) noexcept = 0;

		virtual bool EvaluateBitwise(EOpcode Operation, const FValue& Left,
			const FValue& Right, FValue& OutValue) noexcept = 0;

		virtual bool Compare(EOpcode Operation, const FValue& Left,
			const FValue& Right, bool& OutValue) noexcept = 0;
	};
}
