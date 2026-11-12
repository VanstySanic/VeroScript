// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNative.h"

namespace VeroScript
{
	struct FNumericError;
}

namespace VeroScript::Memory
{
	bool IsValueLeaf(const FNativeFunction&, const FValue*, std::size_t) noexcept;
	// Standard catalog slot; handled operations accept sealed region values and
	// primitive operands. The work check must not reenter the memory domain.
	bool TryInvokeValueLeaf(std::uint16_t Index, IMemoryCallServices& Services,
		const FValue* Args, std::size_t Count,
		FValue& Output, FMemoryResult& Result);
}
namespace VeroScript::Numerics
{
	bool IsValueLeaf(const FNativeFunction&, const FValue*, std::size_t) noexcept;
	// The caller proves the registration is the immutable standard catalog entry.
	bool TryInvokeValueLeaf(const FNativeFunction&, const FNativeCallContext&,
		FValue&, std::string&, bool& Success);
	bool TryCopyValue(const FValue&, FValue&, FNumericError&, bool& Handled) noexcept;
}
