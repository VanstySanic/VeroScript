// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptBytecodeLinker.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptPackageInterface.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	VeroScriptCompiler::FEmitter::FEmitter(std::vector<std::uint8_t>& InCode,
		std::vector<FDebugLine>* const InDebugLines, const FSourceLocation* const InLocation,
		std::vector<std::string>* const InDebugFiles, const std::vector<FNativeFunction>* const InNatives,
		std::vector<FBytecodeNativeReference>* const InNativeReferences)
		: Natives(InNatives), NativeReferences(InNativeReferences), Code(InCode), DebugLines(InDebugLines), EmitLocation(InLocation),
		  DebugFiles(InDebugFiles)
	{
	}

	void VeroScriptCompiler::FEmitter::Emit(const EOpcode Opcode)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Opcode));
	}

	void VeroScriptCompiler::FEmitter::EmitU8Operand(const EOpcode Opcode, const std::uint8_t Value)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Opcode));
		Bytecode::WriteU8(Code, Value);
	}

	void VeroScriptCompiler::FEmitter::EmitU16Operand(const EOpcode Opcode, const std::uint16_t Value)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Opcode));
		Bytecode::WriteU16LE(Code, Value);
	}

	void VeroScriptCompiler::FEmitter::EmitU16U16Operands(
		const EOpcode Opcode,
		const std::uint16_t First,
		const std::uint16_t Second)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Opcode));
		Bytecode::WriteU16LE(Code, First);
		Bytecode::WriteU16LE(Code, Second);
	}

	std::size_t VeroScriptCompiler::FEmitter::EmitI32Operand(const EOpcode Opcode, const std::int32_t Value)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Opcode));
		const std::size_t OperandOffset = Code.size();
		Bytecode::WriteI32LE(Code, Value);
		return OperandOffset;
	}

	void VeroScriptCompiler::FEmitter::EmitCall(const std::uint16_t FunctionIndex, const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::Call));
		Bytecode::WriteU16LE(Code, FunctionIndex);
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::EmitCallMethod(
		const std::uint16_t MethodNameIndex,
		const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::CallMethod));
		Bytecode::WriteU16LE(Code, MethodNameIndex);
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::EmitCallBoundMethod(
		const std::uint16_t FunctionIndex,
		const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::CallBoundMethod));
		Bytecode::WriteU16LE(Code, FunctionIndex);
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::EmitCallBuiltin(const EBuiltinId Builtin, const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::CallBuiltin));
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(Builtin));
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::EmitCallNative(
		const std::uint16_t NativeIndex,
		const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		const auto Count = StandardLibrary::GetBindingCatalog().GetNativeFunctions().size();
		const auto First = Natives && Natives->size() >= Count ? Natives->size() - Count
															   : std::numeric_limits<std::size_t>::max();
		const bool Standard = NativeIndex >= First;
		std::uint16_t ReferenceIndex = NativeIndex;
		if (!Standard && NativeReferences && Natives && NativeIndex < Natives->size())
		{
			const auto Reference = DescribeNativeFunction((*Natives)[NativeIndex]);
			const auto Found = std::find(NativeReferences->begin(), NativeReferences->end(), Reference);
			if (Found == NativeReferences->end())
			{
				if (NativeReferences->size() >= 0xFFFF)
				{
					throw std::length_error("native reference table overflow");
				}
				ReferenceIndex = static_cast<std::uint16_t>(NativeReferences->size());
				NativeReferences->push_back(Reference);
			}
			else
			{
				ReferenceIndex = static_cast<std::uint16_t>(Found - NativeReferences->begin());
			}
		}
		Bytecode::WriteU8(
			Code, static_cast<std::uint8_t>(Standard ? EOpcode::CallMemory : EOpcode::CallNative));
		Bytecode::WriteU16LE(Code, Standard ? static_cast<std::uint16_t>(NativeIndex - First) : ReferenceIndex);
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::EmitMakeClosure(
		const std::uint16_t FunctionIndex,
		const std::uint8_t CaptureCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::MakeClosure));
		Bytecode::WriteU16LE(Code, FunctionIndex);
		Bytecode::WriteU8(Code, CaptureCount);
	}

	void VeroScriptCompiler::FEmitter::EmitCallIndirect(const std::uint8_t ArgumentCount)
	{
		RecordOpcodeLocation();
		Bytecode::WriteU8(Code, static_cast<std::uint8_t>(EOpcode::CallIndirect));
		Bytecode::WriteU8(Code, ArgumentCount);
	}

	void VeroScriptCompiler::FEmitter::PatchI32(const std::size_t OperandOffset, const std::int32_t Value)
	{
		const auto Unsigned = static_cast<std::uint32_t>(Value);
		Code[OperandOffset] = static_cast<std::uint8_t>(Unsigned & 0xFFu);
		Code[OperandOffset + 1] = static_cast<std::uint8_t>((Unsigned >> 8) & 0xFFu);
		Code[OperandOffset + 2] = static_cast<std::uint8_t>((Unsigned >> 16) & 0xFFu);
		Code[OperandOffset + 3] = static_cast<std::uint8_t>((Unsigned >> 24) & 0xFFu);
	}

	std::int32_t VeroScriptCompiler::FEmitter::CurrentOffset() const
	{
		return static_cast<std::int32_t>(Code.size());
	}

	void VeroScriptCompiler::FEmitter::AppendCode(
		const std::vector<std::uint8_t>& Bytes,
		const std::vector<FDebugLine>* const SourceLines)
	{
		const std::size_t BeginOffset = Code.size();
		if (!Bytes.empty())
		{
			RecordOpcodeLocation();
		}
		Code.insert(Code.end(), Bytes.begin(), Bytes.end());
		if (DebugLines && SourceLines)
		{
			const std::size_t Count = (std::min)(Bytes.size(), SourceLines->size());
			if (Count != 0 && DebugLines->size() < BeginOffset + Count)
			{
				DebugLines->resize(BeginOffset + Count);
			}
			for (std::size_t Offset = 0; Offset < Count; ++Offset)
			{
				if ((*SourceLines)[Offset].Line != 0)
				{
					(*DebugLines)[BeginOffset + Offset] = (*SourceLines)[Offset];
				}
			}
		}
	}

	void VeroScriptCompiler::FEmitter::RecordOpcodeLocation()
	{
		if (!DebugLines || !EmitLocation || EmitLocation->Line == 0)
		{
			return;
		}
		const std::size_t Ip = Code.size();
		if (DebugLines->size() <= Ip)
		{
			DebugLines->resize(Ip + 1);
		}
		FDebugLine& Line = (*DebugLines)[Ip];
		Line.Line = EmitLocation->Line;
		Line.Column = EmitLocation->Column;
		Line.FileIndex = InternDebugFile(EmitLocation->FileLabel);
	}

	std::uint16_t VeroScriptCompiler::FEmitter::InternDebugFile(const std::string_view FileLabel)
	{
		if (!DebugFiles)
		{
			return 0;
		}
		if (DebugFiles->empty())
		{
			DebugFiles->emplace_back();
		}
		if (FileLabel.empty())
		{
			return 0;
		}
		for (std::uint16_t Index = 1; Index < DebugFiles->size(); ++Index)
		{
			if ((*DebugFiles)[Index] == FileLabel)
			{
				return Index;
			}
		}
		if (DebugFiles->size() >= 0xFFFFu)
		{
			return 0;
		}
		DebugFiles->emplace_back(FileLabel);
		return static_cast<std::uint16_t>(DebugFiles->size() - 1);
	}

}
