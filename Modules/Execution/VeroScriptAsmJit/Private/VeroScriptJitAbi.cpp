// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitAbi.h"

#include <limits>

namespace VeroScript::AsmJit::JitAbi
{
	namespace
	{
		void SetError(std::string* const OutError, const char* const Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
		}

		bool IsScalar(const JitIr::EValueType Type)
		{
			return Type == JitIr::EValueType::I32 || Type == JitIr::EValueType::U32
				|| Type == JitIr::EValueType::I64 || Type == JitIr::EValueType::U64
				|| Type == JitIr::EValueType::F32 || Type == JitIr::EValueType::F64
				|| Type == JitIr::EValueType::Bool
				|| Type == JitIr::EValueType::Void;
		}
	}

	bool BuildScalarCallLayout(
		const JitIr::FSignature& Signature,
		const ECallAbi Abi,
		FCallLayout& OutLayout,
		std::string* const OutError)
	{
		OutLayout = {};
		OutLayout.Abi = Abi;
		if (!Signature.bValid || !IsScalar(Signature.ReturnType))
		{
			SetError(OutError, "call signature is not a valid scalar signature");
			return false;
		}
		const std::uint8_t RegisterLimit = Abi == ECallAbi::MsX64 ? 4
			: Abi == ECallAbi::Aapcs64 ? 8
																  : 0;
		std::uint8_t GprIndex = 0;
		std::uint8_t FprIndex = 0;
		std::size_t ArgumentIndex = 0;
		std::uint32_t StackOffset = Abi == ECallAbi::MsX64 ? 32u : 0u;
		for (const JitIr::EValueType Type : Signature.Parameters)
		{
			if (!IsScalar(Type) || Type == JitIr::EValueType::Void)
			{
				SetError(OutError, "call signature contains a non-scalar parameter");
				return false;
			}
			FArgumentLocation Location;
			Location.Type = Type;
			const bool bFloat = Type == JitIr::EValueType::F32
				|| Type == JitIr::EValueType::F64;
			Location.RegisterClass = bFloat ? ERegisterClass::Fpr : ERegisterClass::Gpr;
			if (Abi == ECallAbi::MsX64 && ArgumentIndex < RegisterLimit)
			{
				// Microsoft x64 assigns registers by argument position. Integer and
				// floating arguments do not consume independent register sequences.
				Location.RegisterIndex = static_cast<std::uint8_t>(ArgumentIndex);
			}
			else if (Abi == ECallAbi::Aapcs64
				&& (bFloat ? FprIndex : GprIndex) < RegisterLimit)
			{
				std::uint8_t& RegisterIndex = bFloat ? FprIndex : GprIndex;
				Location.RegisterIndex = RegisterIndex++;
			}
			else
			{
				Location.RegisterClass = ERegisterClass::Memory;
				if (StackOffset > std::numeric_limits<std::uint16_t>::max())
				{
					SetError(OutError, "call argument stack layout exceeds supported size");
					return false;
				}
				Location.StackOffset = static_cast<std::uint16_t>(StackOffset);
				const bool bWide = Type == JitIr::EValueType::I64
					|| Type == JitIr::EValueType::U64
					|| Type == JitIr::EValueType::F64;
				StackOffset += Abi == ECallAbi::CdeclX86 && !bWide ? 4u : 8u;
			}
			OutLayout.Arguments.push_back(Location);
			++ArgumentIndex;
		}
		if (Abi != ECallAbi::CdeclX86)
		{
			// Both 64-bit ABIs require SP to be 16-byte aligned at call boundaries.
			StackOffset = (StackOffset + 15u) & ~15u;
		}
		if (StackOffset > std::numeric_limits<std::uint16_t>::max())
		{
			SetError(OutError, "call stack layout exceeds supported size");
			return false;
		}
		OutLayout.Return.Type = Signature.ReturnType;
		OutLayout.Return.RegisterClass = Signature.ReturnType == JitIr::EValueType::Void
			? ERegisterClass::Memory
			: (Signature.ReturnType == JitIr::EValueType::F32
				|| Signature.ReturnType == JitIr::EValueType::F64
				? ERegisterClass::Fpr
					  : ERegisterClass::Gpr);
		OutLayout.StackBytes = static_cast<std::uint16_t>(StackOffset);
		OutLayout.bSupported = true;
		return true;
	}
}
