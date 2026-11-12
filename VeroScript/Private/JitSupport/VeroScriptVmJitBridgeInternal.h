// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VeroScript::JitBridgeDetail
{
	inline bool IsScalarNativeType(const ENativeType Type)
	{
		return Type == ENativeType::Int
			|| Type == ENativeType::Int8
			|| Type == ENativeType::UInt8
			|| Type == ENativeType::Int16
			|| Type == ENativeType::UInt16
			|| Type == ENativeType::Int64
			|| Type == ENativeType::UInt64
			|| Type == ENativeType::Unsigned
			|| Type == ENativeType::Bool
			|| Type == ENativeType::Float
			|| Type == ENativeType::Double;
	}

	inline bool IsScalarNativeSignature(
		const FNativeFunction& Native,
		const std::uint8_t ArgumentCount)
	{
		if (!Native.Callback
			|| Native.ParamTypes.size() != ArgumentCount
			|| (!IsScalarNativeType(Native.ReturnType)
				&& Native.ReturnType != ENativeType::Void)
			|| !NativeParamFlagsAreValid(Native)
			|| !NativeFunctionContainerMetadataIsValid(Native))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			if (!IsScalarNativeType(Native.ParamTypes[Index])
				|| NativeParamFlagAt(Native, Index) != 0)
			{
				return false;
			}
		}
		return true;
	}

}
