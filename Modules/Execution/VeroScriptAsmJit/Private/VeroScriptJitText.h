// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitLinear.h"
#include <atomic>

namespace VeroScript::AsmJit::JitLinear
{
	std::size_t ResolveNativeTextHash(FTypedLoopNativeContext& Context,
		const FTypedLoopNativeStringView& View) noexcept;

	inline std::size_t NativeTextHash(FTypedLoopNativeContext& Context,
		const FTypedLoopNativeStringView& View) noexcept
	{
		const auto* Scratch = Context.Strings;
		if (!Scratch || (!Scratch->LocalHashValid && !Scratch->TemporaryHashValid)) return View.Hash;
		return ResolveNativeTextHash(Context, View);
	}

	// Integer-only producers have a fixed maximum payload. Reserve their actual
	// buffers before publication so both allocation and quota rejection precede
	// execution; a nested invocation declines before borrowing an active pool.
	struct FTypedLoopTextBufferPool
	{
		explicit FTypedLoopTextBufferPool(std::size_t Count);
		std::size_t RetainedBytes() const noexcept;
		std::vector<std::string> Values;
		std::vector<FTypedLoopNativeStringView> Views;
		std::vector<ETypedLoopLocalKind> Kinds;
		std::vector<std::uint8_t> HashValid;
		std::atomic<bool> Active{false};
	};

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeStoreText(
		FTypedLoopNativeContext* Context, std::uint32_t Instruction,
		std::uint64_t Left, std::uint64_t Right) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeTextOperation(
		FTypedLoopNativeContext* Context, std::uint32_t Instruction,
		std::uint64_t Left, std::uint64_t Right) noexcept;

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeCharacterCode(
		FTypedLoopNativeContext* Context, std::uint64_t Text, std::uint64_t Index,
		ETypedLoopLocalKind IndexKind, std::uint32_t Offset) noexcept;
}
