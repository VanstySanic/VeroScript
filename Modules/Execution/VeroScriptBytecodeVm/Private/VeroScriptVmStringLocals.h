// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VeroScript
{
	struct FBytecodeFunction;
	struct FBytecodeModule;

	namespace VmStringLocals
	{
		enum class EFusionResult : std::uint8_t
		{
			None,
			Length,
			Append,
		};

		EFusionResult TryFuseLoad(FValue &Value, const FBytecodeModule &Module,
								  const std::vector<std::uint8_t> &Code, std::size_t &Next,
								  std::uint16_t Slot, const std::vector<FValue> &Locals,
								  std::size_t StackRoom, std::int64_t &OutLength);

#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		EFusionResult TryFuseTextLoad(FValue& V, const FBytecodeModule& M, const std::vector<std::uint8_t>& C,
		    std::size_t& N, std::uint16_t S, const std::vector<FValue>& L, std::size_t R, std::int64_t& O);
#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		bool CanTransferOptimizedTextArgument(const FValue& V, const FBytecodeFunction& F, std::size_t N, std::size_t R) noexcept;

		bool CanTransferTextArgument(const FValue& Value, const FBytecodeFunction& Function,
		    std::size_t Next, std::size_t StackRoom) noexcept;

		bool TryFuseCharCode(const FValue& Text, const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code, std::size_t& Next,
		    const std::vector<FValue>& Locals, std::size_t StackRoom,
		    bool AllowWrapper, std::int32_t& OutCode, bool& OutWrapper) noexcept;

		bool TryFuseIdentity(const FBytecodeModule& Module, const std::vector<std::uint8_t>& Code,
		    std::size_t& Next, std::uint16_t Slot, std::size_t StackRoom, bool& OutInPlace) noexcept;

		bool TryFuseLength(const FValue &Value, const std::vector<std::uint8_t> &Code, std::size_t &Next,
						   std::int64_t &OutLength) noexcept;
	} // namespace VmStringLocals
} // namespace VeroScript
