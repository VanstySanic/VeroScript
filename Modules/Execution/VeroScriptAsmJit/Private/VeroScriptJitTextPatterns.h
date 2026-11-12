// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptBuiltins.h"
#include <string_view>

namespace VeroScript::AsmJit::JitLinear
{
	enum class EPureTextProjection : std::uint8_t
	{
		None,
		CharacterCode,
		IdentityString,
		IdentityWString
	};

	struct FPureTextProjection
	{
		EPureTextProjection Kind = EPureTextProjection::None;
		std::uint32_t InstructionOffset = 0;
	};

	struct FReadonlyTextBuiltinForwarder
	{
		EBuiltinId Builtin = EBuiltinId::Find;
		std::uint32_t InstructionOffset = 0;
	};

	inline bool DescribeReadonlyTextBuiltinForwarder(const FBytecodeFunction& Function,
		FReadonlyTextBuiltinForwarder& Out) noexcept
	{
		Out = {};
		if (Function.bIsMethod || Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted ||
			Function.bIsAbstract || !Function.UnavailableReason.empty() || !Function.Captures.empty() ||
			!Function.ExceptionRegions.empty() || !Function.PauseMarkerOffsets.empty() ||
			Function.bUsesLocalStaticStorage || !Function.LocalStaticInitializers.empty() ||
			BytecodeFunctionHasRefParams(Function) || Function.ParamCount != 2u || Function.LocalCount != 2u)
		{
			return false;
		}
		const auto& Code = Function.Code;
		std::size_t First = 0;
		while (First < Code.size() && First < 4u && static_cast<EOpcode>(Code[First]) == EOpcode::Nop)
		{
			++First;
		}
		constexpr auto LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		constexpr auto CallSize = Bytecode::InstructionSize(EOpcode::CallBuiltin);
		constexpr auto ReturnSize = Bytecode::InstructionSize(EOpcode::Return);
		if (Code.size() - First != 2u * LoadSize + CallSize + ReturnSize ||
			static_cast<EOpcode>(Code[First]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + First + 1u) != 0u ||
			static_cast<EOpcode>(Code[First + LoadSize]) != EOpcode::LoadLocal ||
			Bytecode::ReadU16LE(Code.data() + First + LoadSize + 1u) != 1u ||
			static_cast<EOpcode>(Code[First + 2u * LoadSize]) != EOpcode::CallBuiltin ||
			Code[First + 2u * LoadSize + 2u] != 2u || static_cast<EOpcode>(Code.back()) != EOpcode::Return)
		{
			return false;
		}
		const auto Builtin = static_cast<EBuiltinId>(Code[First + 2u * LoadSize + 1u]);
		const bool Integral = Builtin == EBuiltinId::Find || Builtin == EBuiltinId::StringLastFind;
		const bool Boolean = Builtin == EBuiltinId::StringStartsWith || Builtin == EBuiltinId::StringEndsWith;
		if ((!Integral && !Boolean) || Function.ClosureTypeName !=
										   (Integral ? "fn(string,string)->int64" : "fn(string,string)->bool"))
		{
			return false;
		}
		Out = {Builtin, static_cast<std::uint32_t>(First + 2u * LoadSize)};
		return true;
	}

	// A projection has no observable body besides this intrinsic operation.
	// Validate its signature as well as its bytecode; manually supplied modules
	// must not bypass the ordinary argument/return contract through a lookalike.
	inline bool DescribePureTextProjection(const FBytecodeFunction& Function,
		FPureTextProjection& Out) noexcept
	{
		Out = {};
		if ((Function.bIsMethod && !Function.bIsStaticMethod) || Function.bReturnsVoid ||
			Function.bReturnsRef || Function.bDeleted || Function.bIsAbstract ||
			!Function.UnavailableReason.empty() || !Function.Captures.empty() ||
			!Function.ExceptionRegions.empty() || !Function.PauseMarkerOffsets.empty() ||
			!Function.LocalStaticInitializers.empty() || BytecodeFunctionHasRefParams(Function) ||
			Function.LocalCount != Function.ParamCount)
		{
			return false;
		}
		const auto& Code = Function.Code;
		std::size_t First = 0;
		while (First < Code.size() && First < 4u &&
			   static_cast<EOpcode>(Code[First]) == EOpcode::Nop)
		{
			++First;
		}
		constexpr auto LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		constexpr auto CallSize = Bytecode::InstructionSize(EOpcode::CallBuiltin);
		constexpr auto ReturnSize = Bytecode::InstructionSize(EOpcode::Return);
		const std::string_view Signature = Function.ClosureTypeName;
		if (Function.ParamCount == 1u && Code.size() - First == LoadSize + ReturnSize &&
			static_cast<EOpcode>(Code[First]) == EOpcode::LoadLocal &&
			Bytecode::ReadU16LE(Code.data() + First + 1u) == 0u &&
			static_cast<EOpcode>(Code.back()) == EOpcode::Return)
		{
			if (Signature == "fn(string)->string")
			{
				Out.Kind = EPureTextProjection::IdentityString;
			}
			else if (Signature == "fn(wstring)->wstring")
			{
				Out.Kind = EPureTextProjection::IdentityWString;
			}
			else
			{
				return false;
			}
			Out.InstructionOffset = static_cast<std::uint32_t>(Code.size() - ReturnSize);
			return true;
		}
		if (Function.ParamCount != 2u || Code.size() - First != 2u * LoadSize + CallSize + ReturnSize ||
			static_cast<EOpcode>(Code[First]) != EOpcode::LoadLocal ||
			Bytecode::ReadU16LE(Code.data() + First + 1u) != 0u ||
			static_cast<EOpcode>(Code[First + LoadSize]) != EOpcode::LoadLocal ||
			Bytecode::ReadU16LE(Code.data() + First + LoadSize + 1u) != 1u ||
			static_cast<EOpcode>(Code[First + 2u * LoadSize]) != EOpcode::CallBuiltin ||
			static_cast<EBuiltinId>(Code[First + 2u * LoadSize + 1u]) != EBuiltinId::CharCode ||
			Code[First + 2u * LoadSize + 2u] != 2u ||
			static_cast<EOpcode>(Code.back()) != EOpcode::Return)
		{
			return false;
		}
		if (Signature != "fn(string,int)->int" && Signature != "fn(string,int64)->int" &&
			Signature != "fn(string,unsigned int)->int" && Signature != "fn(string,uint64)->int")
		{
			return false;
		}
		Out.Kind = EPureTextProjection::CharacterCode;
		Out.InstructionOffset = static_cast<std::uint32_t>(First + 2u * LoadSize);
		return true;
	}
}
