// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptHostBinding.h"
#include "VeroScriptAsmJitRuntime.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VeroScript::AsmJit::JitBridgeDetail
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

	inline bool PackMixedScalar(
		const EJitScalarValueKind Kind,
		const FValue& Value,
		std::uint64_t& OutBits)
	{
		switch (Kind)
		{
		case EJitScalarValueKind::I8:
			{
				if (const auto* Scalar = std::get_if<std::int8_t>(&Value))
				{
					OutBits = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U8:
			{
				if (const auto* Scalar = std::get_if<std::uint8_t>(&Value))
				{
					OutBits = *Scalar;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I16:
			{
				if (const auto* Scalar = std::get_if<std::int16_t>(&Value))
				{
					OutBits = static_cast<std::uint32_t>(static_cast<std::int32_t>(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U16:
			{
				if (const auto* Scalar = std::get_if<std::uint16_t>(&Value))
				{
					OutBits = *Scalar;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I32:
			{
				if (const auto* Scalar = std::get_if<std::int32_t>(&Value))
				{
					OutBits = static_cast<std::uint32_t>(*Scalar);
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U32:
			{
				if (const auto* Scalar = std::get_if<std::uint32_t>(&Value))
				{
					OutBits = *Scalar;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I64:
			{
				if (const auto* Scalar = std::get_if<std::int64_t>(&Value))
				{
					OutBits = static_cast<std::uint64_t>(*Scalar);
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U64:
			{
				if (const auto* Scalar = std::get_if<std::uint64_t>(&Value))
				{
					OutBits = *Scalar;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::F32:
			{
				if (const auto* Scalar = std::get_if<float>(&Value))
				{
					std::uint32_t Bits = 0;
					std::memcpy(&Bits, Scalar, sizeof(Bits));
					OutBits = Bits;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::F64:
			{
				if (const auto* Scalar = std::get_if<double>(&Value))
				{
					std::memcpy(&OutBits, Scalar, sizeof(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::Bool:
			{
				if (const auto* Scalar = std::get_if<bool>(&Value))
				{
					OutBits = *Scalar ? 1u : 0u;
					return true;
				}
				return false;
			}
		default:
			return false;
		}
	}

	inline bool UnpackMixedScalar(
		const EJitScalarValueKind Kind,
		const std::uint64_t Bits,
		FValue& OutValue)
	{
		switch (Kind)
		{
		case EJitScalarValueKind::I8:
			{
				OutValue = static_cast<std::int8_t>(static_cast<std::uint8_t>(Bits));
				return true;
			}
		case EJitScalarValueKind::U8:
			{
				OutValue = static_cast<std::uint8_t>(Bits);
				return true;
			}
		case EJitScalarValueKind::I16:
			{
				OutValue = static_cast<std::int16_t>(static_cast<std::uint16_t>(Bits));
				return true;
			}
		case EJitScalarValueKind::U16:
			{
				OutValue = static_cast<std::uint16_t>(Bits);
				return true;
			}
		case EJitScalarValueKind::I32:
			{
				OutValue = static_cast<std::int32_t>(static_cast<std::uint32_t>(Bits));
				return true;
			}
		case EJitScalarValueKind::U32:
			{
				OutValue = static_cast<std::uint32_t>(Bits);
				return true;
			}
		case EJitScalarValueKind::I64:
			{
				std::int64_t Scalar = 0;
				std::memcpy(&Scalar, &Bits, sizeof(Scalar));
				OutValue = Scalar;
				return true;
			}
		case EJitScalarValueKind::U64:
			{
				OutValue = Bits;
				return true;
			}
		case EJitScalarValueKind::F32:
			{
				const std::uint32_t ScalarBits = static_cast<std::uint32_t>(Bits);
				float Scalar = 0.0f;
				std::memcpy(&Scalar, &ScalarBits, sizeof(Scalar));
				OutValue = Scalar;
				return true;
			}
		case EJitScalarValueKind::F64:
			{
				double Scalar = 0.0;
				std::memcpy(&Scalar, &Bits, sizeof(Scalar));
				OutValue = Scalar;
				return true;
			}
		case EJitScalarValueKind::Bool:
			{
				OutValue = Bits != 0;
				return true;
			}
		default:
			return false;
		}
	}

	inline bool StoreMixedScalar(
		const EJitScalarValueKind Kind,
		const std::uint64_t Bits,
		FValue& InOutValue)
	{
		switch (Kind)
		{
		case EJitScalarValueKind::I8:
			{
				if (auto* Scalar = std::get_if<std::int8_t>(&InOutValue))
				{
					*Scalar = static_cast<std::int8_t>(static_cast<std::uint8_t>(Bits));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U8:
			{
				if (auto* Scalar = std::get_if<std::uint8_t>(&InOutValue))
				{
					*Scalar = static_cast<std::uint8_t>(Bits);
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I16:
			{
				if (auto* Scalar = std::get_if<std::int16_t>(&InOutValue))
				{
					*Scalar = static_cast<std::int16_t>(static_cast<std::uint16_t>(Bits));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U16:
			{
				if (auto* Scalar = std::get_if<std::uint16_t>(&InOutValue))
				{
					*Scalar = static_cast<std::uint16_t>(Bits);
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I32:
			{
				if (auto* Scalar = std::get_if<std::int32_t>(&InOutValue))
				{
					*Scalar = static_cast<std::int32_t>(static_cast<std::uint32_t>(Bits));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U32:
			{
				if (auto* Scalar = std::get_if<std::uint32_t>(&InOutValue))
				{
					*Scalar = static_cast<std::uint32_t>(Bits);
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::I64:
			{
				if (auto* Scalar = std::get_if<std::int64_t>(&InOutValue))
				{
					std::memcpy(Scalar, &Bits, sizeof(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::U64:
			{
				if (auto* Scalar = std::get_if<std::uint64_t>(&InOutValue))
				{
					*Scalar = Bits;
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::F32:
			{
				if (auto* Scalar = std::get_if<float>(&InOutValue))
				{
					const std::uint32_t ScalarBits = static_cast<std::uint32_t>(Bits);
					std::memcpy(Scalar, &ScalarBits, sizeof(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::F64:
			{
				if (auto* Scalar = std::get_if<double>(&InOutValue))
				{
					std::memcpy(Scalar, &Bits, sizeof(*Scalar));
					return true;
				}
				return false;
			}
		case EJitScalarValueKind::Bool:
			{
				if (auto* Scalar = std::get_if<bool>(&InOutValue))
				{
					*Scalar = Bits != 0;
					return true;
				}
				return false;
			}
		default:
			return false;
		}
	}
}
