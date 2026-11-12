// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeIO.h"
#include "Processing/VeroScriptSourceMetadataIO.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"
#include "TypeSupport/VeroScriptTemplateMetadataValidation.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptMemory.h"
#include "VeroScriptNative.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptPath.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptStandardLibrary.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <type_traits>

namespace VeroScript
{
	namespace Detail
	{
		struct FAnnotationImageBudget
		{
			std::size_t Bytes = 0, Entries = 0;
		};
		thread_local FAnnotationImageBudget* AnnotationImageBudget = nullptr;
		struct FAnnotationImageGuard
		{
			FAnnotationImageGuard()
			{
				AnnotationImageBudget = &Budget;
			}

			~FAnnotationImageGuard()
			{
				AnnotationImageBudget = Previous;
			}

			FAnnotationImageBudget Budget;

			FAnnotationImageBudget* Previous = AnnotationImageBudget;
		};
		std::size_t AnnotationBytesRemaining()
		{
			return 64u * 1024u * 1024u - (AnnotationImageBudget ? AnnotationImageBudget->Bytes : 0);
		}
		bool ReserveAnnotationEntries(std::size_t Count)
		{
			if (!AnnotationImageBudget) return Count <= 65536;
			if (Count > 65536 - AnnotationImageBudget->Entries) return false;
			AnnotationImageBudget->Entries += Count;
			return true;
		}
		bool ConsumeAnnotationBytes(std::size_t Count)
		{
			if (Count > AnnotationBytesRemaining()) return false;
			if (AnnotationImageBudget)
			{
				AnnotationImageBudget->Bytes += Count;
			}

			return true;
		}

		constexpr bool CanReadBytes(
		    const std::size_t Size,
		    const std::size_t Offset,
		    const std::size_t Count) noexcept
		{
			return Offset <= Size && Count <= Size - Offset;
		}

		bool WriteString(std::vector<std::uint8_t>& Buffer, const std::string& Value)
		{
			if (Value.size() > 0xFFFFFFFFu)
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Value.size()));
			for (const char Character : Value)
			{
				Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Character));
			}
			return true;
		}

		bool ReadString(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::string& OutValue,
		    std::string& OutError)
		{
			if (Offset > Size || Size - Offset < 4)
			{
				OutError = "unexpected end of bytecode while reading string length";
				return false;
			}
			const std::uint32_t Length = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (Length > Size - Offset)
			{
				OutError = "unexpected end of bytecode while reading string data";
				return false;
			}
			OutValue.assign(reinterpret_cast<const char*>(Data + Offset), Length);
			Offset += Length;
			return true;
		}

		void WriteConstant(std::vector<std::uint8_t>& Buffer, const FBytecodeConstant& Constant)
		{
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Constant.Kind));
			switch (Constant.Kind)
			{
			case EConstantKind::I8:
				{
					Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Constant.I8));
					break;
				}
			case EConstantKind::U8:
				{
					Bytecode::WriteU8(Buffer, Constant.U8);
					break;
				}
			case EConstantKind::I16:
				{
					Bytecode::WriteU16LE(Buffer, static_cast<std::uint16_t>(Constant.I16));
					break;
				}
			case EConstantKind::U16:
				{
					Bytecode::WriteU16LE(Buffer, Constant.U16);
					break;
				}
			case EConstantKind::I32:
				{
					Bytecode::WriteI32LE(Buffer, Constant.I32);
					break;
				}
			case EConstantKind::U32:
				{
					Bytecode::WriteU32LE(Buffer, Constant.U32);
					break;
				}
			case EConstantKind::I64:
				{
					Bytecode::WriteU64LE(Buffer, static_cast<std::uint64_t>(Constant.I64));
					break;
				}
			case EConstantKind::U64:
				{
					Bytecode::WriteU64LE(Buffer, Constant.U64);
					break;
				}
			case EConstantKind::F64:
				{
					Bytecode::WriteF64LE(Buffer, Constant.F64);
					break;
				}
			case EConstantKind::F32:
				{
					Bytecode::WriteF32LE(Buffer, Constant.F32);
					break;
				}
			case EConstantKind::Bool:
				{
					Bytecode::WriteU8(Buffer, Constant.Bool ? 1 : 0);
					break;
				}
			case EConstantKind::Str:
				{
					Bytecode::WriteU16LE(Buffer, Constant.StringIndex);
					Bytecode::WriteU8(Buffer, Constant.bIsLinkageString ? 1u : 0u);
					break;
				}
			case EConstantKind::WStr:
				{
					Bytecode::WriteU16LE(Buffer, Constant.StringIndex);
					break;
				}
			case EConstantKind::Enum:
				{
					Bytecode::WriteU64LE(
					    Buffer, static_cast<std::uint64_t>(Constant.I64));
					Bytecode::WriteU16LE(Buffer, Constant.StringIndex);
					break;
				}
			default:
				{
					break;
				}
			}
		}

		bool ReadConstant(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    FBytecodeConstant& OutConstant,
		    std::string& OutError)
		{
			if (!CanReadBytes(Size, Offset, 1))
			{
				OutError = "unexpected end of bytecode while reading constant";
				return false;
			}
			OutConstant.Kind = static_cast<EConstantKind>(Data[Offset++]);
			OutConstant.bIsLinkageString = false;
			switch (OutConstant.Kind)
			{
			case EConstantKind::I8:
				{
					if (!CanReadBytes(Size, Offset, 1))
					{
						OutError = "unexpected end of bytecode while reading i8 constant";
						return false;
					}
					OutConstant.I8 = static_cast<std::int8_t>(Data[Offset++]);
					return true;
				}
			case EConstantKind::U8:
				{
					if (!CanReadBytes(Size, Offset, 1))
					{
						OutError = "unexpected end of bytecode while reading u8 constant";
						return false;
					}
					OutConstant.U8 = Data[Offset++];
					return true;
				}
			case EConstantKind::I16:
				{
					if (!CanReadBytes(Size, Offset, 2))
					{
						OutError = "unexpected end of bytecode while reading i16 constant";
						return false;
					}
					OutConstant.I16 = static_cast<std::int16_t>(Bytecode::ReadU16LE(Data + Offset));
					Offset += 2;
					return true;
				}
			case EConstantKind::U16:
				{
					if (!CanReadBytes(Size, Offset, 2))
					{
						OutError = "unexpected end of bytecode while reading u16 constant";
						return false;
					}
					OutConstant.U16 = Bytecode::ReadU16LE(Data + Offset);
					Offset += 2;
					return true;
				}
			case EConstantKind::I32:
				{
					if (!CanReadBytes(Size, Offset, 4))
					{
						OutError = "unexpected end of bytecode while reading i32 constant";
						return false;
					}
					OutConstant.I32 = Bytecode::ReadI32LE(Data + Offset);
					Offset += 4;
					return true;
				}
			case EConstantKind::U32:
				{
					if (!CanReadBytes(Size, Offset, 4))
					{
						OutError = "unexpected end of bytecode while reading u32 constant";
						return false;
					}
					OutConstant.U32 = Bytecode::ReadU32LE(Data + Offset);
					Offset += 4;
					return true;
				}
			case EConstantKind::I64:
				{
					if (!CanReadBytes(Size, Offset, 8))
					{
						OutError = "unexpected end of bytecode while reading i64 constant";
						return false;
					}
					OutConstant.I64 =
					    static_cast<std::int64_t>(Bytecode::ReadU64LE(Data + Offset));
					Offset += 8;
					return true;
				}
			case EConstantKind::U64:
				{
					if (!CanReadBytes(Size, Offset, 8))
					{
						OutError = "unexpected end of bytecode while reading u64 constant";
						return false;
					}
					OutConstant.U64 = Bytecode::ReadU64LE(Data + Offset);
					Offset += 8;
					return true;
				}
			case EConstantKind::F64:
				{
					if (!CanReadBytes(Size, Offset, 8))
					{
						OutError = "unexpected end of bytecode while reading f64 constant";
						return false;
					}
					OutConstant.F64 = Bytecode::ReadF64LE(Data + Offset);
					Offset += 8;
					return true;
				}
			case EConstantKind::F32:
				{
					if (!CanReadBytes(Size, Offset, 4))
					{
						OutError = "unexpected end of bytecode while reading f32 constant";
						return false;
					}
					OutConstant.F32 = Bytecode::ReadF32LE(Data + Offset);
					Offset += 4;
					return true;
				}
			case EConstantKind::Bool:
				{
					if (!CanReadBytes(Size, Offset, 1))
					{
						OutError = "unexpected end of bytecode while reading bool constant";
						return false;
					}
					if (Data[Offset] > 1u)
					{
						OutError = "invalid bool constant encoding";
						return false;
					}
					OutConstant.Bool = Data[Offset++] != 0;
					return true;
				}
			case EConstantKind::Str:
			case EConstantKind::WStr:
				{
					const std::size_t Bytes = OutConstant.Kind == EConstantKind::Str ? 3u : 2u;
					if (!CanReadBytes(Size, Offset, Bytes))
					{
						OutError = "unexpected end of bytecode while reading string constant";
						return false;
					}
					OutConstant.StringIndex = Bytecode::ReadU16LE(Data + Offset);
					Offset += 2;
					if (OutConstant.Kind == EConstantKind::Str)
					{
						if (Data[Offset] > 1u)
						{
							OutError = "invalid string constant linkage role";
							return false;
						}
						OutConstant.bIsLinkageString = Data[Offset++] != 0u;
					}
					return true;
				}
			case EConstantKind::Enum:
				{
					if (!CanReadBytes(Size, Offset, 10))
					{
						OutError = "unexpected end of bytecode while reading enum constant";
						return false;
					}
					OutConstant.I64 =
					    static_cast<std::int64_t>(Bytecode::ReadU64LE(Data + Offset));
					OutConstant.StringIndex = Bytecode::ReadU16LE(Data + Offset + 8);
					Offset += 10;
					return true;
				}
			default:
				{
					OutError = "invalid constant kind in bytecode";
					return false;
				}
			}
		}

		bool WriteWString(std::vector<std::uint8_t>& Buffer, const std::u16string& Value)
		{
			if (Value.size() > 0xFFFFFFFFu)
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Value.size()));
			for (const char16_t Character : Value)
			{
				Bytecode::WriteU16LE(Buffer, static_cast<std::uint16_t>(Character));
			}
			return true;
		}

		bool ReadWString(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::u16string& OutValue,
		    std::string& OutError)
		{
			if (Offset > Size || Size - Offset < 4)
			{
				OutError = "unexpected end of bytecode while reading wstring length";
				return false;
			}
			const std::uint32_t Length = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (static_cast<std::size_t>(Length) > (Size - Offset) / 2)
			{
				OutError = "unexpected end of bytecode while reading wstring data";
				return false;
			}
			OutValue.clear();
			OutValue.reserve(Length);
			for (std::uint32_t Index = 0; Index < Length; ++Index)
			{
				OutValue.push_back(static_cast<char16_t>(Bytecode::ReadU16LE(Data + Offset)));
				Offset += 2;
			}
			return true;
		}

		bool WriteBytes(
		    std::vector<std::uint8_t>& Buffer,
		    const std::vector<std::uint8_t>& Bytes)
		{
			if (Bytes.size() > std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Bytes.size()));
			for (const std::uint8_t Byte : Bytes)
			{
				Bytecode::WriteU8(Buffer, Byte);
			}
			return true;
		}

		bool ReadBytes(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::vector<std::uint8_t>& OutBytes,
		    std::string& OutError)
		{
			if (Offset > Size || Size - Offset < 4)
			{
				OutError = "unexpected end of bytecode while reading byte length";
				return false;
			}
			const std::uint32_t Length = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (Length > Size - Offset)
			{
				OutError = "unexpected end of bytecode while reading byte data";
				return false;
			}
			OutBytes.assign(Data + Offset, Data + Offset + Length);
			Offset += Length;
			return true;
		}

		// Reject pathological section sizes that cannot fit in remaining bytes.
		constexpr std::uint32_t kMaxBytecodeSectionEntries = 1u << 22; // 4M

		bool ReadCount(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::uint32_t& OutCount,
		    std::string& OutError,
		    const std::size_t MinBytesPerEntry = 1)
		{
			if (Offset > Size || Size - Offset < 4)
			{
				OutError = "unexpected end of bytecode while reading section count";
				return false;
			}
			OutCount = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (OutCount > kMaxBytecodeSectionEntries)
			{
				OutError = "bytecode section count exceeds safety limit";
				return false;
			}
			if (MinBytesPerEntry > 0 && OutCount > 0)
			{
				const std::size_t Remaining = Size - Offset;
				if (OutCount > Remaining / MinBytesPerEntry)
				{
					OutError = "bytecode section count exceeds remaining file size";
					return false;
				}
			}
			return true;
		}

		bool WriteTemplateParameters(std::vector<std::uint8_t>& Buffer, const FTemplateParameterList& List)
		{
			const auto& Parameters = List.GetParameters();
			if (Parameters.size() > 65536) return false;
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Parameters.size()));
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(List.GetEnclosingCount()));
			for (const auto& Parameter : Parameters)
			{
				Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Parameter.Kind));
				for (const auto* Value : {&Parameter.Id, &Parameter.Name, &Parameter.Namespace,
				         &Parameter.ValueTypeName, &Parameter.DefaultTypeName, &Parameter.DefaultSource})
				{
					if (!WriteString(Buffer, *Value)) return false;
				}
				Bytecode::WriteU32LE(Buffer, Parameter.Location.Line);
				Bytecode::WriteU32LE(Buffer, Parameter.Location.Column);
			}
			return true;
		}

		bool ReadTemplateParameters(const std::uint8_t* Data, const std::size_t Size, std::size_t& Offset,
		    FTemplateParameterList& List, std::string& Error)
		{
			std::uint32_t ParameterCount = 0;
			if (!ReadCount(Data, Size, Offset, ParameterCount, Error, 33) || ParameterCount > 65536 || !CanReadBytes(Size, Offset, 4))
			{
				Error = "invalid template parameter count";
				return false;
			}
			const auto EnclosingCount = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (EnclosingCount > ParameterCount)
			{
				Error = "invalid enclosing template parameter count";
				return false;
			}
			std::vector<FTemplateParameter> Parameters;
			std::unordered_set<std::string> ParameterNames;
			for (std::uint32_t Index = 0; Index < ParameterCount; ++Index)
			{
				if (!CanReadBytes(Size, Offset, 1) || Data[Offset] > static_cast<std::uint8_t>(ETemplateParameterKind::Value))
				{
					Error = "invalid template parameter kind";
					return false;
				}
				FTemplateParameter Parameter;
				Parameter.Kind = static_cast<ETemplateParameterKind>(Data[Offset++]);
				for (auto* Value : {&Parameter.Id, &Parameter.Name, &Parameter.Namespace,
				         &Parameter.ValueTypeName, &Parameter.DefaultTypeName, &Parameter.DefaultSource})
				{
					if (!ReadString(Data, Size, Offset, *Value, Error)) return false;
				}
				if (Parameter.Name.empty() || !ParameterNames.insert(Parameter.Name).second || (Parameter.Kind == ETemplateParameterKind::Type && (!Parameter.ValueTypeName.empty() || (!Parameter.DefaultSource.empty() && Parameter.DefaultSource != Parameter.DefaultTypeName))) || (Parameter.Kind == ETemplateParameterKind::Value && (Parameter.ValueTypeName.empty() || !Parameter.DefaultTypeName.empty())))
				{
					Error = "invalid type scope template parameter";
					return false;
				}
				if (!CanReadBytes(Size, Offset, 8))
				{
					Error = "truncated template parameter position";
					return false;
				}
				Parameter.Location.Line = Bytecode::ReadU32LE(Data + Offset);
				Parameter.Location.Column = Bytecode::ReadU32LE(Data + Offset + 4);
				Offset += 8;
				Parameters.push_back(std::move(Parameter));
			}
			List.Assign(std::move(Parameters), EnclosingCount);
			return true;
		}

		bool WriteTemplateInstance(std::vector<std::uint8_t>& Buffer, const FTemplateInstanceInfo& Instance)
		{
			if (!WriteString(Buffer, Instance.DeclarationId)) return false;
			for (const auto* Values : {&Instance.Arguments, &Instance.EnclosingArguments, &Instance.ValueTypeNames})
			{
				if (Values->size() > 65536) return false;
				Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Values->size()));
				for (const auto& Value : *Values)
				{
					if (!WriteString(Buffer, Value)) return false;
				}
			}
			return true;
		}

		bool ReadTemplateInstance(const std::uint8_t* Data, const std::size_t Size, std::size_t& Offset,
		    FTemplateInstanceInfo& Instance, std::string& Error)
		{
			if (!ReadString(Data, Size, Offset, Instance.DeclarationId, Error)) return false;
			for (auto* Values : {&Instance.Arguments, &Instance.EnclosingArguments, &Instance.ValueTypeNames})
			{
				std::uint32_t Count = 0;
				if (!ReadCount(Data, Size, Offset, Count, Error, 4) || Count > 65536)
				{
					Error = "invalid template instance argument count";
					return false;
				}
				for (std::uint32_t Index = 0; Index < Count; ++Index)
				{
					std::string Value;
					if (!ReadString(Data, Size, Offset, Value, Error)) return false;
					Values->push_back(std::move(Value));
				}
			}
			return true;
		}

		bool WriteTypeScope(std::vector<std::uint8_t>& Buffer, const FTypeDeclarationScope& Scope)
		{
			for (const auto* Value : {&Scope.Id, &Scope.ScopeId, &Scope.ParentScopeId, &Scope.SimpleName,
			         &Scope.QualifiedName, &Scope.OwnerTypeName, &Scope.OwnerFunctionName, &Scope.AccessTypeName,
			         &Scope.AccessModulePath, &Scope.ModulePath, &Scope.Kind, &Scope.DisplayName})
			{
				if (!WriteString(Buffer, *Value)) return false;
			}
			for (const auto* Values : {&Scope.EnclosingTypeParameters, &Scope.FunctionTypeParameters,
			         &Scope.ConcreteTypeArguments, &Scope.ForbiddenRuntimeNames})
			{
				if (Values->size() > 65536) return false;
				Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Values->size()));
				for (const auto& Value : *Values)
				{
					if (!WriteString(Buffer, Value)) return false;
				}
			}
			Bytecode::WriteU32LE(Buffer, Scope.OwnTemplateParameterCount);
			if (!WriteTemplateParameters(Buffer, Scope.EnclosingTemplateParameters)) return false;
			Bytecode::WriteU32LE(Buffer, Scope.OwnTypeParameterCount);
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Scope.Access));
			Bytecode::WriteU8(Buffer, Scope.bLocal ? 1u : 0u);
			Bytecode::WriteU8(Buffer, Scope.bExported ? 1u : 0u);
			Bytecode::WriteU8(Buffer, Scope.bPublicPath ? 1u : 0u);
			return true;
		}

		bool ReadTypeScope(const std::uint8_t* Data, const std::size_t Size, std::size_t& Offset,
		    FTypeDeclarationScope& Scope, std::string& Error)
		{
			for (auto* Value : {&Scope.Id, &Scope.ScopeId, &Scope.ParentScopeId, &Scope.SimpleName,
			         &Scope.QualifiedName, &Scope.OwnerTypeName, &Scope.OwnerFunctionName, &Scope.AccessTypeName,
			         &Scope.AccessModulePath, &Scope.ModulePath, &Scope.Kind, &Scope.DisplayName})
			{
				if (!ReadString(Data, Size, Offset, *Value, Error)) return false;
			}
			for (auto* Values : {&Scope.EnclosingTypeParameters, &Scope.FunctionTypeParameters,
			         &Scope.ConcreteTypeArguments, &Scope.ForbiddenRuntimeNames})
			{
				std::uint32_t Count = 0;
				if (!ReadCount(Data, Size, Offset, Count, Error, 4) || Count > 65536) return false;
				for (std::uint32_t Index = 0; Index < Count; ++Index)
				{
					std::string Value;
					if (!ReadString(Data, Size, Offset, Value, Error)) return false;
					Values->push_back(std::move(Value));
				}
			}
			if (!CanReadBytes(Size, Offset, 4)) return false;
			Scope.OwnTemplateParameterCount = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (!ReadTemplateParameters(Data, Size, Offset, Scope.EnclosingTemplateParameters, Error)) return false;
			if (!CanReadBytes(Size, Offset, 8) || Data[Offset + 4] > static_cast<std::uint8_t>(EMemberAccess::Private) || Data[Offset + 5] > 1 || Data[Offset + 6] > 1 || Data[Offset + 7] > 1)
			{
				Error = "invalid type scope flags";
				return false;
			}
			Scope.OwnTypeParameterCount = Bytecode::ReadU32LE(Data + Offset);
			Scope.Access = static_cast<EMemberAccess>(Data[Offset + 4]);
			Scope.bLocal = Data[Offset + 5] != 0;
			Scope.bExported = Data[Offset + 6] != 0;
			Scope.bPublicPath = Data[Offset + 7] != 0;
			Offset += 8;
			return true;
		}

		bool WritePauseMarkers(std::vector<std::uint8_t>& Buffer, const std::vector<std::uint32_t>& Markers)
		{
			if (Markers.size() > kMaxBytecodeSectionEntries) return false;
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Markers.size()));
			for (const auto Offset : Markers)
			{
				Bytecode::WriteU32LE(Buffer, Offset);
			}
			return true;
		}

		bool ReadPauseMarkers(const std::uint8_t* Data, std::size_t Size, std::size_t& Offset,
		    std::vector<std::uint32_t>& Out, std::string& Error)
		{
			std::uint32_t Count = 0;
			if (!ReadCount(Data, Size, Offset, Count, Error, 4)) return false;
			Out.reserve(Count);
			for (std::uint32_t I = 0; I < Count; ++I, Offset += 4)
			{
				Out.push_back(Bytecode::ReadU32LE(Data + Offset));
			}
			return true;
		}

		bool ValidatePauseMarkers(const std::vector<std::uint8_t>& Code,
		    const std::vector<std::uint32_t>& Markers,
		    const std::vector<std::uint8_t>& Boundaries, std::string& Error)
		{
			if (Markers.size() > kMaxBytecodeSectionEntries || Markers.size() > Code.size())
			{
				Error = "pause marker count exceeds bytecode safety limit";
				return false;
			}
			std::size_t Previous = 0;
			bool First = true;
			for (const auto Ip : Markers)
			{
				if ((!First && Ip <= Previous) || Ip >= Code.size() || Code.size() - Ip <= 1 ||
				    Ip >= Boundaries.size() || !Boundaries[Ip] ||
				    static_cast<EOpcode>(Code[Ip]) != EOpcode::Nop ||
				    static_cast<EOpcode>(Code[Ip + 1]) == EOpcode::Nop)
				{
					Error = "invalid pause marker offset";
					return false;
				}
				First = false;
				Previous = Ip;
			}
			return true;
		}

		// Parallel opcode→source mapping (sparse slots with Line==0 allowed).
		bool WriteDebugLines(
		    std::vector<std::uint8_t>& Buffer,
		    const std::vector<FDebugLine>& Lines)
		{
			if (Lines.size() > kMaxBytecodeSectionEntries)
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Lines.size()));
			for (const FDebugLine& Line : Lines)
			{
				Bytecode::WriteU32LE(Buffer, Line.Line);
				Bytecode::WriteU32LE(Buffer, Line.Column);
				Bytecode::WriteU16LE(Buffer, Line.FileIndex);
			}
			return true;
		}

		bool ReadDebugLines(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::vector<FDebugLine>& OutLines,
		    std::string& OutError)
		{
			std::uint32_t Count = 0;
			if (!ReadCount(Data, Size, Offset, Count, OutError, 10))
			{
				return false;
			}
			OutLines.clear();
			OutLines.reserve(Count);
			for (std::uint32_t Index = 0; Index < Count; ++Index)
			{
				if (!CanReadBytes(Size, Offset, 10))
				{
					OutError = "unexpected end of bytecode while reading debug line";
					return false;
				}
				FDebugLine Line;
				Line.Line = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				Line.Column = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				Line.FileIndex = Bytecode::ReadU16LE(Data + Offset);
				Offset += 2;
				OutLines.push_back(Line);
			}
			return true;
		}

		bool WriteStringList(
		    std::vector<std::uint8_t>& Buffer,
		    const std::vector<std::string>& Values)
		{
			if (Values.size() > kMaxBytecodeSectionEntries)
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Values.size()));
			for (const std::string& Value : Values)
			{
				if (!WriteString(Buffer, Value))
				{
					return false;
				}
			}
			return true;
		}

		bool ValidAnnotationText(std::string_view Text)
		{
			for (std::size_t I = 0; I < Text.size();)
			{
				const auto C = static_cast<unsigned char>(Text[I++]);
				if (C < 0x80)
				{
					continue;
				}

				unsigned N = C >= 0xF0 && C <= 0xF4 ? 3
				    : C >= 0xE0 && C <= 0xEF        ? 2
				    : C >= 0xC2 && C <= 0xDF        ? 1
				                                    : 0;
				if (!N || N > Text.size() - I) return false;
				std::uint32_t Value = C & ((1u << (6 - N)) - 1);
				for (unsigned J = 0; J < N; ++J)
				{
					const auto B = static_cast<unsigned char>(Text[I++]);
					if ((B & 0xC0) != 0x80) return false;
					Value = (Value << 6) | (B & 63);
				}
				if ((N == 1 && Value < 0x80) || (N == 2 && Value < 0x800) || (N == 3 && Value < 0x10000) ||
				    Value > 0x10FFFF || (Value >= 0xD800 && Value <= 0xDFFF))
				{
					return false;
				}
			}
			return true;
		}
		bool ReadAnnotationString(const std::uint8_t* D, std::size_t Size, std::size_t& Offset, std::string& Value, std::string& Error)
		{
			if (!CanReadBytes(Size, Offset, 4) || Bytecode::ReadU32LE(D + Offset) > 1024 * 1024)
			{
				Error = "annotation string exceeds safety limit";
				return false;
			}
			return ReadString(D, Size, Offset, Value, Error);
		}
		bool InvalidAnnotationImage(std::string& Error, const char* Message)
		{
			if (Error.empty())
			{
				Error = Message;
			}

			return false;
		}

		bool WriteAnnotationPosition(std::vector<std::uint8_t>& B, const FAnnotationSourcePosition& P)
		{
			if (P.FileLabel.size() > 1024 * 1024 || !P.Line || !P.Column || !ValidAnnotationText(P.FileLabel) || !WriteString(B, P.FileLabel)) return false;
			Bytecode::WriteU32LE(B, P.Line);
			Bytecode::WriteU32LE(B, P.Column);
			return true;
		}
		bool ReadAnnotationPosition(const std::uint8_t* D, std::size_t Size, std::size_t& Offset, FAnnotationSourcePosition& P, std::string& Error)
		{
			if (!ReadAnnotationString(D, Size, Offset, P.FileLabel, Error) ||
			    !ValidAnnotationText(P.FileLabel) || !CanReadBytes(Size, Offset, 8))
			{
				return InvalidAnnotationImage(Error, "invalid annotation source position");
			}

			P.Line = Bytecode::ReadU32LE(D + Offset);
			P.Column = Bytecode::ReadU32LE(D + Offset + 4);
			Offset += 8;
			return (P.Line && P.Column) || InvalidAnnotationImage(Error, "invalid annotation source position");
		}
		bool WriteAnnotationArguments(std::vector<std::uint8_t>& B, const std::vector<FAnnotationArgument>& Args, std::size_t Depth, std::size_t& Nodes)
		{
			if ((Depth >= 64 && !Args.empty()) || Args.size() > 4096 || Nodes > 4096 - Args.size()) return false;
			Nodes += Args.size();
			Bytecode::WriteU32LE(B, static_cast<std::uint32_t>(Args.size()));
			for (const auto& A : Args)
			{
				if (A.Name.size() > 1024 * 1024 || A.RawText.size() > 1024 * 1024 || A.Value.size() > 1024 * 1024 || A.Name.empty() || static_cast<unsigned>(A.Kind) > 2 || static_cast<unsigned>(A.ScalarKind) > 4 || !ValidAnnotationText(A.Name) || !ValidAnnotationText(A.RawText) || !ValidAnnotationText(A.Value)) return false;
				if ((A.Kind != EAnnotationArgumentKind::Group && !A.Children.empty()) || (A.Kind != EAnnotationArgumentKind::Scalar && (!A.Value.empty() || !A.RawText.empty()))) return false;
				if (!WriteString(B, A.Name) || !WriteString(B, A.RawText) || !WriteString(B, A.Value)) return false;
				Bytecode::WriteU8(B, static_cast<std::uint8_t>(A.Kind));
				Bytecode::WriteU8(B, static_cast<std::uint8_t>(A.ScalarKind));
				if (!WriteAnnotationPosition(B, A.Range.Begin) || !WriteAnnotationPosition(B, A.Range.End) || !WriteAnnotationArguments(B, A.Children, Depth + 1, Nodes)) return false;
			}
			return true;
		}
		bool ReadAnnotationArguments(const std::uint8_t* D, std::size_t Size, std::size_t& Offset, std::vector<FAnnotationArgument>& Args, std::size_t Depth, std::size_t& Nodes, std::string& Error)
		{
			std::uint32_t Count = 0;
			if (!ReadCount(D, Size, Offset, Count, Error, 26) || (Depth >= 64 && Count) || Count > 4096 ||
			    Nodes > 4096 - Count)
			{
				return InvalidAnnotationImage(Error, "annotation argument budget exceeded");
			}

			Nodes += Count;
			Args.reserve(Count);
			for (std::uint32_t I = 0; I < Count; ++I)
			{
				FAnnotationArgument A;
				if (!ReadAnnotationString(D, Size, Offset, A.Name, Error) ||
				    !ReadAnnotationString(D, Size, Offset, A.RawText, Error) ||
				    !ReadAnnotationString(D, Size, Offset, A.Value, Error) || !CanReadBytes(Size, Offset, 2))
				{
					return InvalidAnnotationImage(Error, "invalid annotation argument");
				}

				A.Kind = static_cast<EAnnotationArgumentKind>(D[Offset++]);
				A.ScalarKind = static_cast<EAnnotationScalarKind>(D[Offset++]);
				if (A.Name.size() > 1024 * 1024 || A.RawText.size() > 1024 * 1024 ||
				    A.Value.size() > 1024 * 1024 || A.Name.empty() || static_cast<unsigned>(A.Kind) > 2 ||
				    static_cast<unsigned>(A.ScalarKind) > 4 || !ValidAnnotationText(A.Name) ||
				    !ValidAnnotationText(A.RawText) || !ValidAnnotationText(A.Value) ||
				    !ReadAnnotationPosition(D, Size, Offset, A.Range.Begin, Error) ||
				    !ReadAnnotationPosition(D, Size, Offset, A.Range.End, Error) ||
				    !ReadAnnotationArguments(D, Size, Offset, A.Children, Depth + 1, Nodes, Error))
				{
					return InvalidAnnotationImage(Error, "invalid annotation argument");
				}

				if ((A.Kind != EAnnotationArgumentKind::Group && !A.Children.empty()) ||
				    (A.Kind != EAnnotationArgumentKind::Scalar && (!A.Value.empty() || !A.RawText.empty())))
				{
					return InvalidAnnotationImage(Error, "invalid annotation argument");
				}

				Args.push_back(std::move(A));
			}
			return true;
		}
		bool WriteGeneratedOperations(std::vector<std::uint8_t>& B, const std::vector<FAnnotationGenerationOperation>& Ops)
		{
			if (!ReserveAnnotationEntries(Ops.size())) return false;
			const auto Start = B.size();
			Bytecode::WriteU32LE(B, static_cast<std::uint32_t>(Ops.size()));
			std::unordered_set<std::string> Ids;
			for (const auto& Op : Ops)
			{
				if ((Op.Kind != EAnnotationOperation::AddMethod && Op.Kind != EAnnotationOperation::CompleteMethodBody) || static_cast<unsigned>(Op.Access) > 2 || Op.TargetId.size() > 1024 * 1024 || Op.ProcessorId.size() > 1024 * 1024 || Op.GenerationId.size() > 1024 * 1024 || Op.TargetId.empty() || Op.GenerationId.empty() || !Ids.insert(Op.GenerationId).second || Op.Source.empty() || Op.Source.size() > 1024 * 1024 || !ValidAnnotationText(Op.Source)) return false;
				Bytecode::WriteU8(B, static_cast<std::uint8_t>(Op.Kind));
				Bytecode::WriteU8(B, static_cast<std::uint8_t>(Op.Access));
				if (!WriteString(B, Op.TargetId) || !WriteString(B, Op.Source) || !WriteString(B, Op.ProcessorId) || !WriteString(B, Op.GenerationId) || !WriteAnnotationPosition(B, Op.Origin) || B.size() - Start > 64 * 1024 * 1024) return false;
			}
			return ConsumeAnnotationBytes(B.size() - Start);
		}
		bool ReadGeneratedOperations(const std::uint8_t* D, std::size_t Size, std::size_t& Offset, std::vector<FAnnotationGenerationOperation>& Ops, std::string& Error)
		{
			std::uint32_t Count = 0;
			const auto Start = Offset;
			Size = Offset <= Size ? Offset + std::min<std::size_t>(Size - Offset, AnnotationBytesRemaining())
			                      : Size;
			if (!ReadCount(D, Size, Offset, Count, Error, 30) || !ReserveAnnotationEntries(Count))
			{
				Error = "annotation image entry budget exceeded";
				return false;
			}
			Ops.reserve(Count);
			std::unordered_set<std::string> Ids;
			for (std::uint32_t I = 0; I < Count; ++I)
			{
				FAnnotationGenerationOperation Op;
				if (!CanReadBytes(Size, Offset, 2))
				{
					return InvalidAnnotationImage(Error, "truncated generated template operation");
				}

				Op.Kind = static_cast<EAnnotationOperation>(D[Offset++]);
				Op.Access = static_cast<EMemberAccess>(D[Offset++]);
				if (!ReadAnnotationString(D, Size, Offset, Op.TargetId, Error) ||
				    !CanReadBytes(Size, Offset, 4) || Bytecode::ReadU32LE(D + Offset) > 1024 * 1024 ||
				    !ReadAnnotationString(D, Size, Offset, Op.Source, Error) ||
				    !ReadAnnotationString(D, Size, Offset, Op.ProcessorId, Error) ||
				    !ReadAnnotationString(D, Size, Offset, Op.GenerationId, Error) ||
				    !ReadAnnotationPosition(D, Size, Offset, Op.Origin, Error))
				{
					return InvalidAnnotationImage(Error, "invalid generated template operation");
				}

				if ((Op.Kind != EAnnotationOperation::AddMethod &&
				        Op.Kind != EAnnotationOperation::CompleteMethodBody) ||
				    static_cast<unsigned>(Op.Access) > 2 || Op.TargetId.size() > 1024 * 1024 ||
				    Op.ProcessorId.size() > 1024 * 1024 || Op.GenerationId.size() > 1024 * 1024 ||
				    Op.TargetId.empty() || Op.GenerationId.empty() || !Ids.insert(Op.GenerationId).second ||
				    Op.Source.empty() || !ValidAnnotationText(Op.Source) || Offset - Start > 64 * 1024 * 1024)
				{
					Error = "invalid generated template operation";
					return false;
				}
				Ops.push_back(std::move(Op));
			}
			return ConsumeAnnotationBytes(Offset - Start) || InvalidAnnotationImage(Error, "annotation image byte budget exceeded");
		}
		bool WriteDeclMeta(
		    std::vector<std::uint8_t>& Buffer,
		    const FDeclMeta& Meta)
		{
			if (Meta.size() > kMaxBytecodeSectionEntries)
			{
				return false;
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Meta.size()));
			for (const auto& Pair : Meta)
			{
				if (!WriteString(Buffer, Pair.first) || !WriteString(Buffer, Pair.second))
				{
					return false;
				}
			}
			if (!ReserveAnnotationEntries(Meta.Annotations.size())) return false;
			const auto Begin = Buffer.size();
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Meta.Annotations.size()));
			for (const auto& A : Meta.Annotations)
			{
				if (A.Name.size() > 1024 * 1024 || A.Name.empty() || !ValidAnnotationText(A.Name) || !WriteString(Buffer, A.Name)) return false;
				Bytecode::WriteU64LE(Buffer, A.Ordinal);
				std::size_t Nodes = 0;
				if (!WriteAnnotationPosition(Buffer, A.Range.Begin) || !WriteAnnotationPosition(Buffer, A.Range.End) || !WriteAnnotationPosition(Buffer, A.NamePosition) || !WriteAnnotationArguments(Buffer, A.Arguments, 0, Nodes) || Buffer.size() - Begin > 64 * 1024 * 1024) return false;
			}
			if (!ConsumeAnnotationBytes(Buffer.size() - Begin)) return false;
			FSourceMetadataSnapshot Extra;
			Extra.Retention = Meta.CommentRetention;
			FSourceNodeMetadata Node;
			Node.Id = Meta.StableId.empty() ? "metadata" : Meta.StableId;
			Node.Range = Meta.SourceRange;
			Node.Annotations = Meta.Annotations;
			Node.Comments = Meta.Comments;
			Node.Documentation = Meta.Documentation;
			Extra.Nodes.push_back(std::move(Node));
			std::vector<std::uint8_t> Encoded;
			std::string Error;
			return EncodeSourceMetadata(Extra, Encoded, Error) && WriteBytes(Buffer, Encoded);
		}

		bool ReadStringList(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    std::vector<std::string>& OutValues,
		    std::string& OutError)
		{
			std::uint32_t Count = 0;
			if (!ReadCount(Data, Size, Offset, Count, OutError, 4))
			{
				return false;
			}
			OutValues.clear();
			OutValues.reserve(Count);
			for (std::uint32_t Index = 0; Index < Count; ++Index)
			{
				std::string Value;
				if (!ReadString(Data, Size, Offset, Value, OutError))
				{
					return false;
				}
				OutValues.push_back(std::move(Value));
			}
			return true;
		}

		bool ReadDeclMeta(
		    const std::uint8_t* Data,
		    const std::size_t Size,
		    std::size_t& Offset,
		    FDeclMeta& OutMeta,
		    std::string& OutError)
		{
			std::uint32_t Count = 0;
			if (!ReadCount(Data, Size, Offset, Count, OutError, 8))
			{
				return false;
			}
			OutMeta.clear();
			OutMeta.Annotations.clear();
			OutMeta.reserve(Count);
			for (std::uint32_t Index = 0; Index < Count; ++Index)
			{
				std::string Key;
				std::string Value;
				if (!ReadString(Data, Size, Offset, Key, OutError) || !ReadString(Data, Size, Offset, Value, OutError))
				{
					return false;
				}
				OutMeta.emplace_back(std::move(Key), std::move(Value));
			}
			const auto Begin = Offset;
			const auto AnnotationSize = Offset <= Size ? Offset + std::min<std::size_t>(Size - Offset, AnnotationBytesRemaining()) : Size;
			std::uint32_t AnnotationCount = 0;
			if (!ReadCount(Data, AnnotationSize, Offset, AnnotationCount, OutError, 48) ||
			    !ReserveAnnotationEntries(AnnotationCount))
			{
				OutError = "annotation image entry budget exceeded";
				return false;
			}
			OutMeta.Annotations.reserve(AnnotationCount);
			for (std::uint32_t I = 0; I < AnnotationCount; ++I)
			{
				FDeclarationAnnotation A;
				if (!ReadAnnotationString(Data, AnnotationSize, Offset, A.Name, OutError) || A.Name.empty() ||
				    !ValidAnnotationText(A.Name) || !CanReadBytes(AnnotationSize, Offset, 8))
				{
					return InvalidAnnotationImage(OutError, "invalid structured annotation header");
				}

				A.Ordinal = Bytecode::ReadU64LE(Data + Offset);
				Offset += 8;
				std::size_t Nodes = 0;
				if (!ReadAnnotationPosition(Data, AnnotationSize, Offset, A.Range.Begin, OutError) ||
				    !ReadAnnotationPosition(Data, AnnotationSize, Offset, A.Range.End, OutError) ||
				    !ReadAnnotationPosition(Data, AnnotationSize, Offset, A.NamePosition, OutError) ||
				    !ReadAnnotationArguments(Data, AnnotationSize, Offset, A.Arguments, 0, Nodes, OutError) ||
				    Offset - Begin > 64 * 1024 * 1024)
				{
					return InvalidAnnotationImage(OutError, "invalid structured annotation");
				}

				OutMeta.Annotations.push_back(std::move(A));
			}
			if (!ConsumeAnnotationBytes(Offset - Begin)) return InvalidAnnotationImage(OutError, "annotation image byte budget exceeded");
			std::uint32_t ExtraSize = 0;
			if (!CanReadBytes(Size, Offset, 4)) return InvalidAnnotationImage(OutError, "truncated metadata length");
			ExtraSize = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			if (ExtraSize > 64u * 1024u * 1024u || !CanReadBytes(Size, Offset, ExtraSize)) return InvalidAnnotationImage(OutError, "invalid metadata length");
			auto Extra = DecodeSourceMetadata(Data + Offset, ExtraSize, OutError);
			if (!Extra || Extra->Nodes.size() != 1) return false;
			Offset += ExtraSize;
			const auto& Node = Extra->Nodes.front();
			OutMeta.StableId = Node.Id == "metadata" ? "" : Node.Id;
			OutMeta.Annotations = Node.Annotations;
			OutMeta.Comments = Node.Comments;
			OutMeta.Documentation = Node.Documentation;
			OutMeta.SourceRange = Node.Range;
			OutMeta.CommentRetention = Extra->Retention;
			return true;
		}

		bool ValidateConstantReference(
		    const FBytecodeModule& Module,
		    const FBytecodeConstant& Constant,
		    const std::string& Context,
		    std::string& OutError)
		{
			if (Constant.bIsLinkageString && Constant.Kind != EConstantKind::Str)
			{
				OutError = Context + ": linkage role requires a string constant";
				return false;
			}
			switch (Constant.Kind)
			{
			case EConstantKind::I8:
			case EConstantKind::U8:
			case EConstantKind::I16:
			case EConstantKind::U16:
			case EConstantKind::I32:
			case EConstantKind::U32:
			case EConstantKind::I64:
			case EConstantKind::U64:
			case EConstantKind::F64:
			case EConstantKind::F32:
			case EConstantKind::Bool:
			case EConstantKind::Str:
			case EConstantKind::WStr:
			case EConstantKind::Enum:
				{
					break;
				}
			default:
				{
					OutError = Context + ": invalid constant kind";
					return false;
				}
			}
			if ((Constant.Kind == EConstantKind::Str || Constant.Kind == EConstantKind::Enum) && Constant.StringIndex >= Module.StringPool.size())
			{
				OutError = Context + ": string constant index out of range";
				return false;
			}
			if (Constant.Kind == EConstantKind::WStr && Constant.StringIndex >= Module.WStringPool.size())
			{
				OutError = Context + ": wstring constant index out of range";
				return false;
			}
			return true;
		}

		bool ValidateCode(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint8_t>& Code,
		    const std::uint16_t LocalCount,
		    const std::string& Context,
		    std::string& OutError,
		    std::vector<std::uint8_t>* const OutInstructionBoundaries = nullptr)
		{
			if (Code.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
			{
				OutError = Context + ": bytecode exceeds signed jump-offset range";
				return false;
			}
			std::vector<std::uint8_t> InstructionBoundary(Code.size() + 1, 0);
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				InstructionBoundary[Ip] = 1;
				if (Code[Ip] > static_cast<std::uint8_t>(EOpcode::GetIndexPathRef))
				{
					OutError = Context + ": invalid opcode at byte " + std::to_string(Ip);
					return false;
				}
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t InstructionSize = Bytecode::InstructionSize(Opcode);
				if (InstructionSize == 0 || InstructionSize > Code.size() - Ip)
				{
					OutError = Context + ": truncated instruction at byte " + std::to_string(Ip);
					return false;
				}
				Ip += InstructionSize;
			}
			InstructionBoundary[Code.size()] = 1;

			const auto FailIndex = [&](const char* Kind, const std::size_t Ip)
			{
				OutError = Context + ": " + Kind + " index out of range at byte " + std::to_string(Ip);
				return false;
			};
			const auto CheckJump = [&](const std::size_t Ip, const std::size_t Size,
			                           const std::size_t OperandOffset) -> bool
			{
				const std::int64_t Target =
				    static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Code.data() + Ip + OperandOffset);
				if (Target < 0 || static_cast<std::size_t>(Target) > Code.size() || !InstructionBoundary[static_cast<std::size_t>(Target)])
				{
					OutError = Context + ": jump target is not an instruction boundary at byte " + std::to_string(Ip);
					return false;
				}
				return true;
			};

			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Code[Ip]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				const auto OperandU16 = [&]()
				{
					return Bytecode::ReadU16LE(Code.data() + Ip + 1);
				};
				switch (Opcode)
				{
				case EOpcode::CallNative:
					{
						if (OperandU16() >= Module.NativeReferences.size() ||
						    Code[Ip + 3] != Module.NativeReferences[OperandU16()].ArgumentCount)
						{
							return FailIndex("native reference", Ip);
						}
						break;
					}
				case EOpcode::CallMemory:
					{
						const auto& Natives = StandardLibrary::GetBindingCatalog().GetNativeFunctions();
						if (OperandU16() >= Natives.size() ||
						    Code[Ip + 3] != NativeRuntimeArgumentCount(Natives[OperandU16()]))
						{
							return FailIndex("memory member", Ip);
						}
						break;
					}
				case EOpcode::ConstI32:
					{
						const std::uint16_t Index = OperandU16();
						if (Index >= Module.Constants.size())
						{
							return FailIndex("constant", Ip);
						}
						const EConstantKind Kind = Module.Constants[Index].Kind;
						if (Kind != EConstantKind::I8 && Kind != EConstantKind::U8 && Kind != EConstantKind::I16 && Kind != EConstantKind::U16 && Kind != EConstantKind::I32 && Kind != EConstantKind::U32 && Kind != EConstantKind::I64 && Kind != EConstantKind::U64 && Kind != EConstantKind::Enum)
						{
							OutError = Context + ": integer opcode references non-integer constant at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::ConstF64:
					{
						if (OperandU16() >= Module.Constants.size())
						{
							return FailIndex("constant", Ip);
						}
						if (Module.Constants[OperandU16()].Kind != EConstantKind::F64 && Module.Constants[OperandU16()].Kind != EConstantKind::F32)
						{
							OutError = Context + ": floating opcode references non-floating constant at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::ConstStr:
					{
						if (OperandU16() >= Module.Constants.size())
						{
							return FailIndex("constant", Ip);
						}
						if (Module.Constants[OperandU16()].Kind != EConstantKind::Str && Module.Constants[OperandU16()].Kind != EConstantKind::WStr)
						{
							OutError = Context + ": string opcode references non-string constant at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::ConstBool:
					{
						if (Code[Ip + 1] > 1)
						{
							OutError = Context + ": invalid bool literal at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::MakeLocalRef:
				case EOpcode::GetIndexRef:
				case EOpcode::GetIndexPathRef:
				case EOpcode::GetMapEntryRef:
				case EOpcode::BindLocal:
				case EOpcode::TakeLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::JmpIfLocalGeImm:
					{
						if (OperandU16() >= LocalCount)
						{
							return FailIndex("local", Ip);
						}
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						if (OperandU16() >= LocalCount || Bytecode::ReadU16LE(Code.data() + Ip + 3) >= LocalCount)
						{
							return FailIndex("local", Ip);
						}
						break;
					}
				case EOpcode::BeginLocalStatic:
				case EOpcode::CommitLocalStatic:
				case EOpcode::LoadGlobal:
				case EOpcode::StoreGlobal:
				case EOpcode::MakeGlobalRef:
					{
						if (Opcode == EOpcode::BeginLocalStatic && !CheckJump(Ip, Size, 3)) return false;
						if (OperandU16() >= Module.GlobalNames.size())
						{
							return FailIndex("global", Ip);
						}
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::StoreField:
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
				case EOpcode::InitializeThisField:
				case EOpcode::LoadFieldKeep:
				case EOpcode::MakeThisFieldRef:
				case EOpcode::MakeFieldRef:
				case EOpcode::IncField:
				case EOpcode::CallMethod:
				case EOpcode::NewNativeArray:
				case EOpcode::NewUninitializedNativeArray:
				case EOpcode::NewDynamicArray:
				case EOpcode::NewMap:
				case EOpcode::NewSet:
				case EOpcode::NewQueue:
				case EOpcode::NewStack:
				case EOpcode::NewOrderedMap:
				case EOpcode::NewDeque:
				case EOpcode::NewChannel:
				case EOpcode::CheckedCast:
				case EOpcode::BoxVariant:
				case EOpcode::VariantIs:
				case EOpcode::CloneValueOfType:
					{
						if (OperandU16() >= Module.StringPool.size())
						{
							return FailIndex("string", Ip);
						}
						if (Opcode == EOpcode::CloneValueOfType)
						{
							const auto& Name = Module.StringPool[OperandU16()];
							if (Bytecode::ReadU16LE(Code.data() + Ip + 3) > 1 || Name.empty() || Name == "void" || Runtime::IsReferenceTypeName(Name) || Name.find('\0') != std::string::npos)
							{
								OutError = Context + ": invalid value copy view";
								return false;
							}
						}
						if (Opcode == EOpcode::BoxVariant || Opcode == EOpcode::VariantIs)
						{
							const auto& Name = Module.StringPool[OperandU16()];
							if (Name.empty() || Name == "void" || Name == "nullptr" || Name == "variant" || Runtime::IsReferenceTypeName(Name) || Name.find('\0') != std::string::npos || CanonicalizeNativeElementTypeName(Name) != Name)
							{
								OutError = Context + ": invalid variant type operand at byte " + std::to_string(Ip);
								return false;
							}
						}
						break;
					}
				case EOpcode::TryCast:
					{
						if (OperandU16() >= Module.StringPool.size())
						{
							return FailIndex("string", Ip);
						}
						{
							const std::uint16_t OptionalTypeIndex =
							    Bytecode::ReadU16LE(Code.data() + Ip + 3);
							if (OptionalTypeIndex >= Module.Types.size())
							{
								return FailIndex("type", Ip);
							}
							const FBytecodeTypeDesc& OptionalType = Module.Types[OptionalTypeIndex];
							const std::string& TargetType = Module.StringPool[OperandU16()];
							if (TargetType == "variant")
							{
								OutError = Context + ": try_cast<variant> is not a valid bytecode operation";
								return false;
							}
							const bool bBuiltinOptional = OptionalType.Kind == EScriptObjectKind::Struct && OptionalType.Name.rfind("optional#", 0) == 0 && OptionalType.Fields.size() == 3 && OptionalType.Fields[0].Name == "present" && OptionalType.Fields[0].TypeName == "bool" && OptionalType.Fields[1].Name == "value" && OptionalType.Fields[1].TypeName == TargetType && OptionalType.Fields[2].Name == "empty_value" && OptionalType.Fields[2].TypeName == TargetType;
							if (!bBuiltinOptional)
							{
								OutError = Context + ": try_cast requires matching builtin optional<T> metadata at byte " + std::to_string(Ip);
								return false;
							}
						}
						break;
					}
				case EOpcode::NewObject:
				case EOpcode::NewUninitializedObject:
					{
						if (OperandU16() >= Module.Types.size())
						{
							return FailIndex("type", Ip);
						}
						if (Module.Types[OperandU16()].bIsStatic)
						{
							OutError = "cannot construct static type from bytecode: " + Module.Types[OperandU16()].Name;
							return false;
						}
						break;
					}
				case EOpcode::Call:
				case EOpcode::CallBoundMethod:
					{
						const std::uint16_t FunctionIndex = OperandU16();
						if (FunctionIndex >= Module.Functions.size())
						{
							return FailIndex("function", Ip);
						}
						if (Code[Ip + 3] != Module.Functions[FunctionIndex].ParamCount)
						{
							OutError = Context + ": call argument count mismatch at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::MakeClosure:
					{
						const std::uint16_t FunctionIndex = OperandU16();
						if (FunctionIndex >= Module.Functions.size())
						{
							return FailIndex("closure function", Ip);
						}
						const FBytecodeFunction& Closure = Module.Functions[FunctionIndex];
						if (Code[Ip + 3] != Closure.Captures.size())
						{
							OutError = Context + ": closure capture count mismatch at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						const auto Builtin = static_cast<EBuiltinId>(Code[Ip + 1]);
						if (Builtin >= EBuiltinId::Count)
						{
							OutError = Context + ": invalid builtin id at byte " + std::to_string(Ip);
							return false;
						}
						const Builtins::FDescriptor* const Descriptor =
						    Builtins::LookupById(Builtin);
						if (!Descriptor || !Descriptor->AcceptsArgCount(Code[Ip + 2]))
						{
							OutError = Context + ": invalid argument count for builtin '" + std::string(Descriptor ? Descriptor->Name : "<unknown>") + "' at byte " + std::to_string(Ip);
							return false;
						}
						break;
					}
				case EOpcode::Jmp:
				case EOpcode::JmpIfFalse:
					{
						if (!CheckJump(Ip, Size, 1))
						{
							return false;
						}
						break;
					}
				default:
					{
						break;
					}
				}
				if (Opcode == EOpcode::JmpIfLocalGeImm && !CheckJump(Ip, Size, 7))
				{
					return false;
				}
				Ip += Size;
			}
			if (OutInstructionBoundaries)
			{
				*OutInstructionBoundaries = std::move(InstructionBoundary);
			}
			return true;
		}
	}

	bool VeroScriptBytecodeIO::ValidateModule(
	    const FBytecodeModule& Module,
	    std::string& OutError)
	try
	{
		OutError.clear();
		constexpr std::size_t MaxU16Entries =
		    std::numeric_limits<std::uint16_t>::max();
		if (!ValidateRuntimeTypeScopes(Module, OutError) || !ValidateTemplateMetadata(Module, OutError)) return false;
		const auto CheckU16Table = [&](const std::size_t Count, const char* Name)
		{
			if (Count > MaxU16Entries)
			{
				OutError = std::string("too many ") + Name + " in bytecode module";
				return false;
			}
			return true;
		};
		if (!ValidateExternalDependencies(Module.RequiredExternalDependencies, OutError)) return false;
		if (!ValidatePublicMacros(Module.PublicMacros, OutError))
		{
			return false;
		}

		if (Module.Version != VEROSCRIPT_BC_VERSION)
		{
			OutError = "bytecode module version mismatch";
			return false;
		}
		if (!CheckU16Table(Module.NativeReferences.size(), "native references")) return false;
		for (const auto& Reference : Module.NativeReferences)
		{
			if (Reference.Name.empty() || Reference.Signature.empty() || Reference.ArgumentCount > 255)
			{
				OutError = "invalid native reference descriptor";
				return false;
			}
		}
		if (!CheckU16Table(Module.StringPool.size(), "strings") || !CheckU16Table(Module.WStringPool.size(), "wide strings") || !CheckU16Table(Module.Constants.size(), "constants") || !CheckU16Table(Module.Types.size(), "types") || !CheckU16Table(Module.Interfaces.size(), "interfaces") || !CheckU16Table(Module.Enums.size(), "enums") || !CheckU16Table(Module.Functions.size(), "functions") || !CheckU16Table(Module.GlobalNames.size(), "globals") || !CheckU16Table(Module.DebugFiles.size(), "debug files"))
		{
			return false;
		}
		if (Module.MainFunctionIndex >= 0 && (Module.MainFunctionIndex > std::numeric_limits<std::uint16_t>::max() || static_cast<std::size_t>(Module.MainFunctionIndex) >= Module.Functions.size()))
		{
			OutError = "main function index out of range";
			return false;
		}

		for (std::size_t Index = 0; Index < Module.Constants.size(); ++Index)
		{
			if (!Detail::ValidateConstantReference(
			        Module,
			        Module.Constants[Index],
			        "constant " + std::to_string(Index),
			        OutError))
			{
				return false;
			}
		}

		std::unordered_set<std::string> TypeNames;
		for (std::size_t TypeIndex = 0; TypeIndex < Module.Types.size(); ++TypeIndex)
		{
			const FBytecodeTypeDesc& TypeDesc = Module.Types[TypeIndex];
			if (TypeDesc.BaseTypeIndices.size() > Detail::kMaxBytecodeSectionEntries || TypeDesc.UsingMembers.size() > Detail::kMaxBytecodeSectionEntries || TypeDesc.ImplementedInterfaceIndices.size() > Detail::kMaxBytecodeSectionEntries || TypeDesc.StaticFields.size() > Detail::kMaxBytecodeSectionEntries)
			{
				OutError = "type metadata section count exceeds safety limit: " + TypeDesc.Name;
				return false;
			}
			if (!TypeNames.insert(TypeDesc.Name).second)
			{
				OutError = "duplicate type linkage name: " + TypeDesc.Name;
				return false;
			}
			if (TypeDesc.Kind != EScriptObjectKind::Struct && TypeDesc.Kind != EScriptObjectKind::Class)
			{
				OutError = "invalid object kind for type " + TypeDesc.Name;
				return false;
			}
			if (TypeDesc.Fields.size() > MaxU16Entries || TypeDesc.OwnFieldStartIndex > TypeDesc.Fields.size())
			{
				OutError = "field layout out of range for type " + TypeDesc.Name;
				return false;
			}
			if (!TypeDesc.BaseInheritanceAccess.empty() && TypeDesc.BaseInheritanceAccess.size() != TypeDesc.BaseTypeIndices.size())
			{
				OutError = "base access metadata size mismatch";
				return false;
			}
			for (const EMemberAccess Access : TypeDesc.BaseInheritanceAccess)
			{
				if (Access > EMemberAccess::Private)
				{
					OutError = "invalid base access metadata";
					return false;
				}
			}
			for (const FBytecodeTypeDesc::FUsingMember& Using : TypeDesc.UsingMembers)
			{
				if (Using.MemberName.empty() || Using.Access > EMemberAccess::Private)
				{
					OutError = "invalid using member metadata";
					return false;
				}
			}
			for (const std::uint16_t BaseIndex : TypeDesc.BaseTypeIndices)
			{
				if (BaseIndex >= Module.Types.size() || BaseIndex == TypeIndex)
				{
					OutError = "type base index out of range or self-referential";
					return false;
				}
			}
			for (const std::uint16_t InterfaceIndex :
			    TypeDesc.ImplementedInterfaceIndices)
			{
				if (InterfaceIndex >= Module.Interfaces.size())
				{
					OutError = "implemented interface index out of range";
					return false;
				}
			}
			if (TypeDesc.DestructorFunctionIndex != std::numeric_limits<std::uint16_t>::max() && TypeDesc.DestructorFunctionIndex >= Module.Functions.size())
			{
				OutError = "destructor function index out of range";
				return false;
			}
			if (TypeDesc.CopyConstructorIndex != 0xFFFF)
			{
				if (TypeDesc.CopyConstructorIndex >= Module.Functions.size())
				{
					OutError = "copy constructor index out of range";
					return false;
				}
				const auto& Copy = Module.Functions[TypeDesc.CopyConstructorIndex];
				if (Copy.SpecialMemberKind != ESpecialMemberKind::CopyConstructor || Copy.OwnerTypeIndex != TypeIndex)
				{
					OutError = "copy constructor owner mismatch";
					return false;
				}
			}
			if (TypeDesc.bDestructorPureVirtual && (!TypeDesc.bDestructorVirtual || !TypeDesc.bIsAbstract))
			{
				OutError = "pure virtual destructor requires virtual abstract type metadata: " + TypeDesc.Name + " (virtual=" + (TypeDesc.bDestructorVirtual ? "true" : "false") + ", abstract=" + (TypeDesc.bIsAbstract ? "true" : "false") + ')';
				return false;
			}
			if (TypeDesc.bIsStatic && ((TypeDesc.Kind != EScriptObjectKind::Class && TypeDesc.Kind != EScriptObjectKind::Struct) || !TypeDesc.BaseTypeIndices.empty() || !TypeDesc.ImplementedInterfaceIndices.empty() || !TypeDesc.Fields.empty() || TypeDesc.DestructorFunctionIndex != std::numeric_limits<std::uint16_t>::max() || TypeDesc.bDestructorVirtual || TypeDesc.bDestructorPureVirtual || TypeDesc.bIsAbstract || !TypeDesc.HostBaseTypeName.empty() || TypeDesc.HostBaseTypeId != 0))
			{
				OutError = "static type has instance-only type metadata: " + TypeDesc.Name;
				return false;
			}
			if (TypeDesc.HostBaseTypeName.empty() && TypeDesc.HostBaseTypeId != 0)
			{
				OutError = "host base id requires a host base name for type " + TypeDesc.Name;
				return false;
			}
			std::unordered_set<std::string> FieldNames;
			for (std::size_t FieldIndex = 0;
			    FieldIndex < TypeDesc.Fields.size();
			    ++FieldIndex)
			{
				const FBytecodeFieldDesc& Field = TypeDesc.Fields[FieldIndex];
				const std::string Context = "field " + TypeDesc.Name + "." + Field.Name;
				if (Field.Access > EMemberAccess::Private)
				{
					OutError = Context + ": invalid access metadata";
					return false;
				}
				if (Field.bIsConst && Field.bIsMutable)
				{
					OutError = Context + ": const field cannot be mutable";
					return false;
				}
				if (!FieldNames.insert(Field.Name).second)
				{
					OutError = "duplicate field: " + TypeDesc.Name + "." + Field.Name;
					return false;
				}
				if (Field.bHasDefault && !Detail::ValidateConstantReference(Module, Field.DefaultValue, Context, OutError))
				{
					return false;
				}
				if (Field.bHasInitCode && !Detail::ValidateCode(Module, Field.InitCode, Field.InitLocalCount, Context, OutError))
				{
					return false;
				}
			}
			for (const FBytecodeFieldDesc& Field : TypeDesc.StaticFields)
			{
				const std::string Context =
				    "static field " + TypeDesc.Name + "." + Field.Name;
				if (Field.bIsMutable || (Field.bConstexpr && !Field.bIsConst))
				{
					OutError = Context + ": static field cannot be mutable or have invalid constexpr qualifiers";
					return false;
				}
				if (Field.bHasDefault || Field.bHasInitCode || !Field.InitCode.empty() || Field.InitLocalCount != 0)
				{
					OutError = Context + ": static field initializers must use module globals";
					return false;
				}
				if (!FieldNames.insert(Field.Name).second)
				{
					OutError = "duplicate field: " + TypeDesc.Name + "." + Field.Name;
					return false;
				}
				if (Field.bHasDefault && !Detail::ValidateConstantReference(Module, Field.DefaultValue, Context, OutError))
				{
					return false;
				}
				if (Field.bHasInitCode && !Detail::ValidateCode(Module, Field.InitCode, Field.InitLocalCount, Context, OutError))
				{
					return false;
				}
			}
		}
		{
			std::vector<std::vector<std::size_t>> DerivedTypes(Module.Types.size());
			std::vector<std::size_t> RemainingBases(Module.Types.size(), 0);
			std::vector<std::size_t> Ready;
			for (std::size_t TypeIndex = 0; TypeIndex < Module.Types.size(); ++TypeIndex)
			{
				RemainingBases[TypeIndex] = Module.Types[TypeIndex].BaseTypeIndices.size();
				if (RemainingBases[TypeIndex] == 0)
				{
					Ready.push_back(TypeIndex);
				}
				for (const std::uint16_t BaseIndex : Module.Types[TypeIndex].BaseTypeIndices)
				{
					DerivedTypes[BaseIndex].push_back(TypeIndex);
				}
			}
			std::size_t VisitedCount = 0;
			while (!Ready.empty())
			{
				const std::size_t TypeIndex = Ready.back();
				Ready.pop_back();
				++VisitedCount;
				for (const std::size_t DerivedIndex : DerivedTypes[TypeIndex])
				{
					if (--RemainingBases[DerivedIndex] == 0)
					{
						Ready.push_back(DerivedIndex);
					}
				}
			}
			if (VisitedCount != Module.Types.size())
			{
				OutError = "cyclic type inheritance";
				return false;
			}
		}

		std::unordered_set<std::string> InterfaceNames;
		for (std::size_t InterfaceIndex = 0;
		    InterfaceIndex < Module.Interfaces.size();
		    ++InterfaceIndex)
		{
			const FBytecodeInterfaceDesc& Interface = Module.Interfaces[InterfaceIndex];
			if (Interface.BaseInterfaceIndices.size() > Detail::kMaxBytecodeSectionEntries || Interface.Fields.size() > Detail::kMaxBytecodeSectionEntries || Interface.StaticFields.size() > Detail::kMaxBytecodeSectionEntries)
			{
				OutError = "interface metadata section count exceeds safety limit: " + Interface.Name;
				return false;
			}
			if (!InterfaceNames.insert(Interface.Name).second)
			{
				OutError = "duplicate interface linkage name: " + Interface.Name;
				return false;
			}
			for (const std::uint16_t BaseIndex : Interface.BaseInterfaceIndices)
			{
				if (BaseIndex >= Module.Interfaces.size() || BaseIndex == InterfaceIndex)
				{
					OutError = "interface base index out of range or self-referential";
					return false;
				}
			}
			std::unordered_set<std::string> FieldNames;
			for (const FBytecodeFieldDesc& Field : Interface.Fields)
			{
				const std::string Context =
				    "interface field " + Interface.Name + "." + Field.Name;
				if (Field.bIsConst && Field.bIsMutable)
				{
					OutError = Context + ": const field cannot be mutable";
					return false;
				}
				if (Field.bIsMutable)
				{
					OutError = Context + ": interface field cannot be mutable";
					return false;
				}
				if (!FieldNames.insert(Field.Name).second)
				{
					OutError = "duplicate interface field: " + Interface.Name + "." + Field.Name;
					return false;
				}
				if (Field.bHasDefault && !Detail::ValidateConstantReference(Module, Field.DefaultValue, Context, OutError))
				{
					return false;
				}
				if (Field.bHasInitCode && !Detail::ValidateCode(Module, Field.InitCode, Field.InitLocalCount, Context, OutError))
				{
					return false;
				}
			}
			for (const FBytecodeFieldDesc& Field : Interface.StaticFields)
			{
				const std::string Context =
				    "static interface field " + Interface.Name + "." + Field.Name;
				if (Field.bIsMutable || (Field.bConstexpr && !Field.bIsConst))
				{
					OutError = Context + ": static field cannot be mutable or have invalid constexpr qualifiers";
					return false;
				}
				if (Field.bHasDefault || Field.bHasInitCode || !Field.InitCode.empty() || Field.InitLocalCount != 0)
				{
					OutError = Context + ": static field initializers must use module globals";
					return false;
				}
				if (!FieldNames.insert(Field.Name).second)
				{
					OutError = "duplicate interface field: " + Interface.Name + "." + Field.Name;
					return false;
				}
				if (Field.bHasDefault && !Detail::ValidateConstantReference(Module, Field.DefaultValue, Context, OutError))
				{
					return false;
				}
				if (Field.bHasInitCode && !Detail::ValidateCode(Module, Field.InitCode, Field.InitLocalCount, Context, OutError))
				{
					return false;
				}
			}
		}
		{
			std::vector<std::vector<std::size_t>> DerivedInterfaces(Module.Interfaces.size());
			std::vector<std::size_t> RemainingBases(Module.Interfaces.size(), 0);
			std::vector<std::size_t> Ready;
			for (std::size_t InterfaceIndex = 0;
			    InterfaceIndex < Module.Interfaces.size();
			    ++InterfaceIndex)
			{
				RemainingBases[InterfaceIndex] =
				    Module.Interfaces[InterfaceIndex].BaseInterfaceIndices.size();
				if (RemainingBases[InterfaceIndex] == 0)
				{
					Ready.push_back(InterfaceIndex);
				}
				for (const std::uint16_t BaseIndex :
				    Module.Interfaces[InterfaceIndex].BaseInterfaceIndices)
				{
					DerivedInterfaces[BaseIndex].push_back(InterfaceIndex);
				}
			}
			std::size_t VisitedCount = 0;
			while (!Ready.empty())
			{
				const std::size_t InterfaceIndex = Ready.back();
				Ready.pop_back();
				++VisitedCount;
				for (const std::size_t DerivedIndex : DerivedInterfaces[InterfaceIndex])
				{
					if (--RemainingBases[DerivedIndex] == 0)
					{
						Ready.push_back(DerivedIndex);
					}
				}
			}
			if (VisitedCount != Module.Interfaces.size())
			{
				OutError = "cyclic interface inheritance";
				return false;
			}
		}

		std::unordered_set<std::string> EnumNames;
		for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
		{
			if (EnumDesc.Members.size() > Detail::kMaxBytecodeSectionEntries)
			{
				OutError = "enum member count exceeds safety limit: " + EnumDesc.Name;
				return false;
			}
			if (!EnumNames.insert(EnumDesc.Name).second)
			{
				OutError = "duplicate enum linkage name: " + EnumDesc.Name;
				return false;
			}
			std::unordered_set<std::string> MemberNames;
			for (const FBytecodeEnumMemberDesc& Member : EnumDesc.Members)
			{
				if (!MemberNames.insert(Member.Name).second)
				{
					OutError = "duplicate enum member: " + EnumDesc.Name + "." + Member.Name;
					return false;
				}
			}
		}

		std::unordered_set<std::string> FunctionNames;
		std::unordered_set<std::uint16_t> LocalStaticSlots;
		const auto IsConstantScalar = [&](const std::string_view Name)
		{
			FTemplateValueType Type;
			return TryGetTemplateValueType(Name, Type) || std::any_of(Module.Enums.begin(), Module.Enums.end(), [&](const auto& Enum)
			{
				return Enum.Name == Name;
			});
		};
		for (const FBytecodeFunction& Function : Module.Functions)
		{
			if (Function.Access > EMemberAccess::Private || (Function.bIsMethod && Function.bIsStaticMethod))
			{
				OutError = "invalid function access metadata: " + Function.Name;
				return false;
			}
			if (!FunctionNames.insert(Function.Name).second)
			{
				OutError = "duplicate function linkage name: " + Function.Name;
				return false;
			}
			if (Function.LocalCount < Function.ParamCount || Function.Captures.size() > std::numeric_limits<std::uint8_t>::max() || Function.LocalCount < static_cast<std::size_t>(Function.ParamCount) + Function.Captures.size() || Function.LocalNames.size() > Function.LocalCount)
			{
				OutError = "invalid frame metadata for function " + Function.Name;
				return false;
			}
			if (!Function.ParamFlags.empty() && Function.ParamFlags.size() != Function.ParamCount)
			{
				OutError = "parameter flag count mismatch for function " + Function.Name;
				return false;
			}
			for (const std::uint8_t Flags : Function.ParamFlags)
			{
				if ((Flags & ~(BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0 || ((Flags & BytecodeParamFlagRef) == 0 && (Flags & BytecodeParamFlagConstRef) != 0))
				{
					OutError = "invalid parameter flags for function " + Function.Name;
					return false;
				}
			}
			if (Function.bReturnsVoid && Function.bReturnsRef)
			{
				OutError = "void function cannot return a reference: " + Function.Name;
				return false;
			}
			if (!Function.ClosureTypeName.empty())
			{
				std::vector<std::string> SignatureParams;
				std::string SignatureReturn;
				if (!Runtime::ParseFunctionType(
				        Function.ClosureTypeName, SignatureParams, SignatureReturn) ||
				    SignatureParams.size() != Function.ParamCount || Runtime::IsReferenceTypeName(SignatureReturn) != Function.bReturnsRef || (SignatureReturn == "void") != Function.bReturnsVoid)
				{
					OutError = "function signature metadata mismatch: " + Function.Name + " (signature=" + Function.ClosureTypeName + ", decoded_params=" + std::to_string(SignatureParams.size()) + ", bytecode_params=" + std::to_string(Function.ParamCount) + ')';
					return false;
				}
			}
			std::unordered_set<std::string> CaptureNames;
			for (std::size_t CaptureIndex = 0;
			    CaptureIndex < Function.Captures.size();
			    ++CaptureIndex)
			{
				const std::string& CaptureName = Function.Captures[CaptureIndex].Name;
				const std::string& CaptureType = Function.Captures[CaptureIndex].BindingTypeName();
				if (CaptureName.empty() || !CaptureNames.insert(CaptureName).second || CaptureType.empty())
				{
					OutError = "invalid closure capture metadata for function " + Function.Name;
					return false;
				}
				const auto& Capture = Function.Captures[CaptureIndex];
				if (Capture.Mode > EClosureCaptureMode::Reference || (Capture.Mode == EClosureCaptureMode::MutableValue && !Capture.bWritable) || (Capture.bStableStorage && (Capture.Mode != EClosureCaptureMode::Value || Capture.bWritable)))
				{
					OutError = "invalid closure capture mode: " + Function.Name;
					return false;
				}
				std::string_view ValueType = Capture.TypeName;
				if (ValueType.empty() || ValueType.rfind("ref ", 0) == 0 || ValueType.rfind("const ref ", 0) == 0)
				{
					OutError = "invalid closure capture type for function " + Function.Name;
					return false;
				}
			}
			if (Function.bConstexpr)
			{
				std::vector<std::string> ParameterTypes;
				std::string ReturnType;
				if (Function.bReturnsVoid || Function.bReturnsRef || Function.bIsMethod || Function.bIsVirtual || Function.bIsAbstract || Function.bDeleted || Function.bDefaulted || BytecodeFunctionHasRefParams(Function) ||
				    !Runtime::ParseFunctionType(Function.ClosureTypeName, ParameterTypes, ReturnType) || !IsConstantScalar(ReturnType) ||
				    std::any_of(ParameterTypes.begin(), ParameterTypes.end(), [&](const auto& Type)
				{
					return !IsConstantScalar(Type);
				}))
				{
					OutError = "invalid constexpr function metadata: " + Function.Name;
					return false;
				}
			}
			if (Function.Code.empty() && !Function.bIsAbstract && !Function.bDeleted)
			{
				OutError = "non-abstract function has empty bytecode: " + Function.Name;
				return false;
			}
			if (Function.bIsMethod && Function.OwnerTypeIndex != std::numeric_limits<std::uint16_t>::max() && Function.OwnerTypeIndex >= Module.Types.size())
			{
				OutError = "method owner type index out of range for function " + Function.Name;
				return false;
			}
			if (!Function.bIsMethod && Function.bIsVirtual)
			{
				OutError = "non-method function cannot be virtual: " + Function.Name;
				return false;
			}
			if (Function.bIsAbstract && (!Function.bIsMethod || !Function.bIsVirtual))
			{
				OutError =
				    "abstract function must be a virtual instance method: " + Function.Name;
				return false;
			}
			if (Function.bDeleted && (Function.bIsAbstract || !Function.Code.empty() || Function.bIsVirtual))
			{
				OutError = "deleted function must be non-virtual declaration-only metadata: " + Function.Name;
				return false;
			}
			if (static_cast<std::uint8_t>(Function.SpecialMemberKind) > static_cast<std::uint8_t>(ESpecialMemberKind::Inequality) || (Function.bDefaulted && Function.bDeleted) || (!Function.UnavailableReason.empty() && !Function.bDefaulted) || (Function.bExplicitCopy && Function.SpecialMemberKind != ESpecialMemberKind::CopyConstructor))
			{
				OutError = "contradictory special member metadata: " + Function.Name;
				return false;
			}
			if (Function.bDefaulted && Function.SpecialMemberKind == ESpecialMemberKind::None)
			{
				if (!Function.bIsMethod || Function.OwnerTypeIndex >= Module.Types.size() || Function.ParamCount != 0 || !Function.bReturnsVoid || Function.bIsConstMethod || Function.bIsStaticMethod)
				{
					OutError = "invalid defaulted constructor/destructor metadata";
					return false;
				}
				const auto Leaf = MethodLinkageBareLeaf(Function.Name);
				const auto Owner = ModuleMeta::TypeSimpleName(Module.Types[Function.OwnerTypeIndex].Name);
				if (Leaf != Owner && Leaf != "~" + Owner)
				{
					OutError = "defaulted ordinary method metadata";
					return false;
				}
			}
			if (Function.SpecialMemberKind != ESpecialMemberKind::None)
			{
				std::vector<std::string> Parameters;
				std::string Return;
				if (!Function.bIsMethod || Function.bIsStaticMethod || Function.OwnerTypeIndex >= Module.Types.size() || !Runtime::ParseFunctionType(Function.ClosureTypeName, Parameters, Return) || Parameters != std::vector<std::string>{"const ref " + Module.Types[Function.OwnerTypeIndex].Name} || Function.ParamCount != 1 || Function.ParamFlags != std::vector<std::uint8_t>{3})
				{
					OutError = "invalid special member owner/signature: " + Function.Name;
					return false;
				}
				const auto& Owner = Module.Types[Function.OwnerTypeIndex];
				const auto Kind = Function.SpecialMemberKind;
				const auto Leaf = MethodLinkageBareLeaf(Function.Name);
				if ((Kind == ESpecialMemberKind::CopyConstructor && Leaf != ModuleMeta::TypeSimpleName(Owner.Name)) || (Kind == ESpecialMemberKind::CopyAssignment && Leaf != "operator=" && Leaf != "@copy_assign") || (Kind == ESpecialMemberKind::Equality && Leaf != "operator==") || (Kind == ESpecialMemberKind::Inequality && Leaf != "operator!="))
				{
					OutError = "invalid special member name: " + Function.Name;
					return false;
				}
				if ((Kind == ESpecialMemberKind::CopyConstructor && (Return != "void" || Function.bIsConstMethod || Owner.Kind != EScriptObjectKind::Struct)) || (Kind == ESpecialMemberKind::CopyAssignment && (Return != "ref " + Owner.Name || Function.bIsConstMethod || Owner.Kind != EScriptObjectKind::Struct)) || ((Kind == ESpecialMemberKind::Equality || Kind == ESpecialMemberKind::Inequality) && (Return != "bool" || !Function.bIsConstMethod)))
				{
					OutError = "invalid special member result type: " + Function.Name;
					return false;
				}
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				if (Op == EOpcode::DefaultCopyConstruct && (!Function.bDefaulted || Function.SpecialMemberKind != ESpecialMemberKind::CopyConstructor))
				{
					OutError = "default copy instruction outside its constructor";
					return false;
				}
				Ip += Bytecode::InstructionSize(Op);
			}
			if (Function.DebugLines.size() > Function.Code.size())
			{
				OutError = "debug line table exceeds bytecode size for function " + Function.Name;
				return false;
			}
			for (const FDebugLine& Line : Function.DebugLines)
			{
				if (Line.Line != 0 && Line.FileIndex >= Module.DebugFiles.size())
				{
					OutError = "debug file index out of range for function " + Function.Name;
					return false;
				}
			}
			std::vector<std::uint8_t> InstructionBoundaries;
			if (!Detail::ValidateCode(
			        Module,
			        Function.Code,
			        Function.LocalCount,
			        "function " + Function.Name,
			        OutError,
			        &InstructionBoundaries))
			{
				return false;
			}
			if (!Detail::ValidatePauseMarkers(Function.Code, Function.PauseMarkerOffsets, InstructionBoundaries, OutError)) return false;
			std::vector<const FBytecodeLocalStaticInitializer*> StaticRanges;
			for (const auto& Initializer : Function.LocalStaticInitializers)
			{
				if (Initializer.GlobalSlot >= Module.GlobalMetadata.size() || !Module.GlobalMetadata[Initializer.GlobalSlot].bLocalStatic || !LocalStaticSlots.insert(Initializer.GlobalSlot).second || Initializer.Name.empty() || Initializer.BeginOffset >= Initializer.CommitOffset || Initializer.CommitOffset > Function.Code.size() || Function.Code.size() - Initializer.CommitOffset < 3u || Initializer.EndOffset != Initializer.CommitOffset + 3u || Initializer.ScopeEndOffset < Initializer.EndOffset || Initializer.ScopeEndOffset > Function.Code.size() || !InstructionBoundaries[Initializer.ScopeEndOffset] || Initializer.CommitOffset - Initializer.BeginOffset < 7u || !InstructionBoundaries[Initializer.BeginOffset] || !InstructionBoundaries[Initializer.CommitOffset] || static_cast<EOpcode>(Function.Code[Initializer.BeginOffset]) != EOpcode::BeginLocalStatic || static_cast<EOpcode>(Function.Code[Initializer.CommitOffset]) != EOpcode::CommitLocalStatic || Bytecode::ReadU16LE(Function.Code.data() + Initializer.BeginOffset + 1u) != Initializer.GlobalSlot || Bytecode::ReadU16LE(Function.Code.data() + Initializer.CommitOffset + 1u) != Initializer.GlobalSlot || static_cast<std::int64_t>(Initializer.BeginOffset) + 7 + Bytecode::ReadI32LE(Function.Code.data() + Initializer.BeginOffset + 3u) != Initializer.EndOffset)
				{
					OutError = "invalid local static initializer metadata: " + Function.Name;
					return false;
				}
				StaticRanges.push_back(&Initializer);
			}
			std::sort(StaticRanges.begin(), StaticRanges.end(), [](const auto* A, const auto* B)
			{
				return A->BeginOffset < B->BeginOffset;
			});
			for (std::size_t Index = 1; Index < StaticRanges.size(); ++Index)
			{
				if (StaticRanges[Index]->BeginOffset < StaticRanges[Index - 1u]->EndOffset)
				{
					OutError = "overlapping local static initializer ranges: " + Function.Name;
					return false;
				}
			}
			const auto StaticAt = [&](const std::size_t Offset) -> const FBytecodeLocalStaticInitializer*
			{
				const auto It = std::upper_bound(StaticRanges.begin(), StaticRanges.end(), Offset,
				    [](const std::size_t Position, const auto* Range)
				{
					return Position < Range->BeginOffset;
				});
				if (It == StaticRanges.begin()) return nullptr;
				const auto* Range = *(It - 1);
				return Offset < Range->EndOffset ? Range : nullptr;
			};
			bool bUsesStatics = !StaticRanges.empty();
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const auto Op = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(Op);
				const auto* Range = StaticAt(Ip);
				if (Op == EOpcode::StoreLocal || Op == EOpcode::MakeLocalRef)
				{
					const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
					if (Slot >= Function.ParamCount && Slot - Function.ParamCount < Function.Captures.size())
					{
						const auto& Capture = Function.Captures[Slot - Function.ParamCount];
						if (Capture.Mode == EClosureCaptureMode::Value && Capture.bWritable)
						{
							OutError = "value receiver capture cannot be rebound or borrowed: " + Function.Name;
							return false;
						}
					}
				}
				if ((Op == EOpcode::BeginLocalStatic && (!Range || Range->BeginOffset != Ip)) || (Op == EOpcode::CommitLocalStatic && (!Range || Range->CommitOffset != Ip)))
				{
					OutError = "local static instruction has no matching initializer: " + Function.Name;
					return false;
				}
				if (Op == EOpcode::LoadGlobal || Op == EOpcode::StoreGlobal || Op == EOpcode::MakeGlobalRef)
				{
					const auto Slot = Bytecode::ReadU16LE(Function.Code.data() + Ip + 1u);
					bUsesStatics = bUsesStatics || (Slot < Module.GlobalMetadata.size() && Module.GlobalMetadata[Slot].bLocalStatic);
				}
				if (Op == EOpcode::Jmp || Op == EOpcode::JmpIfFalse || Op == EOpcode::JmpIfLocalGeImm)
				{
					const auto Target = static_cast<std::size_t>(static_cast<std::int64_t>(Ip + Size) + Bytecode::ReadI32LE(Function.Code.data() + Ip + Size - 4u));
					const auto* Destination = StaticAt(Target);
					if ((Destination && Target != Destination->BeginOffset && Destination != Range) || (Range && Destination != Range))
					{
						OutError = "jump crosses a local static initializer boundary: " + Function.Name;
						return false;
					}
				}
				if (Range && Op == EOpcode::Return)
				{
					OutError = "return leaves an uncommitted local static initializer: " + Function.Name;
					return false;
				}
				Ip += Size;
			}
			if ((bUsesStatics && !Function.bUsesLocalStaticStorage) || (StaticRanges.empty() != (Function.LocalStaticSemanticIdentity == 0)))
			{
				OutError = "local static function flags are inconsistent: " + Function.Name;
				return false;
			}
			for (const auto& Region : Function.ExceptionRegions)
			{
				const auto EntersInitializer = [&](const std::uint32_t Offset)
				{
					const auto* Range = StaticAt(Offset);
					return Range && Offset != Range->BeginOffset;
				};
				if (EntersInitializer(Region.FinallyOffset) || std::any_of(Region.Catches.begin(), Region.Catches.end(), [&](const auto& Handler)
				{
					return EntersInitializer(Handler.HandlerOffset);
				}))
				{
					OutError = "exception handler enters a local static initializer: " + Function.Name;
					return false;
				}
			}
			if (Function.ExceptionRegions.size() > Detail::kMaxBytecodeSectionEntries)
			{
				OutError = "exception region count exceeds safety limit: " + Function.Name;
				return false;
			}
			std::unordered_set<std::uint32_t> FinallyEnds;
			for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
			{
				if (Region.BeginOffset >= Region.EndOffset || Region.EndOffset > Function.Code.size() || !InstructionBoundaries[Region.BeginOffset] || !InstructionBoundaries[Region.EndOffset])
				{
					OutError = "invalid exception protected range in function " + Function.Name;
					return false;
				}
				if (Region.Catches.size() > Detail::kMaxBytecodeSectionEntries)
				{
					OutError = "catch handler count exceeds safety limit: " + Function.Name;
					return false;
				}
				const bool bHasFinally =
				    Region.FinallyOffset != std::numeric_limits<std::uint32_t>::max();
				if (bHasFinally != (Region.FinallyEndOffset != std::numeric_limits<std::uint32_t>::max()))
				{
					OutError = "incomplete finally metadata in function " + Function.Name;
					return false;
				}
				if (bHasFinally)
				{
					if (Region.FinallyOffset < Region.EndOffset || Region.FinallyOffset >= Region.FinallyEndOffset || Region.FinallyEndOffset > Function.Code.size() || !InstructionBoundaries[Region.FinallyOffset] || !InstructionBoundaries[Region.FinallyEndOffset])
					{
						OutError = "invalid finally range in function " + Function.Name;
						return false;
					}
					// Both endpoints are validated instruction boundaries. Walk back
					// only over the final instruction's operands; shared or nested
					// cleanup ranges must not rescan their entire bodies per region.
					std::size_t LastIp = Region.FinallyEndOffset - 1;
					while (!InstructionBoundaries[LastIp])
					{
						--LastIp;
					}
					if (static_cast<EOpcode>(Function.Code[LastIp]) != EOpcode::EndFinally)
					{
						OutError = "finally range must end with EndFinally in function " + Function.Name;
						return false;
					}
					FinallyEnds.insert(static_cast<std::uint32_t>(LastIp));
				}
				if (Region.Catches.empty() && !bHasFinally)
				{
					OutError = "exception region has no handler in function " + Function.Name;
					return false;
				}
				std::unordered_set<std::string> CatchTypes;
				bool bSawCatchAll = false;
				std::uint32_t PreviousHandlerOffset = 0;
				bool bHasPreviousHandler = false;
				for (const FBytecodeCatchHandler& Handler : Region.Catches)
				{
					if (Handler.LocalSlot >= Function.LocalCount || Handler.RethrowSlot >= Function.LocalCount || Handler.HandlerOffset >= Function.Code.size() || Handler.HandlerOffset < Region.EndOffset || (bHasFinally && Handler.HandlerOffset >= Region.FinallyOffset) || !InstructionBoundaries[Handler.HandlerOffset] || (bHasPreviousHandler && Handler.HandlerOffset <= PreviousHandlerOffset))
					{
						OutError = "invalid catch handler target in function " + Function.Name;
						return false;
					}
					PreviousHandlerOffset = Handler.HandlerOffset;
					bHasPreviousHandler = true;
					if (bSawCatchAll)
					{
						OutError = "catch-all handler must be last in function " + Function.Name;
						return false;
					}
					if (Handler.TypeName.empty())
					{
						bSawCatchAll = true;
					}
					else if (!CatchTypes.insert(Handler.TypeName).second)
					{
						OutError = "duplicate catch type in function " + Function.Name;
						return false;
					}
				}
			}
			if (Function.ExceptionRegions.size() > 1)
			{
				// Check interval structure in O(n log n), without changing source
				// order. A pairwise scan lets otherwise small hostile bytecode make
				// package/cache loading spend quadratic time in validation.
				std::vector<const FBytecodeExceptionRegion*> Ordered;
				Ordered.reserve(Function.ExceptionRegions.size());
				for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
				{
					Ordered.push_back(&Region);
				}
				std::sort(Ordered.begin(), Ordered.end(), [](const auto* Left, const auto* Right)
				{
					return Left->BeginOffset != Right->BeginOffset
					    ? Left->BeginOffset < Right->BeginOffset
					    : Left->EndOffset > Right->EndOffset;
				});
				std::vector<const FBytecodeExceptionRegion*> Active;
				for (const FBytecodeExceptionRegion* Region : Ordered)
				{
					while (!Active.empty() && Region->BeginOffset >= Active.back()->EndOffset)
					{
						Active.pop_back();
					}
					if (!Active.empty() && (Region->EndOffset > Active.back()->EndOffset || (Region->BeginOffset == Active.back()->BeginOffset && Region->EndOffset == Active.back()->EndOffset)))
					{
						OutError = "exception protected ranges must be strictly nested or disjoint in function " + Function.Name;
						return false;
					}
					Active.push_back(Region);
				}
			}
			for (std::size_t Ip = 0; Ip < Function.Code.size();)
			{
				const EOpcode Opcode = static_cast<EOpcode>(Function.Code[Ip]);
				if (Opcode == EOpcode::EndFinally && FinallyEnds.find(static_cast<std::uint32_t>(Ip)) == FinallyEnds.end())
				{
					OutError = "orphan EndFinally opcode in function " + Function.Name;
					return false;
				}
				Ip += Bytecode::InstructionSize(Opcode);
			}
		}
		if (Module.MainFunctionIndex >= 0)
		{
			const FBytecodeFunction& Main =
			    Module.Functions[static_cast<std::size_t>(Module.MainFunctionIndex)];
			const std::string_view LinkageName(Main.Name);
			const std::size_t UnitSeparator = LinkageName.find('\x1F');
			const std::size_t IdentityStart = UnitSeparator == std::string_view::npos
			    ? 0
			    : UnitSeparator + 1;
			// A private module path is part of the linkage prefix and may itself
			// contain "#fn". Only the free-function marker after the identity
			// separator belongs to main's signature.
			const std::size_t Signature = LinkageName.find("#fn#", IdentityStart);
			const std::size_t LeafEnd = Signature == std::string_view::npos
			    ? LinkageName.size()
			    : Signature;
			const std::string_view LeafName = LinkageName.substr(
			    IdentityStart,
			    LeafEnd - IdentityStart);
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (LeafName != "main" || Main.bIsMethod || Main.bIsAbstract || Main.bDeleted || !Runtime::ParseFunctionType(Main.ClosureTypeName, ParamTypes, ReturnType) || ParamTypes.size() != Main.ParamCount || Main.ParamCount > 1 || (Main.ParamCount == 1 && ParamTypes[0] != "string[]") || Main.ParamFlags.size() != Main.ParamCount || (ReturnType != "void" && ReturnType != "int") || Main.bReturnsVoid != (ReturnType == "void") || std::any_of(Main.ParamFlags.begin(), Main.ParamFlags.end(), [](const std::uint8_t Flags)
			{
				return Flags != 0;
			}))
			{
				OutError = "invalid main signature in bytecode module";
				return false;
			}
		}

		std::unordered_set<std::string> GlobalNames;
		for (const std::string& GlobalName : Module.GlobalNames)
		{
			if (!GlobalNames.insert(GlobalName).second)
			{
				OutError = "duplicate global linkage name: " + GlobalName;
				return false;
			}
		}
		if (!Module.GlobalTypeNames.empty() && Module.GlobalTypeNames.size() != Module.GlobalNames.size())
		{
			OutError = "global type metadata count does not match global count";
			return false;
		}
		if (!Module.GlobalMetadata.empty() && Module.GlobalMetadata.size() != Module.GlobalNames.size())
		{
			OutError = "global reflection metadata count does not match global count";
			return false;
		}
		for (std::size_t I = 0; I < Module.GlobalMetadata.size(); ++I)
		{
			const auto& Meta = Module.GlobalMetadata[I];
			if (Meta.bLocalStatic && (!LocalStaticSlots.count(static_cast<std::uint16_t>(I)) || Meta.bStaticMember || Meta.bExported))
			{
				OutError = "local static storage has no unique function owner or is exported";
				return false;
			}
			if (!Meta.bConstexpr && Meta.ConstantValue)
			{
				OutError = "constant payload requires constexpr metadata";
				return false;
			}
			if (!Meta.bConstexpr)
			{
				continue;
			}
			FTemplateValueType Expected;
			if (I >= Module.GlobalTypeNames.size() || !Meta.bConst || !Meta.ConstantValue)
			{
				OutError = "invalid constexpr global metadata";
				return false;
			}
			if (!TryGetTemplateValueType(Module.GlobalTypeNames[I], Expected))
			{
				const auto Enum = std::find_if(Module.Enums.begin(), Module.Enums.end(), [&](const auto& Type)
				{
					return Type.Name == Module.GlobalTypeNames[I];
				});
				if (Enum == Module.Enums.end() || !TryGetTemplateValueType(Enum->UnderlyingTypeName, Expected))
				{
					OutError = "invalid constexpr global type";
					return false;
				}
				Expected.Name = Enum->Name;
				Expected.bEnum = true;
				Expected.bScopedEnum = Enum->bScoped;
			}
			const auto& Actual = Meta.ConstantValue->Type;
			if (Actual.Name != Expected.Name || Actual.Width != Expected.Width || Actual.bUnsigned != Expected.bUnsigned || Actual.bBoolean != Expected.bBoolean || Actual.bEnum != Expected.bEnum || Actual.bScopedEnum != Expected.bScopedEnum || EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *Meta.ConstantValue}).empty())
			{
				OutError = "invalid constexpr global value type or bits";
				return false;
			}
		}
		std::vector<std::uint8_t> InitInstructionBoundaries;
		if (!Detail::ValidateCode(
		        Module,
		        Module.InitCode,
		        Module.InitLocalCount,
		        "module initializer",
		        OutError,
		        &InitInstructionBoundaries))
		{
			return false;
		}
		if (!Detail::ValidatePauseMarkers(Module.InitCode, Module.InitPauseMarkerOffsets, InitInstructionBoundaries, OutError)) return false;
		for (std::size_t Ip = 0; Ip < Module.InitCode.size();)
		{
			const auto Op = static_cast<EOpcode>(Module.InitCode[Ip]);
			if (Op == EOpcode::BeginLocalStatic || Op == EOpcode::CommitLocalStatic)
			{
				OutError = "local static initialization requires a function frame";
				return false;
			}
			Ip += Bytecode::InstructionSize(Op);
		}
		std::unordered_set<std::uint16_t> DeferredGlobals;
		for (const FBytecodeGlobalInitRange& Range : Module.GlobalInitRanges)
		{
			if (Range.GlobalSlot >= Module.GlobalNames.size() || Range.EndOffset <= Range.BeginOffset || Range.EndOffset > Module.InitCode.size() || !InitInstructionBoundaries[Range.BeginOffset] || !InitInstructionBoundaries[Range.EndOffset])
			{
				OutError =
				    "global initializer range is invalid or not instruction-aligned";
				return false;
			}
			if (Range.bDemandInitialized)
			{
				if (!DeferredGlobals.insert(Range.GlobalSlot).second)
				{
					OutError = "duplicate deferred global initializer";
					return false;
				}
				if (Range.GlobalSlot >= Module.GlobalMetadata.size() || Module.GlobalMetadata[Range.GlobalSlot].bLocalStatic)
				{
					OutError = "deferred global initializer requires a module global";
					return false;
				}
				for (std::size_t Ip = Range.BeginOffset; Ip < Range.EndOffset;)
				{
					const auto Opcode = static_cast<EOpcode>(Module.InitCode[Ip]);
					const auto Size = Bytecode::InstructionSize(Opcode);
					if (Opcode == EOpcode::Jmp || Opcode == EOpcode::JmpIfFalse || Opcode == EOpcode::JmpIfLocalGeImm)
					{
						const auto Target = static_cast<std::int64_t>(Ip + Size)
						    + Bytecode::ReadI32LE(Module.InitCode.data() + Ip + Size - 4);
						if (Target < static_cast<std::int64_t>(Range.BeginOffset) || Target > static_cast<std::int64_t>(Range.EndOffset))
						{
							OutError = "deferred global initializer branch leaves its code range";
							return false;
						}
					}
					Ip += Size;
				}
			}
		}
		{
			std::vector<const FBytecodeGlobalInitRange*> OrderedRanges;
			OrderedRanges.reserve(Module.GlobalInitRanges.size());
			for (const FBytecodeGlobalInitRange& Range : Module.GlobalInitRanges)
			{
				OrderedRanges.push_back(&Range);
			}
			std::stable_sort(
			    OrderedRanges.begin(),
			    OrderedRanges.end(),
			    [](const FBytecodeGlobalInitRange* const Left,
			        const FBytecodeGlobalInitRange* const Right)
			{
				if (Left->BeginOffset != Right->BeginOffset)
				{
					return Left->BeginOffset < Right->BeginOffset;
				}
				return Left->EndOffset > Right->EndOffset;
			});
			std::vector<const FBytecodeGlobalInitRange*> OpenRanges;
			for (const FBytecodeGlobalInitRange* const Range : OrderedRanges)
			{
				while (!OpenRanges.empty() && Range->BeginOffset >= OpenRanges.back()->EndOffset)
				{
					OpenRanges.pop_back();
				}
				if (!OpenRanges.empty())
				{
					const FBytecodeGlobalInitRange* const Parent =
					    OpenRanges.back();
					if (Range->EndOffset > Parent->EndOffset)
					{
						OutError =
						    "global initializer ranges overlap without nesting";
						return false;
					}
					if (Range->BeginOffset == Parent->BeginOffset && Range->EndOffset == Parent->EndOffset)
					{
						OutError = "duplicate global initializer bytecode range";
						return false;
					}
				}
				OpenRanges.push_back(Range);
			}
		}
		if (Module.InitDebugLines.size() > Module.InitCode.size())
		{
			OutError = "module initializer debug line table exceeds bytecode size";
			return false;
		}
		for (const FDebugLine& Line : Module.InitDebugLines)
		{
			if (Line.Line != 0 && Line.FileIndex >= Module.DebugFiles.size())
			{
				OutError = "module initializer debug file index out of range";
				return false;
			}
		}
		if (Module.PackageTemplateModules.size() > Detail::kMaxBytecodeSectionEntries)
		{
			OutError = "package template module count exceeds safety limit";
			return false;
		}
		std::unordered_set<std::string> TemplateModulePaths;
		for (const FBytecodePackageTemplateModule& Template :
		    Module.PackageTemplateModules)
		{
			if (Template.ModulePath.empty() || Template.Source.empty())
			{
				OutError = "package template module path/source must not be empty";
				return false;
			}
			if (!TemplateModulePaths.insert(Template.ModulePath).second)
			{
				OutError = "duplicate package template module: " + Template.ModulePath;
				return false;
			}
			if (Template.Imports.size() > Detail::kMaxBytecodeSectionEntries)
			{
				OutError = "package template import count exceeds safety limit";
				return false;
			}
			if (Template.bFromPackage && Template.bPackageSurface && Template.FacadePath.empty())
			{
				OutError = "surface package template requires a facade path";
				return false;
			}
			for (const FBytecodePackageTemplateImport& Import : Template.Imports)
			{
				if (Import.ResolvedModulePath.empty())
				{
					OutError = "package template import path must not be empty";
					return false;
				}
			}
		}
		if (Module.PackageComponents.size() > Detail::kMaxBytecodeSectionEntries)
		{
			OutError = "package component count exceeds safety limit";
			return false;
		}
		std::unordered_set<std::string> PackageComponentIds;
		for (const FBytecodePackageComponent& Component : Module.PackageComponents)
		{
			if (Component.Id.empty() || !PackageComponentIds.insert(Component.Id).second)
			{
				OutError = Component.Id.empty()
				    ? "package component identity must not be empty"
				    : "duplicate package component identity: " + Component.Id;
				return false;
			}
		}
		std::size_t GenerationCount = 0, GenerationBytes = 0;
		std::vector<std::uint8_t> ProcessingBytes;
		if (Module.SourceMetadata && !EncodeSourceMetadata(*Module.SourceMetadata, ProcessingBytes, OutError)) return false;
		for (const auto& Template : Module.PackageTemplateModules)
		{
			if (!EncodeCompileOperations(Template.CompileGeneratedOperations, ProcessingBytes, OutError) ||
			    (Template.InitialSourceMetadata &&
			        !EncodeSourceMetadata(*Template.InitialSourceMetadata, ProcessingBytes, OutError)))
			{
				return false;
			}

			for (const auto& O : Template.CompileGeneratedOperations)
			{
				if (O.Scope.ModuleId != Template.ModulePath)
				{
					OutError = "processing operation module does not match its package template";
					return false;
				}
			}

			if (!Template.OwnerComponentId.empty() && !PackageComponentIds.count(Template.OwnerComponentId))
			{
				OutError = "unknown template owner component";
				return false;
			}
			for (const auto& Op : Template.GeneratedOperations)
			{
				if (++GenerationCount > 65536 || Op.Source.size() > 1024 * 1024 ||
				    GenerationBytes > 64 * 1024 * 1024 - Op.Source.size() ||
				    Op.TargetId.rfind(Template.ModulePath + '\x1F', 0) != 0)
				{
					OutError = "invalid template generation budget or target";
					return false;
				}
				GenerationBytes += Op.Source.size();
			}
		}
		const auto ValidateOwners =
		    [&](const std::vector<std::string>& Owners,
		        const std::size_t DefinitionCount,
		        const char* Kind)
		{
			if (Owners.size() > DefinitionCount)
			{
				OutError = std::string("package ") + Kind + " owner table exceeds definition table";
				return false;
			}
			for (const std::string& Owner : Owners)
			{
				if (!Owner.empty() && PackageComponentIds.find(Owner) == PackageComponentIds.end())
				{
					OutError = std::string("unknown package ") + Kind + " owner: " + Owner;
					return false;
				}
			}
			return true;
		};
		if (!ValidateOwners(Module.PackageTypeOwners, Module.Types.size(), "type") || !ValidateOwners(Module.PackageInterfaceOwners, Module.Interfaces.size(), "interface") || !ValidateOwners(Module.PackageEnumOwners, Module.Enums.size(), "enum") || !ValidateOwners(Module.PackageFunctionOwners, Module.Functions.size(), "function") || !ValidateOwners(Module.PackageGlobalOwners, Module.GlobalNames.size(), "global"))
		{
			return false;
		}
		if (Module.PackageInitSegments.size() > Detail::kMaxBytecodeSectionEntries)
		{
			OutError = "package initializer segment count exceeds safety limit";
			return false;
		}
		std::size_t PreviousEnd = 0;
		for (const FBytecodePackageInitSegment& Segment :
		    Module.PackageInitSegments)
		{
			if (Segment.OwnerId.empty() || PackageComponentIds.find(Segment.OwnerId) == PackageComponentIds.end())
			{
				OutError = "unknown package initializer owner: " + Segment.OwnerId;
				return false;
			}
			if (Segment.BeginOffset >= Segment.EndOffset || Segment.EndOffset > Module.InitCode.size() || Segment.BeginOffset < PreviousEnd || !InitInstructionBoundaries[Segment.BeginOffset] || !InitInstructionBoundaries[Segment.EndOffset])
			{
				OutError = "invalid or overlapping package initializer segment";
				return false;
			}
			PreviousEnd = Segment.EndOffset;
		}
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutError.clear();
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutError.clear();
		OutError = "size overflow";
		return false;
	}

	std::string VeroScriptBytecodeIO::GetCachePathForScript(
	    const std::string_view ScriptPath,
	    const std::string_view CacheRootDirectory)
	{
		const std::filesystem::path Script = PathUtil::FromUtf8(ScriptPath);
		const std::string FileName = PathUtil::ToUtf8(Script.filename()) + ".vsbc";
		if (CacheRootDirectory.empty())
		{
			std::filesystem::path Directory = Script.parent_path();
			if (Directory.empty())
			{
				Directory = ".";
			}
			return PathUtil::ToUtf8(Directory / ".verocache" / PathUtil::FromUtf8(FileName));
		}

		std::filesystem::path Root = PathUtil::FromUtf8(CacheRootDirectory);
		if (Root.empty())
		{
			Root = ".";
		}

		// Prefer a stable absolute identity so shared cache roots do not thrash on basename.
		std::error_code PathError;
		std::filesystem::path Absolute = std::filesystem::weakly_canonical(Script, PathError);
		if (PathError || Absolute.empty())
		{
			PathError.clear();
			Absolute = std::filesystem::absolute(Script, PathError);
			if (PathError || Absolute.empty())
			{
				Absolute = Script;
			}
		}
		const std::string Identity = PathUtil::ToUtf8(Absolute);
		std::uint64_t Hash = 14695981039346656037ull;
		for (const unsigned char Byte : Identity)
		{
			Hash ^= Byte;
			Hash *= 1099511628211ull;
		}
		char Hex[17];
		for (int Index = 15; Index >= 0; --Index)
		{
			const unsigned Nibble = static_cast<unsigned>((Hash >> (Index * 4)) & 0xFu);
			Hex[15 - Index] = static_cast<char>(Nibble < 10 ? ('0' + Nibble) : ('a' + Nibble - 10));
		}
		Hex[16] = '\0';
		return PathUtil::ToUtf8(Root / Hex / PathUtil::FromUtf8(FileName));
	}
	bool VeroScriptBytecodeIO::SerializeBytecode(
	    const FBytecodeModule& Module,
	    const std::uint64_t SourceFingerprint,
	    std::vector<std::uint8_t>& OutBytes,
	    std::string& OutError)
	try
	{
		Detail::FAnnotationImageGuard AnnotationBudget;
		OutBytes.clear();
		std::string ValidationError;
		if (!ValidateModule(Module, ValidationError))
		{
			OutError = std::move(ValidationError);
			return false;
		}
		const auto SectionCountFits = [](const std::size_t Count)
		{
			return Count <= Detail::kMaxBytecodeSectionEntries;
		};
		if (!SectionCountFits(Module.StringPool.size()) || !SectionCountFits(Module.WStringPool.size()) || !SectionCountFits(Module.Constants.size()) || !SectionCountFits(Module.Types.size()) || !SectionCountFits(Module.Interfaces.size()) || !SectionCountFits(Module.Enums.size()) || !SectionCountFits(Module.Functions.size()) || !SectionCountFits(Module.GlobalNames.size()) || !SectionCountFits(Module.GlobalTypeNames.size()) || !SectionCountFits(Module.GlobalMetadata.size()) || !SectionCountFits(Module.DebugFiles.size()) || !SectionCountFits(Module.GlobalInitRanges.size()) || !SectionCountFits(Module.PackageTemplateModules.size()) || !SectionCountFits(Module.PackageComponents.size()) || !SectionCountFits(Module.PackageTypeOwners.size()) || !SectionCountFits(Module.PackageInterfaceOwners.size()) || !SectionCountFits(Module.PackageEnumOwners.size()) || !SectionCountFits(Module.PackageFunctionOwners.size()) || !SectionCountFits(Module.PackageGlobalOwners.size()) || !SectionCountFits(Module.PackageInitSegments.size()))
		{
			OutError = "bytecode section count exceeds serialization safety limit";
			return false;
		}

		std::vector<std::uint8_t> Buffer;
		bool bWriteOk = true;
		const auto EmitString = [&](const std::string& Value)
		{
			if (bWriteOk && !Detail::WriteString(Buffer, Value))
			{
				bWriteOk = false;
			}
		};
		const auto EmitWString = [&](const std::u16string& Value)
		{
			if (bWriteOk && !Detail::WriteWString(Buffer, Value))
			{
				bWriteOk = false;
			}
		};
		const auto EmitStringList = [&](const std::vector<std::string>& Values)
		{
			if (bWriteOk && !Detail::WriteStringList(Buffer, Values))
			{
				bWriteOk = false;
			}
		};
		const auto EmitDeclMeta = [&](const FDeclMeta& Meta)
		{
			if (bWriteOk && !Detail::WriteDeclMeta(Buffer, Meta))
			{
				bWriteOk = false;
			}
		};

		if (Module.Version != VEROSCRIPT_BC_VERSION)
		{
			OutError = "bytecode module version mismatch";
			return false;
		}
		Bytecode::WriteU32LE(Buffer, VEROSCRIPT_BC_MAGIC);
		Bytecode::WriteU16LE(Buffer, Module.Version.Major);
		Bytecode::WriteU16LE(Buffer, Module.Version.Minor);
		Bytecode::WriteU16LE(Buffer, Module.Version.Patch);
		Bytecode::WriteU64LE(Buffer, SourceFingerprint);
		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.NativeReferences.size()));
		for (const auto& Reference : Module.NativeReferences)
		{
			EmitString(Reference.Name);
			EmitString(Reference.Signature);
			EmitString(Reference.ProviderId);
			Bytecode::WriteU16LE(Buffer, Reference.ArgumentCount);
		}


		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.StringPool.size()));
		for (const std::string& StringValue : Module.StringPool)
		{
			EmitString(StringValue);
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.WStringPool.size()));
		for (const std::u16string& WStringValue : Module.WStringPool)
		{
			EmitWString(WStringValue);
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.Constants.size()));
		for (const FBytecodeConstant& Constant : Module.Constants)
		{
			Detail::WriteConstant(Buffer, Constant);
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.Types.size()));
		for (const FBytecodeTypeDesc& TypeDesc : Module.Types)
		{
			EmitString(TypeDesc.Name);
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(TypeDesc.Kind));
			Bytecode::WriteU32LE(
			    Buffer,
			    static_cast<std::uint32_t>(TypeDesc.BaseTypeIndices.size()));
			for (const std::uint16_t BaseIndex : TypeDesc.BaseTypeIndices)
			{
				Bytecode::WriteU16LE(Buffer, BaseIndex);
			}
			for (std::size_t BaseIndex = 0; BaseIndex < TypeDesc.BaseTypeIndices.size(); ++BaseIndex)
			{
				Bytecode::WriteU8(Buffer, BaseIndex < TypeDesc.BaseInheritanceAccess.size() ? static_cast<std::uint8_t>(TypeDesc.BaseInheritanceAccess[BaseIndex]) : static_cast<std::uint8_t>(EMemberAccess::Private));
			}
			Bytecode::WriteU32LE(Buffer,
			    static_cast<std::uint32_t>(TypeDesc.UsingMembers.size()));
			for (const FBytecodeTypeDesc::FUsingMember& Using : TypeDesc.UsingMembers)
			{
				EmitString(Using.BaseTypeName);
				EmitString(Using.MemberName);
				Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Using.Access));
			}
			Bytecode::WriteU32LE(
			    Buffer,
			    static_cast<std::uint32_t>(TypeDesc.ImplementedInterfaceIndices.size()));
			for (const std::uint16_t InterfaceIndex : TypeDesc.ImplementedInterfaceIndices)
			{
				Bytecode::WriteU16LE(Buffer, InterfaceIndex);
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(TypeDesc.Fields.size()));
			for (const FBytecodeFieldDesc& Field : TypeDesc.Fields)
			{
				EmitString(Field.Name);
				EmitString(Field.TypeName);
				EmitString(Field.DeclaredTypeName.empty()
				        ? Field.TypeName
				        : Field.DeclaredTypeName);
				std::uint8_t Flags = 0;
				if (Field.bHasDefault)
				{
					Flags |= 0x01u;
				}
				if (Field.bHasInitCode)
				{
					Flags |= 0x02u;
				}
				if (Field.bIsMutable)
				{
					Flags |= 0x04u;
				}
				Flags |= static_cast<std::uint8_t>(Field.Access) << 3u;
				if (Field.bIsConst)
				{
					Flags |= 0x20u;
				}
				Bytecode::WriteU8(Buffer, Flags);
				if (Field.bHasDefault)
				{
					Detail::WriteConstant(Buffer, Field.DefaultValue);
				}
				if (Field.bHasInitCode)
				{
					Bytecode::WriteU16LE(Buffer, Field.InitLocalCount);
					if (bWriteOk && !Detail::WriteBytes(Buffer, Field.InitCode))
					{
						bWriteOk = false;
					}
				}
				EmitDeclMeta(Field.DeclMeta);
			}
			Bytecode::WriteU16LE(Buffer, TypeDesc.OwnFieldStartIndex);
			Bytecode::WriteU16LE(Buffer, TypeDesc.DestructorFunctionIndex);
			Bytecode::WriteU16LE(Buffer, TypeDesc.CopyConstructorIndex);
			Bytecode::WriteU8(Buffer, TypeDesc.bDestructorVirtual ? 1u : 0u);
			Bytecode::WriteU8(Buffer, TypeDesc.bDestructorPureVirtual ? 1u : 0u);
			Bytecode::WriteU8(Buffer, TypeDesc.bIsAbstract ? 1u : 0u);
			Bytecode::WriteU8(Buffer, TypeDesc.bIsStatic ? 1u : 0u);
			const bool bHasHostBase = !TypeDesc.HostBaseTypeName.empty();
			Bytecode::WriteU8(Buffer, bHasHostBase ? 1u : 0u);
			if (bHasHostBase)
			{
				EmitString(TypeDesc.HostBaseTypeName);
				Bytecode::WriteU32LE(Buffer, TypeDesc.HostBaseTypeId);
			}
			EmitDeclMeta(TypeDesc.DeclMeta);
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(TypeDesc.StaticFields.size()));
			for (const FBytecodeFieldDesc& StaticField : TypeDesc.StaticFields)
			{
				EmitString(StaticField.Name);
				EmitString(StaticField.TypeName);
				EmitString(StaticField.DeclaredTypeName.empty()
				        ? StaticField.TypeName
				        : StaticField.DeclaredTypeName);
				Bytecode::WriteU8(Buffer, (StaticField.bIsConst ? 1u : 0u) | (StaticField.bConstexpr ? 2u : 0u));
				EmitDeclMeta(StaticField.DeclMeta);
			}
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.Interfaces.size()));
		for (const FBytecodeInterfaceDesc& InterfaceDesc : Module.Interfaces)
		{
			EmitString(InterfaceDesc.Name);
			for (const std::uint16_t BaseIndex : InterfaceDesc.BaseInterfaceIndices)
			{
				if (BaseIndex >= Module.Interfaces.size())
				{
					return false;
				}
			}
			Bytecode::WriteU32LE(
			    Buffer,
			    static_cast<std::uint32_t>(InterfaceDesc.BaseInterfaceIndices.size()));
			for (const std::uint16_t BaseIndex : InterfaceDesc.BaseInterfaceIndices)
			{
				Bytecode::WriteU16LE(Buffer, BaseIndex);
			}
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(InterfaceDesc.Fields.size()));
			for (const FBytecodeFieldDesc& Field : InterfaceDesc.Fields)
			{
				EmitString(Field.Name);
				EmitString(Field.TypeName);
				EmitString(Field.DeclaredTypeName.empty()
				        ? Field.TypeName
				        : Field.DeclaredTypeName);
				std::uint8_t Flags = 0;
				if (Field.bHasDefault)
				{
					Flags |= 0x01u;
				}
				if (Field.bHasInitCode)
				{
					Flags |= 0x02u;
				}
				if (Field.bIsMutable)
				{
					Flags |= 0x04u;
				}
				if (Field.bIsConst)
				{
					Flags |= 0x08u;
				}
				Bytecode::WriteU8(Buffer, Flags);
				if (Field.bHasDefault)
				{
					Detail::WriteConstant(Buffer, Field.DefaultValue);
				}
				if (Field.bHasInitCode)
				{
					Bytecode::WriteU16LE(Buffer, Field.InitLocalCount);
					if (bWriteOk && !Detail::WriteBytes(Buffer, Field.InitCode))
					{
						bWriteOk = false;
					}
				}
				EmitDeclMeta(Field.DeclMeta);
			}
			EmitDeclMeta(InterfaceDesc.DeclMeta);
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(InterfaceDesc.StaticFields.size()));
			for (const FBytecodeFieldDesc& StaticField : InterfaceDesc.StaticFields)
			{
				EmitString(StaticField.Name);
				EmitString(StaticField.TypeName);
				EmitString(StaticField.DeclaredTypeName.empty()
				        ? StaticField.TypeName
				        : StaticField.DeclaredTypeName);
				Bytecode::WriteU8(Buffer, (StaticField.bIsConst ? 1u : 0u) | (StaticField.bConstexpr ? 2u : 0u));
				EmitDeclMeta(StaticField.DeclMeta);
			}
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.Enums.size()));
		for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
		{
			EmitString(EnumDesc.Name);
			Bytecode::WriteU8(Buffer, EnumDesc.bScoped ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0));
			EmitString(EnumDesc.UnderlyingTypeName);
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(EnumDesc.Members.size()));
			for (const FBytecodeEnumMemberDesc& Member : EnumDesc.Members)
			{
				EmitString(Member.Name);
				Bytecode::WriteU64LE(
				    Buffer, static_cast<std::uint64_t>(Member.Value));
				EmitDeclMeta(Member.DeclMeta);
			}
			EmitDeclMeta(EnumDesc.DeclMeta);
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.Functions.size()));
		for (const FBytecodeFunction& Function : Module.Functions)
		{
			EmitString(Function.Name);
			bWriteOk &= Detail::WriteTemplateInstance(Buffer, Function.TemplateInstance);
			EmitString(Function.ModulePath);
			std::uint8_t Flags = 0;
			if (Function.bReturnsVoid)
			{
				Flags |= 0x01u;
			}
			if (Function.bIsMethod)
			{
				Flags |= 0x02u;
			}
			if (Function.bIsVirtual)
			{
				Flags |= 0x04u;
			}
			if (Function.bIsConstMethod)
			{
				Flags |= 0x08u;
			}
			if (Function.bPackageExported)
			{
				Flags |= 0x10u;
			}
			if (Function.bIsAbstract)
			{
				Flags |= 0x20u;
			}
			if (Function.bDeleted)
			{
				Flags |= 0x40u;
			}
			if (Function.bReturnsRef)
			{
				Flags |= 0x80u;
			}
			Bytecode::WriteU8(Buffer, Flags);
			Bytecode::WriteU8(Buffer,
			    static_cast<std::uint8_t>(Function.Access) | (Function.bIsStaticMethod ? 0x04u : 0u) | (Function.bConstexpr ? 0x08u : 0u) | (Function.bUsesLocalStaticStorage ? 0x10u : 0u));
			Bytecode::WriteU16LE(Buffer, Function.OwnerTypeIndex);
			Bytecode::WriteU8(Buffer, Function.ParamCount);
			Bytecode::WriteU16LE(Buffer, Function.LocalCount);
			EmitString(Function.ClosureTypeName);
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Function.SpecialMemberKind));
			Bytecode::WriteU8(Buffer, (Function.bDefaulted ? 1u : 0u) | (Function.bExplicitCopy ? 2u : 0u));
			EmitString(Function.UnavailableReason);
			Bytecode::WriteU64LE(Buffer, Function.LocalStaticSemanticIdentity);
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Function.LocalStaticInitializers.size()));
			for (const auto& Initializer : Function.LocalStaticInitializers)
			{
				Bytecode::WriteU16LE(Buffer, Initializer.GlobalSlot);
				Bytecode::WriteU32LE(Buffer, Initializer.BeginOffset);
				Bytecode::WriteU32LE(Buffer, Initializer.CommitOffset);
				Bytecode::WriteU32LE(Buffer, Initializer.EndOffset);
				Bytecode::WriteU32LE(Buffer, Initializer.ScopeEndOffset);
				EmitString(Initializer.Name);
			}
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Function.Captures.size()));
			for (const auto& Capture : Function.Captures)
			{
				EmitString(Capture.Name);
				EmitString(Capture.TypeName);
				Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Capture.Mode));
				Bytecode::WriteU8(Buffer, (Capture.bWritable ? 1u : 0u) | (Capture.bStableStorage ? 2u : 0u));
			}
			{
				const std::size_t RawFlagCount = Function.ParamFlags.size();
				const std::uint8_t FlagCount = RawFlagCount > 255u
				    ? static_cast<std::uint8_t>(255u)
				    : static_cast<std::uint8_t>(RawFlagCount);
				Bytecode::WriteU8(Buffer, FlagCount);
				for (std::uint8_t FlagIndex = 0; FlagIndex < FlagCount; ++FlagIndex)
				{
					Bytecode::WriteU8(Buffer, Function.ParamFlags[FlagIndex]);
				}
			}
			if (bWriteOk && !Detail::WriteBytes(Buffer, Function.Code))
			{
				bWriteOk = false;
			}
			if (bWriteOk && !Detail::WritePauseMarkers(Buffer, Function.PauseMarkerOffsets))
			{
				bWriteOk = false;
			}
			EmitStringList(Function.LocalNames);
			if (bWriteOk && !Detail::WriteDebugLines(Buffer, Function.DebugLines))
			{
				bWriteOk = false;
			}
			Bytecode::WriteU32LE(
			    Buffer,
			    static_cast<std::uint32_t>(Function.ExceptionRegions.size()));
			for (const FBytecodeExceptionRegion& Region : Function.ExceptionRegions)
			{
				Bytecode::WriteU32LE(Buffer, Region.BeginOffset);
				Bytecode::WriteU32LE(Buffer, Region.EndOffset);
				Bytecode::WriteU32LE(Buffer, Region.FinallyOffset);
				Bytecode::WriteU32LE(Buffer, Region.FinallyEndOffset);
				Bytecode::WriteU32LE(
				    Buffer,
				    static_cast<std::uint32_t>(Region.Catches.size()));
				for (const FBytecodeCatchHandler& Handler : Region.Catches)
				{
					EmitString(Handler.TypeName);
					Bytecode::WriteU16LE(Buffer, Handler.LocalSlot);
					Bytecode::WriteU16LE(Buffer, Handler.RethrowSlot);
					Bytecode::WriteU32LE(Buffer, Handler.HandlerOffset);
				}
			}
			EmitDeclMeta(Function.DeclMeta);
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.GlobalNames.size()));
		for (const std::string& GlobalName : Module.GlobalNames)
		{
			EmitString(GlobalName);
		}
		EmitStringList(Module.GlobalTypeNames);
		Bytecode::WriteU32LE(
		    Buffer,
		    static_cast<std::uint32_t>(Module.GlobalMetadata.size()));
		for (const FBytecodeGlobalMeta& Meta : Module.GlobalMetadata)
		{
			EmitString(Meta.ModulePath);
			EmitString(Meta.DeclaredTypeName);
			Bytecode::WriteU8(Buffer, (Meta.bConst ? 1u : 0u) | (Meta.bConstexpr ? 2u : 0u));
			Bytecode::WriteU8(Buffer, Meta.bExported ? 1u : 0u);
			Bytecode::WriteU8(Buffer, (Meta.bStaticMember ? 1u : 0u) | (Meta.bLocalStatic ? 2u : 0u));
			if (Meta.bConstexpr)
			{
				EmitString(EncodeTemplateArgument({ETemplateParameterKind::Value, {}, *Meta.ConstantValue}));
			}
			EmitDeclMeta(Meta.DeclMeta);
		}
		EmitString(Module.EntryModulePath);

		if (bWriteOk && !Detail::WriteBytes(Buffer, Module.InitCode))
		{
			bWriteOk = false;
		}
		if (bWriteOk && !Detail::WritePauseMarkers(Buffer, Module.InitPauseMarkerOffsets))
		{
			bWriteOk = false;
		}
		Bytecode::WriteU16LE(Buffer, Module.InitLocalCount);
		Bytecode::WriteI32LE(Buffer, Module.MainFunctionIndex);
		EmitStringList(Module.DebugFiles);
		if (bWriteOk && !Detail::WriteDebugLines(Buffer, Module.InitDebugLines))
		{
			bWriteOk = false;
		}
		Bytecode::WriteU32LE(
		    Buffer,
		    static_cast<std::uint32_t>(Module.GlobalInitRanges.size()));
		for (const FBytecodeGlobalInitRange& Range : Module.GlobalInitRanges)
		{
			Bytecode::WriteU16LE(Buffer, Range.GlobalSlot);
			Bytecode::WriteU64LE(
			    Buffer, static_cast<std::uint64_t>(Range.BeginOffset));
			Bytecode::WriteU64LE(
			    Buffer, static_cast<std::uint64_t>(Range.EndOffset));
			Bytecode::WriteU8(Buffer, Range.bDemandInitialized ? 1u : 0u);
		}
		EmitString(Module.PackageInterfaceSource);
		EmitString(Module.PackageInterfaceError);
		Bytecode::WriteU8(
		    Buffer, Module.bContainsPackageDependencies ? 1u : 0u);
		Bytecode::WriteU32LE(
		    Buffer,
		    static_cast<std::uint32_t>(Module.PackageTemplateModules.size()));
		for (const FBytecodePackageTemplateModule& Template :
		    Module.PackageTemplateModules)
		{
			EmitString(Template.ModulePath);
			EmitString(Template.Source);
			EmitString(Template.FacadePath);
			EmitString(Template.OwnerComponentId);
			EmitString(Template.SourceOwnerId);
			if (bWriteOk && !Detail::WriteGeneratedOperations(Buffer, Template.GeneratedOperations))
			{
				bWriteOk = false;
			}

			std::vector<std::uint8_t> ProcessingBytes, InitialMetadataBytes;
			FSourceMetadataSnapshot EmptyInitialMetadata;
			if (!EncodeCompileOperations(Template.CompileGeneratedOperations, ProcessingBytes, OutError) ||
			    !Detail::WriteBytes(Buffer, ProcessingBytes) ||
			    !EncodeSourceMetadata(
			        Template.InitialSourceMetadata ? *Template.InitialSourceMetadata : EmptyInitialMetadata,
			        InitialMetadataBytes, OutError) ||
			    !Detail::WriteBytes(Buffer, InitialMetadataBytes))
			{
				bWriteOk = false;
			}

			std::uint8_t Flags = 0;
			if (Template.bDeclarationOnly)
			{
				Flags |= 0x01u;
			}
			if (Template.bFromPackage)
			{
				Flags |= 0x02u;
			}
			if (Template.bPackageSurface)
			{
				Flags |= 0x04u;
			}
			Bytecode::WriteU8(Buffer, Flags);
			Bytecode::WriteU32LE(
			    Buffer,
			    static_cast<std::uint32_t>(Template.Imports.size()));
			for (const FBytecodePackageTemplateImport& Import : Template.Imports)
			{
				EmitString(Import.ResolvedModulePath);
				Bytecode::WriteU8(Buffer, Import.bReExport ? 1u : 0u);
			}
		}
		Bytecode::WriteU32LE(
		    Buffer, static_cast<std::uint32_t>(Module.PackageComponents.size()));
		for (const FBytecodePackageComponent& Component : Module.PackageComponents)
		{
			EmitString(Component.Id);
			Bytecode::WriteU64LE(Buffer, Component.Fingerprint);
		}
		EmitStringList(Module.PackageTypeOwners);
		EmitStringList(Module.PackageInterfaceOwners);
		EmitStringList(Module.PackageEnumOwners);
		EmitStringList(Module.PackageFunctionOwners);
		EmitStringList(Module.PackageGlobalOwners);
		Bytecode::WriteU32LE(
		    Buffer, static_cast<std::uint32_t>(Module.PackageInitSegments.size()));
		for (const FBytecodePackageInitSegment& Segment :
		    Module.PackageInitSegments)
		{
			EmitString(Segment.OwnerId);
			Bytecode::WriteU64LE(
			    Buffer, static_cast<std::uint64_t>(Segment.BeginOffset));
			Bytecode::WriteU64LE(
			    Buffer, static_cast<std::uint64_t>(Segment.EndOffset));
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.PublicMacros.size()));
		for (const auto& M : Module.PublicMacros)
		{
			bWriteOk &= Detail::WriteString(Buffer, M.Name);
			Bytecode::WriteU8(Buffer, M.bFunctionLike ? 1 : 0);
			Bytecode::WriteU8(Buffer, M.bVariadic ? 1 : 0);
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(M.Parameters.size()));
			for (const auto& P : M.Parameters)
			{
				bWriteOk &= Detail::WriteString(Buffer, P);
			}

			bWriteOk &= Detail::WriteString(Buffer, M.Body);
			bWriteOk &= Detail::WriteString(Buffer, M.OriginModule);
			Bytecode::WriteU64LE(Buffer, M.Revision);
			Bytecode::WriteU64LE(Buffer, M.DefinitionOrdinal);
			Bytecode::WriteU8(Buffer, M.SourceName.empty() ? 0 : 1);
			if (!M.SourceName.empty())
			{
				bWriteOk &= Detail::WriteString(Buffer, M.SourceName);
				Bytecode::WriteU32LE(Buffer, M.Line);
				Bytecode::WriteU32LE(Buffer, M.Column);
				Bytecode::WriteU32LE(Buffer, M.DirectiveStartLine);
				Bytecode::WriteU32LE(Buffer, M.DirectiveEndLine);
			}
		}

		std::vector<std::uint8_t> SourceMetadataBytes;
		FSourceMetadataSnapshot EmptySourceMetadata;
		if (!EncodeSourceMetadata(Module.SourceMetadata ? *Module.SourceMetadata : EmptySourceMetadata,
		        SourceMetadataBytes, OutError) ||
		    !Detail::WriteBytes(Buffer, SourceMetadataBytes))
		{
			bWriteOk = false;
		}

		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.RequiredExternalDependencies.size()));
		for (const auto& D : Module.RequiredExternalDependencies)
		{
			for (const auto* V : {&D.Id, &D.Version, &D.Abi, &D.BuildIdentity, &D.ContentIdentity})
			{
				EmitString(*V);
			}
		}

		const auto EmitScopes = [&](const auto& Declarations)
		{
			Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Declarations.size()));
			for (const auto& Declaration : Declarations)
			{
				bWriteOk &= Detail::WriteTypeScope(Buffer, Declaration.Scope);
				bWriteOk &= Detail::WriteTemplateInstance(Buffer, Declaration.TemplateInstance);
				if constexpr (!std::is_same_v<std::decay_t<decltype(Declaration)>, FBytecodeEnumDesc>)
				{
					Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Declaration.NestedTypes.size()));
					for (const auto& Child : Declaration.NestedTypes)
					{
						bWriteOk &= Detail::WriteTypeScope(Buffer, Child.Scope);
						EmitString(Child.DeclarationId);
					}
				}
			}
		};
		EmitScopes(Module.Types);
		EmitScopes(Module.Interfaces);
		EmitScopes(Module.Enums);
		Bytecode::WriteU32LE(Buffer, static_cast<std::uint32_t>(Module.TemplateDeclarations.size()));
		for (const auto& Declaration : Module.TemplateDeclarations)
		{
			for (const auto* Value : {&Declaration.Id, &Declaration.Kind, &Declaration.Name, &Declaration.Owner,
			         &Declaration.ModulePath, &Declaration.ReturnType, &Declaration.SourceFile})
			{
				EmitString(*Value);
			}
			bWriteOk &= Detail::WriteTemplateParameters(Buffer, Declaration.Parameters);
			bWriteOk &= Detail::WriteTemplateParameters(Buffer, Declaration.EnclosingParameters);
			EmitStringList(Declaration.ParameterTypeNames);
			Bytecode::WriteU8(Buffer, static_cast<std::uint8_t>(Declaration.Access));
			Bytecode::WriteU8(Buffer, (Declaration.bStatic ? 1u : 0u) | (Declaration.bConst ? 2u : 0u) | (Declaration.bExported ? 4u : 0u));
			Bytecode::WriteU32LE(Buffer, Declaration.Line);
			Bytecode::WriteU32LE(Buffer, Declaration.Column);
		}

		Bytecode::WriteU64LE(Buffer, VEROSCRIPT_BYTECODE_LAYOUT);

		if (!bWriteOk)
		{
			OutError = "bytecode contains a string or section too large to serialize";
			return false;
		}
		if (Buffer.size() > VEROSCRIPT_BYTECODE_MAX_BYTES)
		{
			OutError = "serialized bytecode exceeds safety limit";
			return false;
		}
		OutBytes = std::move(Buffer);
		OutError.clear();
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutBytes.clear();
		OutError.clear();
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutBytes.clear();
		OutError.clear();
		OutError = "size overflow";
		return false;
	}

	bool VeroScriptBytecodeIO::WriteFile(
	    const std::string& Path,
	    const FBytecodeModule& Module,
	    const std::uint64_t SourceFingerprint)
	{
		std::string IgnoredError;
		return WriteFile(Path, Module, SourceFingerprint, IgnoredError);
	}

	bool VeroScriptBytecodeIO::WriteFile(
	    const std::string& Path,
	    const FBytecodeModule& Module,
	    const std::uint64_t SourceFingerprint,
	    std::string& OutError)
	try
	{
		OutError.clear();
		std::vector<std::uint8_t> Buffer;
		std::string SerializeError;
		if (!SerializeBytecode(Module, SourceFingerprint, Buffer, SerializeError))
		{
			OutError = std::move(SerializeError);
			return false;
		}
		{
			const std::filesystem::path Parent = PathUtil::FromUtf8(Path).parent_path();
			if (!Parent.empty())
			{
				std::error_code DirectoryError;
				std::filesystem::create_directories(Parent, DirectoryError);
				if (DirectoryError)
				{
					OutError = "failed to create bytecode cache directory: " + DirectoryError.message();
					return false;
				}
			}
		}
		const std::filesystem::path FinalPath = PathUtil::FromUtf8(Path);
		const std::filesystem::path TempPath =
		    PathUtil::MakeTemporarySiblingPath(FinalPath);
		{
			std::ofstream OutputStream(TempPath, std::ios::binary | std::ios::trunc);
			if (!OutputStream)
			{
				OutError = "failed to open temporary bytecode cache file";
				return false;
			}
			OutputStream.write(
			    reinterpret_cast<const char*>(Buffer.data()),
			    static_cast<std::streamsize>(Buffer.size()));
			if (!OutputStream)
			{
				OutputStream.close();
				std::error_code RemoveTemp;
				std::filesystem::remove(TempPath, RemoveTemp);
				OutError = "failed to write temporary bytecode cache file";
				return false;
			}
			OutputStream.close();
			if (!OutputStream)
			{
				std::error_code RemoveTemp;
				std::filesystem::remove(TempPath, RemoveTemp);
				OutError = "failed to flush temporary bytecode cache file";
				return false;
			}
		}

		std::error_code RenameError;
		const bool bReplaced = PathUtil::ReplaceFileAtomically(
		    TempPath, FinalPath, RenameError);
		if (!bReplaced)
		{
			// Windows cannot rename over an existing file — replace then rename.
			// A failed replacement leaves the existing destination intact.
			std::error_code RemoveTemp;
			std::filesystem::remove(TempPath, RemoveTemp);
			OutError = "failed to replace bytecode cache file: " + RenameError.message();
			return false;
		}
		OutError.clear();
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutError = "out of memory";
		return false;
	}
	catch (const std::length_error&)
	{
		OutError = "size overflow";
		return false;
	}

	std::unique_ptr<FBytecodeModule> VeroScriptBytecodeIO::ReadFile(
	    const std::string& Path,
	    const std::uint64_t ExpectedFingerprint,
	    std::string& OutError)
	try
	{
		std::ifstream InputStream(
		    PathUtil::FromUtf8(Path),
		    std::ios::binary | std::ios::ate);
		if (!InputStream)
		{
			OutError = "cannot open bytecode cache: " + Path;
			return nullptr;
		}

		const std::streampos EndPosition = InputStream.tellg();
		if (EndPosition <= 0)
		{
			OutError = "bytecode cache is empty";
			return nullptr;
		}
		const std::streamoff EndOffset = EndPosition;
		if (static_cast<std::uintmax_t>(EndOffset) > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()))
		{
			OutError = "bytecode cache is too large for this platform";
			return nullptr;
		}
		if (static_cast<std::uint64_t>(EndOffset) > VEROSCRIPT_BYTECODE_MAX_BYTES)
		{
			OutError = "bytecode cache exceeds safety limit";
			return nullptr;
		}

		const std::size_t FileSize = static_cast<std::size_t>(EndOffset);
		if (FileSize > static_cast<std::size_t>(
		                   std::numeric_limits<std::streamsize>::max()))
		{
			OutError = "bytecode cache is too large for this stream";
			return nullptr;
		}
		std::vector<std::uint8_t> Buffer(FileSize);
		InputStream.seekg(0, std::ios::beg);
		if (!InputStream.read(
		        reinterpret_cast<char*>(Buffer.data()),
		        static_cast<std::streamsize>(FileSize)) ||
		    InputStream.peek() != std::char_traits<char>::eof())
		{
			OutError = "failed to read bytecode cache";
			return nullptr;
		}
		return DeserializeBytecode(FByteView(Buffer), ExpectedFingerprint, OutError);
	}
	catch (const std::bad_alloc&)
	{
		OutError.clear();
		OutError = "out of memory";
		return nullptr;
	}
	catch (const std::length_error&)
	{
		OutError.clear();
		OutError = "size overflow";
		return nullptr;
	}

	std::unique_ptr<FBytecodeModule> VeroScriptBytecodeIO::DeserializeBytecode(
	    const FByteView Bytes,
	    const std::optional<std::uint64_t> ExpectedFingerprint,
	    std::string& OutError)
	try
	{
		Detail::FAnnotationImageGuard AnnotationBudget;
		OutError.clear();
		if (Bytes.Size != 0 && Bytes.Data == nullptr)
		{
			OutError = "bytecode data pointer is null";
			return nullptr;
		}
		std::size_t Offset = 0;
		const auto* const Data = Bytes.Data;
		const std::size_t Size = Bytes.Size;
		if (Size > VEROSCRIPT_BYTECODE_MAX_BYTES)
		{
			OutError = "bytecode data exceeds safety limit";
			return nullptr;
		}
		if (Size < 26)
		{
			OutError = "bytecode cache header is too small";
			return nullptr;
		}

		if (Bytecode::ReadU64LE(Data + Size - 8) != VEROSCRIPT_BYTECODE_LAYOUT)
		{
			OutError = "bytecode layout mismatch";
			return nullptr;
		}

		const std::uint32_t Magic = Bytecode::ReadU32LE(Data + Offset);
		Offset += 4;
		FSemanticVersion Version;
		Version.Major = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		Version.Minor = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		Version.Patch = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		const std::uint64_t Fingerprint = Bytecode::ReadU64LE(Data + Offset);
		Offset += 8;

		if (Magic != VEROSCRIPT_BC_MAGIC)
		{
			OutError = "invalid bytecode cache magic";
			return nullptr;
		}
		if (Version != VEROSCRIPT_BC_VERSION)
		{
			OutError = "bytecode cache version mismatch";
			return nullptr;
		}
		if (ExpectedFingerprint && Fingerprint != *ExpectedFingerprint)
		{
			OutError = "bytecode cache is stale";
			return nullptr;
		}

		auto Module = std::make_unique<FBytecodeModule>();
		Module->Version = Version;

		std::uint32_t Count = 0;
		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError, 14) || Count > 0xFFFF) return nullptr;
		Module->NativeReferences.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeNativeReference Reference;
			if (!Detail::ReadString(Data, Size, Offset, Reference.Name, OutError)
			    || !Detail::ReadString(Data, Size, Offset, Reference.Signature, OutError)
			    || !Detail::ReadString(Data, Size, Offset, Reference.ProviderId, OutError)
			    || Offset > Size || Size - Offset < 2)
			{
				if (OutError.empty())
				{
					OutError = "unexpected end of native reference descriptor";
				}
				return nullptr;
			}
			Reference.ArgumentCount = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			Module->NativeReferences.push_back(std::move(Reference));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError, 4))
		{
			return nullptr;
		}
		Module->StringPool.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			std::string StringValue;
			if (!Detail::ReadString(Data, Size, Offset, StringValue, OutError))
			{
				return nullptr;
			}
			Module->StringPool.push_back(std::move(StringValue));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError, 4))
		{
			return nullptr;
		}
		Module->WStringPool.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			std::u16string WStringValue;
			if (!Detail::ReadWString(Data, Size, Offset, WStringValue, OutError))
			{
				return nullptr;
			}
			Module->WStringPool.push_back(std::move(WStringValue));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->Constants.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeConstant Constant;
			if (!Detail::ReadConstant(Data, Size, Offset, Constant, OutError))
			{
				return nullptr;
			}
			Module->Constants.push_back(Constant);
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->Types.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeTypeDesc TypeDesc;
			if (!Detail::ReadString(Data, Size, Offset, TypeDesc.Name, OutError))
			{
				return nullptr;
			}
			if (!Detail::CanReadBytes(Size, Offset, 1))
			{
				OutError = "unexpected end of bytecode while reading type kind";
				return nullptr;
			}
			TypeDesc.Kind = static_cast<EScriptObjectKind>(Data[Offset++]);
			std::uint32_t BaseCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, BaseCount, OutError, 2))
			{
				return nullptr;
			}
			TypeDesc.BaseTypeIndices.reserve(BaseCount);
			for (std::uint32_t BaseIdx = 0; BaseIdx < BaseCount; ++BaseIdx)
			{
				if (!Detail::CanReadBytes(Size, Offset, 2))
				{
					OutError = "unexpected end of bytecode while reading base type index";
					return nullptr;
				}
				TypeDesc.BaseTypeIndices.push_back(Bytecode::ReadU16LE(Data + Offset));
				Offset += 2;
			}
			if (!Detail::CanReadBytes(Size, Offset, BaseCount))
			{
				OutError = "unexpected end of bytecode while reading base access";
				return nullptr;
			}
			for (std::uint32_t BaseIdx = 0; BaseIdx < BaseCount; ++BaseIdx)
			{
				const std::uint8_t Access = Data[Offset++];
				if (Access > static_cast<std::uint8_t>(EMemberAccess::Private))
				{
					OutError = "invalid base access";
					return nullptr;
				}
				TypeDesc.BaseInheritanceAccess.push_back(static_cast<EMemberAccess>(Access));
			}
			std::uint32_t UsingCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, UsingCount, OutError, 9))
			{
				return nullptr;
			}
			TypeDesc.UsingMembers.reserve(UsingCount);
			for (std::uint32_t UsingIndex = 0; UsingIndex < UsingCount; ++UsingIndex)
			{
				FBytecodeTypeDesc::FUsingMember Using;
				if (!Detail::ReadString(
				        Data, Size, Offset, Using.BaseTypeName, OutError) ||
				    !Detail::ReadString(
				        Data, Size, Offset, Using.MemberName, OutError) ||
				    !Detail::CanReadBytes(Size, Offset, 1))
				{
					OutError = "invalid using member metadata";
					return nullptr;
				}
				const std::uint8_t Access = Data[Offset++];
				if (Access > static_cast<std::uint8_t>(EMemberAccess::Private) || Using.MemberName.empty())
				{
					OutError = "invalid using member access";
					return nullptr;
				}
				Using.Access = static_cast<EMemberAccess>(Access);
				TypeDesc.UsingMembers.push_back(std::move(Using));
			}
			std::uint32_t InterfaceCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, InterfaceCount, OutError, 2))
			{
				return nullptr;
			}
			TypeDesc.ImplementedInterfaceIndices.reserve(InterfaceCount);
			for (std::uint32_t IfaceIdx = 0; IfaceIdx < InterfaceCount; ++IfaceIdx)
			{
				if (!Detail::CanReadBytes(Size, Offset, 2))
				{
					OutError = "unexpected end of bytecode while reading implemented interface index";
					return nullptr;
				}
				TypeDesc.ImplementedInterfaceIndices.push_back(Bytecode::ReadU16LE(Data + Offset));
				Offset += 2;
			}
			std::uint32_t FieldCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, FieldCount, OutError, 17))
			{
				return nullptr;
			}
			TypeDesc.Fields.reserve(FieldCount);
			for (std::uint32_t FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex)
			{
				FBytecodeFieldDesc Field;
				if (!Detail::ReadString(Data, Size, Offset, Field.Name, OutError))
				{
					return nullptr;
				}
				if (!Detail::ReadString(Data, Size, Offset, Field.TypeName, OutError))
				{
					return nullptr;
				}
				if (!Detail::ReadString(Data, Size, Offset, Field.DeclaredTypeName, OutError))
				{
					return nullptr;
				}
				if (!Detail::CanReadBytes(Size, Offset, 1))
				{
					OutError = "unexpected end of bytecode while reading field flags";
					return nullptr;
				}
				const std::uint8_t Flags = Data[Offset++];
				if ((Flags & ~0x3fu) != 0 || ((Flags >> 3u) & 0x03u) > static_cast<std::uint8_t>(EMemberAccess::Private))
				{
					OutError = "invalid field flags";
					return nullptr;
				}
				Field.bHasDefault = (Flags & 0x01u) != 0;
				Field.bHasInitCode = (Flags & 0x02u) != 0;
				Field.bIsMutable = (Flags & 0x04u) != 0;
				Field.bIsConst = (Flags & 0x20u) != 0;
				Field.Access = static_cast<EMemberAccess>((Flags >> 3u) & 0x03u);
				if (Field.bHasDefault)
				{
					if (!Detail::ReadConstant(Data, Size, Offset, Field.DefaultValue, OutError))
					{
						return nullptr;
					}
				}
				if (Field.bHasInitCode)
				{
					if (!Detail::CanReadBytes(Size, Offset, 2))
					{
						OutError = "unexpected end of bytecode while reading field init locals";
						return nullptr;
					}
					Field.InitLocalCount = Bytecode::ReadU16LE(Data + Offset);
					Offset += 2;
					if (!Detail::ReadBytes(Data, Size, Offset, Field.InitCode, OutError))
					{
						return nullptr;
					}
				}
				if (!Detail::ReadDeclMeta(Data, Size, Offset, Field.DeclMeta, OutError))
				{
					return nullptr;
				}
				TypeDesc.Fields.push_back(std::move(Field));
			}
			if (!Detail::CanReadBytes(Size, Offset, 10))
			{
				OutError = "unexpected end of bytecode while reading type destructor metadata";
				return nullptr;
			}
			TypeDesc.OwnFieldStartIndex = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			TypeDesc.DestructorFunctionIndex = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			TypeDesc.CopyConstructorIndex = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			if (Data[Offset] > 1u)
			{
				OutError = "invalid type destructor virtual flag";
				return nullptr;
			}
			TypeDesc.bDestructorVirtual = Data[Offset++] != 0;
			if (Data[Offset] > 1u)
			{
				OutError = "invalid type destructor pure-virtual flag";
				return nullptr;
			}
			TypeDesc.bDestructorPureVirtual = Data[Offset++] != 0;
			if (Data[Offset] > 1u)
			{
				OutError = "invalid type abstract flag";
				return nullptr;
			}
			TypeDesc.bIsAbstract = Data[Offset++] != 0;
			if (Data[Offset] > 1u)
			{
				OutError = "invalid type static flag";
				return nullptr;
			}
			TypeDesc.bIsStatic = Data[Offset++] != 0;
			if (Offset >= Size)
			{
				OutError = "unexpected end of bytecode while reading type host base flag";
				return nullptr;
			}
			if (Data[Offset] > 1u)
			{
				OutError = "invalid type host base flag";
				return nullptr;
			}
			const bool bHasHostBase = Data[Offset++] != 0;
			if (bHasHostBase)
			{
				if (!Detail::ReadString(Data, Size, Offset, TypeDesc.HostBaseTypeName, OutError))
				{
					return nullptr;
				}
				if (!Detail::CanReadBytes(Size, Offset, 4))
				{
					OutError = "unexpected end of bytecode while reading type host base id";
					return nullptr;
				}
				TypeDesc.HostBaseTypeId = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
			}
			if (!Detail::ReadDeclMeta(Data, Size, Offset, TypeDesc.DeclMeta, OutError))
			{
				return nullptr;
			}
			std::uint32_t StaticFieldCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, StaticFieldCount, OutError, 17))
			{
				return nullptr;
			}
			TypeDesc.StaticFields.reserve(StaticFieldCount);
			for (std::uint32_t StaticIndex = 0; StaticIndex < StaticFieldCount; ++StaticIndex)
			{
				FBytecodeFieldDesc StaticField;
				if (!Detail::ReadString(Data, Size, Offset, StaticField.Name, OutError) || !Detail::ReadString(Data, Size, Offset, StaticField.TypeName, OutError) || !Detail::ReadString(Data, Size, Offset, StaticField.DeclaredTypeName, OutError))
				{
					return nullptr;
				}
				if (!Detail::CanReadBytes(Size, Offset, 1) || Data[Offset] > 3u || Data[Offset] == 2u)
				{
					OutError = "invalid static field const flag";
					return nullptr;
				}
				StaticField.bConstexpr = (Data[Offset] & 2u) != 0;
				StaticField.bIsConst = (Data[Offset++] & 1u) != 0;
				if (!Detail::ReadDeclMeta(Data, Size, Offset, StaticField.DeclMeta, OutError))
				{
					return nullptr;
				}
				TypeDesc.StaticFields.push_back(std::move(StaticField));
			}
			Module->Types.push_back(std::move(TypeDesc));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->Interfaces.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeInterfaceDesc InterfaceDesc;
			if (!Detail::ReadString(Data, Size, Offset, InterfaceDesc.Name, OutError))
			{
				return nullptr;
			}
			std::uint32_t BaseCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, BaseCount, OutError, 2))
			{
				return nullptr;
			}
			InterfaceDesc.BaseInterfaceIndices.reserve(BaseCount);
			for (std::uint32_t BaseIndex = 0; BaseIndex < BaseCount; ++BaseIndex)
			{
				if (!Detail::CanReadBytes(Size, Offset, 2))
				{
					OutError = "unexpected end of bytecode while reading interface base index";
					return nullptr;
				}
				InterfaceDesc.BaseInterfaceIndices.push_back(
				    Bytecode::ReadU16LE(Data + Offset));
				Offset += 2;
			}
			std::uint32_t FieldCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, FieldCount, OutError, 17))
			{
				return nullptr;
			}
			InterfaceDesc.Fields.reserve(FieldCount);
			for (std::uint32_t FieldIndex = 0; FieldIndex < FieldCount; ++FieldIndex)
			{
				FBytecodeFieldDesc Field;
				if (!Detail::ReadString(Data, Size, Offset, Field.Name, OutError) || !Detail::ReadString(Data, Size, Offset, Field.TypeName, OutError) || !Detail::ReadString(Data, Size, Offset, Field.DeclaredTypeName, OutError))
				{
					return nullptr;
				}
				if (Offset >= Size)
				{
					OutError = "unexpected end of bytecode while reading interface field flags";
					return nullptr;
				}
				const std::uint8_t Flags = Data[Offset++];
				if ((Flags & ~0x0fu) != 0)
				{
					OutError = "invalid interface field flags";
					return nullptr;
				}
				Field.bHasDefault = (Flags & 0x01u) != 0;
				Field.bHasInitCode = (Flags & 0x02u) != 0;
				Field.bIsMutable = (Flags & 0x04u) != 0;
				Field.bIsConst = (Flags & 0x08u) != 0;
				if (Field.bHasDefault && !Detail::ReadConstant(Data, Size, Offset, Field.DefaultValue, OutError))
				{
					return nullptr;
				}
				if (Field.bHasInitCode)
				{
					if (!Detail::CanReadBytes(Size, Offset, 2))
					{
						OutError = "unexpected end of bytecode while reading interface field init locals";
						return nullptr;
					}
					Field.InitLocalCount = Bytecode::ReadU16LE(Data + Offset);
					Offset += 2;
					if (!Detail::ReadBytes(Data, Size, Offset, Field.InitCode, OutError))
					{
						return nullptr;
					}
				}
				if (!Detail::ReadDeclMeta(Data, Size, Offset, Field.DeclMeta, OutError))
				{
					return nullptr;
				}
				InterfaceDesc.Fields.push_back(std::move(Field));
			}
			if (!Detail::ReadDeclMeta(Data, Size, Offset, InterfaceDesc.DeclMeta, OutError))
			{
				return nullptr;
			}
			std::uint32_t StaticFieldCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, StaticFieldCount, OutError, 17))
			{
				return nullptr;
			}
			InterfaceDesc.StaticFields.reserve(StaticFieldCount);
			for (std::uint32_t StaticIndex = 0; StaticIndex < StaticFieldCount; ++StaticIndex)
			{
				FBytecodeFieldDesc StaticField;
				if (!Detail::ReadString(Data, Size, Offset, StaticField.Name, OutError) || !Detail::ReadString(Data, Size, Offset, StaticField.TypeName, OutError) || !Detail::ReadString(Data, Size, Offset, StaticField.DeclaredTypeName, OutError))
				{
					return nullptr;
				}
				if (!Detail::CanReadBytes(Size, Offset, 1) || Data[Offset] > 1u)
				{
					OutError = "invalid static interface field const flag";
					return nullptr;
				}
				StaticField.bConstexpr = (Data[Offset] & 2u) != 0;
				StaticField.bIsConst = (Data[Offset++] & 1u) != 0;
				if (!Detail::ReadDeclMeta(Data, Size, Offset, StaticField.DeclMeta, OutError))
				{
					return nullptr;
				}
				InterfaceDesc.StaticFields.push_back(std::move(StaticField));
			}
			Module->Interfaces.push_back(std::move(InterfaceDesc));
		}
		const std::size_t LoadedInterfaceCount = Module->Interfaces.size();
		for (std::size_t Index = 0; Index < LoadedInterfaceCount; ++Index)
		{
			for (const std::uint16_t BaseIndex : Module->Interfaces[Index].BaseInterfaceIndices)
			{
				if (BaseIndex >= LoadedInterfaceCount)
				{
					OutError = "interface base index out of range";
					return nullptr;
				}
			}
		}
		const std::size_t LoadedTypeCount = Module->Types.size();
		for (std::size_t Index = 0; Index < LoadedTypeCount; ++Index)
		{
			const FBytecodeTypeDesc& TypeDesc = Module->Types[Index];
			if (TypeDesc.bDestructorPureVirtual && (!TypeDesc.bDestructorVirtual || !TypeDesc.bIsAbstract))
			{
				OutError = "pure virtual destructor requires virtual abstract type metadata: " + TypeDesc.Name + " (virtual=" + (TypeDesc.bDestructorVirtual ? "true" : "false") + ", abstract=" + (TypeDesc.bIsAbstract ? "true" : "false") + ')';
				return nullptr;
			}
			if (TypeDesc.bIsStatic && ((TypeDesc.Kind != EScriptObjectKind::Class && TypeDesc.Kind != EScriptObjectKind::Struct) || !TypeDesc.BaseTypeIndices.empty() || !TypeDesc.ImplementedInterfaceIndices.empty() || !TypeDesc.Fields.empty() || TypeDesc.DestructorFunctionIndex != std::numeric_limits<std::uint16_t>::max() || TypeDesc.bDestructorVirtual || TypeDesc.bDestructorPureVirtual || TypeDesc.bIsAbstract || !TypeDesc.HostBaseTypeName.empty() || TypeDesc.HostBaseTypeId != 0))
			{
				OutError = "static type has instance-only type metadata: " + TypeDesc.Name;
				return nullptr;
			}
			for (const std::uint16_t BaseIndex : TypeDesc.BaseTypeIndices)
			{
				if (BaseIndex >= LoadedTypeCount)
				{
					OutError = "type base index out of range";
					return nullptr;
				}
			}
			for (const std::uint16_t InterfaceIndex : TypeDesc.ImplementedInterfaceIndices)
			{
				if (InterfaceIndex >= LoadedInterfaceCount)
				{
					OutError = "implemented interface index out of range";
					return nullptr;
				}
			}
			if (TypeDesc.OwnFieldStartIndex > TypeDesc.Fields.size())
			{
				OutError = "type OwnFieldStartIndex out of range";
				return nullptr;
			}
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->Enums.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeEnumDesc EnumDesc;
			if (!Detail::ReadString(Data, Size, Offset, EnumDesc.Name, OutError))
			{
				return nullptr;
			}
			if (Offset >= Size)
			{
				OutError = "unexpected end of bytecode while reading enum scoped flag";
				return nullptr;
			}
			if (Data[Offset] > 1u)
			{
				OutError = "invalid enum scoped flag";
				return nullptr;
			}
			EnumDesc.bScoped = Data[Offset++] != 0;
			if (!Detail::ReadString(
			        Data, Size, Offset, EnumDesc.UnderlyingTypeName, OutError))
			{
				return nullptr;
			}
			std::uint32_t MemberCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, MemberCount, OutError, 16))
			{
				return nullptr;
			}
			EnumDesc.Members.reserve(MemberCount);
			for (std::uint32_t MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
			{
				FBytecodeEnumMemberDesc Member;
				if (!Detail::ReadString(Data, Size, Offset, Member.Name, OutError))
				{
					return nullptr;
				}
				if (!Detail::CanReadBytes(Size, Offset, 8))
				{
					OutError = "unexpected end of bytecode while reading enum member value";
					return nullptr;
				}
				Member.Value =
				    static_cast<std::int64_t>(Bytecode::ReadU64LE(Data + Offset));
				Offset += 8;
				if (!Detail::ReadDeclMeta(Data, Size, Offset, Member.DeclMeta, OutError))
				{
					return nullptr;
				}
				EnumDesc.Members.push_back(std::move(Member));
			}
			if (!Detail::ReadDeclMeta(Data, Size, Offset, EnumDesc.DeclMeta, OutError))
			{
				return nullptr;
			}
			Module->Enums.push_back(std::move(EnumDesc));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeFunction Function;
			if (!Detail::ReadString(Data, Size, Offset, Function.Name, OutError))
			{
				return nullptr;
			}
			if (!Detail::ReadTemplateInstance(Data, Size, Offset, Function.TemplateInstance, OutError)) return nullptr;
			if (!Detail::ReadString(Data, Size, Offset, Function.ModulePath, OutError))
			{
				return nullptr;
			}
			if (!Detail::CanReadBytes(Size, Offset, 7))
			{
				OutError = "unexpected end of bytecode while reading function header";
				return nullptr;
			}
			const std::uint8_t Flags = Data[Offset++];
			Function.bReturnsVoid = (Flags & 0x01u) != 0;
			Function.bIsMethod = (Flags & 0x02u) != 0;
			Function.bIsVirtual = (Flags & 0x04u) != 0;
			Function.bIsConstMethod = (Flags & 0x08u) != 0;
			Function.bPackageExported = (Flags & 0x10u) != 0;
			Function.bIsAbstract = (Flags & 0x20u) != 0;
			Function.bDeleted = (Flags & 0x40u) != 0;
			Function.bReturnsRef = (Flags & 0x80u) != 0;
			const std::uint8_t AccessFlags = Data[Offset++];
			if ((AccessFlags & ~0x1Fu) != 0 || (AccessFlags & 0x03u) > static_cast<std::uint8_t>(EMemberAccess::Private))
			{
				OutError = "invalid function access flags";
				return nullptr;
			}
			Function.Access = static_cast<EMemberAccess>(AccessFlags & 0x03u);
			Function.bIsStaticMethod = (AccessFlags & 0x04u) != 0;
			Function.bConstexpr = (AccessFlags & 0x08u) != 0;
			Function.bUsesLocalStaticStorage = (AccessFlags & 0x10u) != 0;
			Function.OwnerTypeIndex = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			Function.ParamCount = Data[Offset++];
			Function.LocalCount = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			if (!Detail::ReadString(Data, Size, Offset, Function.ClosureTypeName, OutError))
			{
				return nullptr;
			}
			if (!Detail::CanReadBytes(Size, Offset, 2) || Data[Offset] > static_cast<std::uint8_t>(ESpecialMemberKind::Inequality) || (Data[Offset + 1] & ~3u) != 0)
			{
				OutError = "invalid special member metadata";
				return nullptr;
			}
			Function.SpecialMemberKind = static_cast<ESpecialMemberKind>(Data[Offset++]);
			const auto SpecialFlags = Data[Offset++];
			Function.bDefaulted = (SpecialFlags & 1u) != 0;
			Function.bExplicitCopy = (SpecialFlags & 2u) != 0;
			if (!Detail::ReadString(Data, Size, Offset, Function.UnavailableReason, OutError)) return nullptr;
			if (!Detail::CanReadBytes(Size, Offset, 8)) return nullptr;
			Function.LocalStaticSemanticIdentity = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			std::uint32_t InitializerCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, InitializerCount, OutError, 22) || InitializerCount > std::numeric_limits<std::uint16_t>::max())
			{
				return nullptr;
			}
			Function.LocalStaticInitializers.resize(InitializerCount);
			for (auto& Initializer : Function.LocalStaticInitializers)
			{
				if (!Detail::CanReadBytes(Size, Offset, 18)) return nullptr;
				Initializer.GlobalSlot = Bytecode::ReadU16LE(Data + Offset);
				Initializer.BeginOffset = Bytecode::ReadU32LE(Data + Offset + 2);
				Initializer.CommitOffset = Bytecode::ReadU32LE(Data + Offset + 6);
				Initializer.EndOffset = Bytecode::ReadU32LE(Data + Offset + 10);
				Initializer.ScopeEndOffset = Bytecode::ReadU32LE(Data + Offset + 14);
				Offset += 18;
				if (!Detail::ReadString(Data, Size, Offset, Initializer.Name, OutError)) return nullptr;
			}
			if (!Detail::CanReadBytes(Size, Offset, 1))
			{
				OutError = "missing closure capture count";
				return nullptr;
			}
			Function.Captures.resize(Data[Offset++]);
			for (auto& Capture : Function.Captures)
			{
				if (!Detail::ReadString(Data, Size, Offset, Capture.Name, OutError) || !Detail::ReadString(Data, Size, Offset, Capture.TypeName, OutError) || !Detail::CanReadBytes(Size, Offset, 2))
				{
					return nullptr;
				}
				Capture.Mode = static_cast<EClosureCaptureMode>(Data[Offset++]);
				const auto Writable = Data[Offset++];
				if (Capture.Mode > EClosureCaptureMode::Reference || Writable > 3u)
				{
					OutError = "invalid closure capture flags";
					return nullptr;
				}
				Capture.bWritable = (Writable & 1u) != 0;
				Capture.bStableStorage = (Writable & 2u) != 0;
			}
			if (Offset >= Size)
			{
				OutError = "unexpected end of bytecode while reading param flags";
				return nullptr;
			}
			{
				const std::uint8_t FlagCount = Data[Offset++];
				Function.ParamFlags.resize(FlagCount);
				if (!Detail::CanReadBytes(Size, Offset, FlagCount))
				{
					OutError = "unexpected end of bytecode while reading param flags";
					return nullptr;
				}
				for (std::uint8_t FlagIndex = 0; FlagIndex < FlagCount; ++FlagIndex)
				{
					Function.ParamFlags[FlagIndex] = Data[Offset++];
				}
			}
			if (!Detail::ReadBytes(Data, Size, Offset, Function.Code, OutError))
			{
				return nullptr;
			}
			if (!Detail::ReadPauseMarkers(Data, Size, Offset, Function.PauseMarkerOffsets, OutError)) return nullptr;
			if (!Detail::ReadStringList(Data, Size, Offset, Function.LocalNames, OutError))
			{
				return nullptr;
			}
			if (!Detail::ReadDebugLines(Data, Size, Offset, Function.DebugLines, OutError))
			{
				return nullptr;
			}
			std::uint32_t RegionCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, RegionCount, OutError, 20))
			{
				return nullptr;
			}
			Function.ExceptionRegions.reserve(RegionCount);
			for (std::uint32_t RegionIndex = 0; RegionIndex < RegionCount; ++RegionIndex)
			{
				if (Offset > Size || Size - Offset < 20)
				{
					OutError = "unexpected end of bytecode while reading exception region";
					return nullptr;
				}
				FBytecodeExceptionRegion Region;
				Region.BeginOffset = Bytecode::ReadU32LE(Data + Offset);
				Region.EndOffset = Bytecode::ReadU32LE(Data + Offset + 4);
				Region.FinallyOffset = Bytecode::ReadU32LE(Data + Offset + 8);
				Region.FinallyEndOffset = Bytecode::ReadU32LE(Data + Offset + 12);
				Offset += 16;
				std::uint32_t CatchCount = 0;
				if (!Detail::ReadCount(Data, Size, Offset, CatchCount, OutError, 12))
				{
					return nullptr;
				}
				Region.Catches.reserve(CatchCount);
				for (std::uint32_t CatchIndex = 0; CatchIndex < CatchCount; ++CatchIndex)
				{
					FBytecodeCatchHandler Handler;
					if (!Detail::ReadString(Data, Size, Offset, Handler.TypeName, OutError))
					{
						return nullptr;
					}
					if (Offset > Size || Size - Offset < 8)
					{
						OutError = "unexpected end of bytecode while reading catch handler";
						return nullptr;
					}
					Handler.LocalSlot = Bytecode::ReadU16LE(Data + Offset);
					Handler.RethrowSlot = Bytecode::ReadU16LE(Data + Offset + 2);
					Handler.HandlerOffset = Bytecode::ReadU32LE(Data + Offset + 4);
					Offset += 8;
					Region.Catches.push_back(std::move(Handler));
				}
				Function.ExceptionRegions.push_back(std::move(Region));
			}
			if (!Detail::ReadDeclMeta(Data, Size, Offset, Function.DeclMeta, OutError))
			{
				return nullptr;
			}
			Module->Functions.push_back(std::move(Function));
		}

		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->GlobalNames.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			std::string GlobalName;
			if (!Detail::ReadString(Data, Size, Offset, GlobalName, OutError))
			{
				return nullptr;
			}
			Module->GlobalNames.push_back(std::move(GlobalName));
		}
		if (!Detail::ReadStringList(
		        Data, Size, Offset, Module->GlobalTypeNames, OutError))
		{
			return nullptr;
		}
		if (!Detail::ReadCount(Data, Size, Offset, Count, OutError))
		{
			return nullptr;
		}
		Module->GlobalMetadata.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FBytecodeGlobalMeta Meta;
			if (!Detail::ReadString(Data, Size, Offset, Meta.ModulePath, OutError) || !Detail::ReadString(Data, Size, Offset, Meta.DeclaredTypeName, OutError))
			{
				return nullptr;
			}
			if (!Detail::CanReadBytes(Size, Offset, 3))
			{
				OutError = "unexpected end of bytecode while reading global reflection flags";
				return nullptr;
			}
			const std::uint8_t ConstFlag = Data[Offset++];
			const std::uint8_t ExportFlag = Data[Offset++];
			const std::uint8_t StaticFlag = Data[Offset++];
			if (ConstFlag > 3u || (ConstFlag == 2u) || ExportFlag > 1u || StaticFlag > 2u)
			{
				OutError = "invalid global reflection flags";
				return nullptr;
			}
			Meta.bConst = (ConstFlag & 1u) != 0;
			Meta.bConstexpr = (ConstFlag & 2u) != 0;
			Meta.bExported = ExportFlag != 0u;
			Meta.bStaticMember = (StaticFlag & 1u) != 0;
			Meta.bLocalStatic = (StaticFlag & 2u) != 0;
			if (Meta.bConstexpr)
			{
				std::string Encoded;
				FTemplateArgument Value;
				if (!Detail::ReadString(Data, Size, Offset, Encoded, OutError) || !DecodeTemplateArgument(Encoded, Value) || Value.Kind != ETemplateParameterKind::Value)
				{
					OutError = "invalid compiled constexpr global value";
					return nullptr;
				}
				Meta.ConstantValue = std::move(Value.Value);
			}
			if (!Detail::ReadDeclMeta(Data, Size, Offset, Meta.DeclMeta, OutError))
			{
				return nullptr;
			}
			Module->GlobalMetadata.push_back(std::move(Meta));
		}
		if (!Detail::ReadString(
		        Data, Size, Offset, Module->EntryModulePath, OutError))
		{
			return nullptr;
		}

		if (!Detail::ReadBytes(Data, Size, Offset, Module->InitCode, OutError))
		{
			return nullptr;
		}
		if (!Detail::ReadPauseMarkers(Data, Size, Offset, Module->InitPauseMarkerOffsets, OutError)) return nullptr;
		if (!Detail::CanReadBytes(Size, Offset, 2))
		{
			OutError = "unexpected end of bytecode while reading init locals";
			return nullptr;
		}
		Module->InitLocalCount = Bytecode::ReadU16LE(Data + Offset);
		Offset += 2;
		if (!Detail::CanReadBytes(Size, Offset, 4))
		{
			OutError = "unexpected end of bytecode while reading main index";
			return nullptr;
		}
		Module->MainFunctionIndex = Bytecode::ReadI32LE(Data + Offset);
		Offset += 4;
		if (!Detail::ReadStringList(Data, Size, Offset, Module->DebugFiles, OutError))
		{
			return nullptr;
		}
		if (!Detail::ReadDebugLines(Data, Size, Offset, Module->InitDebugLines, OutError))
		{
			return nullptr;
		}
		std::uint32_t InitRangeCount = 0;
		if (!Detail::ReadCount(
		        Data, Size, Offset, InitRangeCount, OutError, 19))
		{
			return nullptr;
		}
		Module->GlobalInitRanges.reserve(InitRangeCount);
		for (std::uint32_t Index = 0; Index < InitRangeCount; ++Index)
		{
			if (Offset > Size || Size - Offset < 19)
			{
				OutError =
				    "unexpected end of bytecode while reading global initializer range";
				return nullptr;
			}
			FBytecodeGlobalInitRange Range;
			Range.GlobalSlot = Bytecode::ReadU16LE(Data + Offset);
			Offset += 2;
			const std::uint64_t Begin = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			const std::uint64_t End = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			if (Begin > std::numeric_limits<std::size_t>::max() || End > std::numeric_limits<std::size_t>::max())
			{
				OutError =
				    "global initializer range is too large for this platform";
				return nullptr;
			}
			Range.BeginOffset = static_cast<std::size_t>(Begin);
			Range.EndOffset = static_cast<std::size_t>(End);
			if (Data[Offset] > 1)
			{
				OutError = "invalid deferred initializer flag";
				return nullptr;
			}
			Range.bDemandInitialized = Data[Offset++] != 0;
			Module->GlobalInitRanges.push_back(Range);
		}
		if (!Detail::ReadString(
		        Data,
		        Size,
		        Offset,
		        Module->PackageInterfaceSource,
		        OutError))
		{
			return nullptr;
		}
		if (!Detail::ReadString(
		        Data,
		        Size,
		        Offset,
		        Module->PackageInterfaceError,
		        OutError))
		{
			return nullptr;
		}
		if (Offset >= Size)
		{
			OutError =
			    "unexpected end of bytecode while reading package dependency flag";
			return nullptr;
		}
		const std::uint8_t PackageDependencyFlag = Data[Offset++];
		if (PackageDependencyFlag > 1u)
		{
			OutError = "invalid package dependency flag";
			return nullptr;
		}
		Module->bContainsPackageDependencies = PackageDependencyFlag != 0;
		std::uint32_t TemplateModuleCount = 0;
		if (!Detail::ReadCount(
		        Data, Size, Offset, TemplateModuleCount, OutError, 13))
		{
			return nullptr;
		}
		Module->PackageTemplateModules.reserve(TemplateModuleCount);
		for (std::uint32_t TemplateIndex = 0;
		    TemplateIndex < TemplateModuleCount;
		    ++TemplateIndex)
		{
			FBytecodePackageTemplateModule Template;
			if (!Detail::ReadString(
			        Data, Size, Offset, Template.ModulePath, OutError) ||
			    !Detail::ReadString(
			        Data, Size, Offset, Template.Source, OutError) ||
			    !Detail::ReadString(
			        Data, Size, Offset, Template.FacadePath, OutError) ||
			    !Detail::ReadString(Data, Size, Offset, Template.OwnerComponentId, OutError) || !Detail::ReadString(Data, Size, Offset, Template.SourceOwnerId, OutError) || !Detail::ReadGeneratedOperations(Data, Size, Offset, Template.GeneratedOperations, OutError))
			{
				return nullptr;
			}
			const auto ReadMetadataBlob = [&](std::size_t& Begin, std::size_t& Length)
			{
				if (!Detail::CanReadBytes(Size, Offset, 4))
				{
					OutError = "truncated template metadata length";
					return false;
				}
				Length = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				Begin = Offset;
				if (Length > 64u * 1024u * 1024u || !Detail::CanReadBytes(Size, Offset, Length))
				{
					OutError = "invalid template metadata length";
					return false;
				}
				Offset += Length;
				return true;
			};
			std::size_t ProcessingBegin = 0, ProcessingSize = 0, InitialBegin = 0, InitialSize = 0;
			if (!ReadMetadataBlob(ProcessingBegin, ProcessingSize) ||
			    !DecodeCompileOperations(
			        Data + ProcessingBegin, ProcessingSize, Template.CompileGeneratedOperations, OutError) ||
			    !ReadMetadataBlob(InitialBegin, InitialSize))
			{
				return nullptr;
			}

			Template.InitialSourceMetadata = DecodeSourceMetadata(Data + InitialBegin, InitialSize, OutError);
			if (!Template.InitialSourceMetadata) return nullptr;
			if (Offset >= Size)
			{
				OutError = "unexpected end while reading package template flags";
				return nullptr;
			}
			const std::uint8_t TemplateFlags = Data[Offset++];
			if ((TemplateFlags & ~0x07u) != 0)
			{
				OutError = "invalid package template flags";
				return nullptr;
			}
			Template.bDeclarationOnly = (TemplateFlags & 0x01u) != 0;
			Template.bFromPackage = (TemplateFlags & 0x02u) != 0;
			Template.bPackageSurface = (TemplateFlags & 0x04u) != 0;
			std::uint32_t ImportCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, ImportCount, OutError, 5))
			{
				return nullptr;
			}
			Template.Imports.reserve(ImportCount);
			for (std::uint32_t ImportIndex = 0;
			    ImportIndex < ImportCount;
			    ++ImportIndex)
			{
				FBytecodePackageTemplateImport Import;
				if (!Detail::ReadString(
				        Data,
				        Size,
				        Offset,
				        Import.ResolvedModulePath,
				        OutError) ||
				    Offset >= Size)
				{
					if (OutError.empty())
					{
						OutError = "unexpected end while reading package template import";
					}
					return nullptr;
				}
				const std::uint8_t ReExport = Data[Offset++];
				if (ReExport > 1u)
				{
					OutError = "invalid package template re-export flag";
					return nullptr;
				}
				Import.bReExport = ReExport != 0;
				Template.Imports.push_back(std::move(Import));
			}
			Module->PackageTemplateModules.push_back(std::move(Template));
		}
		std::uint32_t PackageComponentCount = 0;
		if (!Detail::ReadCount(
		        Data, Size, Offset, PackageComponentCount, OutError, 12))
		{
			return nullptr;
		}
		Module->PackageComponents.reserve(PackageComponentCount);
		for (std::uint32_t ComponentIndex = 0;
		    ComponentIndex < PackageComponentCount;
		    ++ComponentIndex)
		{
			FBytecodePackageComponent Component;
			if (!Detail::ReadString(
			        Data, Size, Offset, Component.Id, OutError) ||
			    Offset > Size || Size - Offset < 8)
			{
				if (OutError.empty())
				{
					OutError = "unexpected end while reading package component";
				}
				return nullptr;
			}
			Component.Fingerprint = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			Module->PackageComponents.push_back(std::move(Component));
		}
		if (!Detail::ReadStringList(
		        Data, Size, Offset, Module->PackageTypeOwners, OutError) ||
		    !Detail::ReadStringList(
		        Data, Size, Offset, Module->PackageInterfaceOwners, OutError) ||
		    !Detail::ReadStringList(
		        Data, Size, Offset, Module->PackageEnumOwners, OutError) ||
		    !Detail::ReadStringList(
		        Data, Size, Offset, Module->PackageFunctionOwners, OutError) ||
		    !Detail::ReadStringList(
		        Data, Size, Offset, Module->PackageGlobalOwners, OutError))
		{
			return nullptr;
		}
		std::uint32_t PackageInitSegmentCount = 0;
		if (!Detail::ReadCount(
		        Data, Size, Offset, PackageInitSegmentCount, OutError, 20))
		{
			return nullptr;
		}
		Module->PackageInitSegments.reserve(PackageInitSegmentCount);
		for (std::uint32_t SegmentIndex = 0;
		    SegmentIndex < PackageInitSegmentCount;
		    ++SegmentIndex)
		{
			FBytecodePackageInitSegment Segment;
			if (!Detail::ReadString(
			        Data, Size, Offset, Segment.OwnerId, OutError) ||
			    Offset > Size || Size - Offset < 16)
			{
				if (OutError.empty())
				{
					OutError =
					    "unexpected end while reading package initializer segment";
				}
				return nullptr;
			}
			const std::uint64_t Begin = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			const std::uint64_t End = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			if (Begin > static_cast<std::uint64_t>(
			                std::numeric_limits<std::size_t>::max()) ||
			    End > static_cast<std::uint64_t>(
			              std::numeric_limits<std::size_t>::max()))
			{
				OutError = "package initializer segment offset overflow";
				return nullptr;
			}
			Segment.BeginOffset = static_cast<std::size_t>(Begin);
			Segment.EndOffset = static_cast<std::size_t>(End);
			Module->PackageInitSegments.push_back(std::move(Segment));
		}

		// The macro tail has its own bounded count, strings and total budget. Check
		// before allocation, even when the enclosing bytecode image is otherwise valid.
		const auto TailStart = Offset;
		const auto ReadMacroCount = [&](std::uint32_t& Count, std::size_t Limit)
		{
			if (!Detail::CanReadBytes(Size, Offset, 4))
			{
				return false;
			}

			Count = Bytecode::ReadU32LE(Data + Offset);
			Offset += 4;
			return Count <= Limit && Count <= (Size - Offset) / 4;
		};
		const auto ReadMacroString = [&](std::string& Text, std::size_t Limit)
		{
			if (!Detail::CanReadBytes(Size, Offset, 4))
			{
				return false;
			}

			const auto Length = Bytecode::ReadU32LE(Data + Offset);
			if (Length > Limit || Offset - TailStart > MaxPublicMacroTableBytes - 4 ||
			    Length > MaxPublicMacroTableBytes - (Offset - TailStart) - 4)
			{
				return false;
			}

			return Detail::ReadString(Data, Size, Offset, Text, OutError);
		};
		const auto ReadMacroBool = [&](bool& Value)
		{
			if (!Detail::CanReadBytes(Size, Offset, 1) || Data[Offset] > 1)
			{
				return false;
			}

			Value = Data[Offset++] != 0;
			return true;
		};
		std::uint32_t MacroCount = 0;
		if (!ReadMacroCount(MacroCount, MaxPublicMacroCount))
		{
			OutError = "invalid public macro tail size or count";
			return nullptr;
		}
		for (std::uint32_t I = 0; I < MacroCount; ++I)
		{
			FPublicMacro M;
			std::uint32_t Params = 0;
			bool bLocation = false;
			if (!ReadMacroString(M.Name, MaxPublicMacroTableBytes) ||
			    !ReadMacroBool(M.bFunctionLike) || !ReadMacroBool(M.bVariadic) ||
			    !ReadMacroCount(Params, MaxPublicMacroParameters))
			{
				OutError = "invalid public macro header";
				return nullptr;
			}
			for (std::uint32_t P = 0; P < Params; ++P)
			{
				std::string Name;
				if (!ReadMacroString(Name, MaxPublicMacroTableBytes))
				{
					OutError = "invalid public macro parameter";
					return nullptr;
				}
				M.Parameters.push_back(std::move(Name));
			}
			if (!ReadMacroString(M.Body, MaxPublicMacroBodyBytes) ||
			    !ReadMacroString(M.OriginModule, MaxPublicMacroTableBytes) ||
			    !Detail::CanReadBytes(Size, Offset, 16))
			{
				OutError = "invalid public macro body or origin";
				return nullptr;
			}
			M.Revision = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			M.DefinitionOrdinal = Bytecode::ReadU64LE(Data + Offset);
			Offset += 8;
			if (!ReadMacroBool(bLocation))
			{
				OutError = "invalid public macro location flag";
				return nullptr;
			}
			if (bLocation)
			{
				if (!ReadMacroString(M.SourceName, MaxPublicMacroTableBytes) ||
				    M.SourceName.empty() || !Detail::CanReadBytes(Size, Offset, 16))
				{
					OutError = "invalid public macro location";
					return nullptr;
				}
				M.Line = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				M.Column = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				M.DirectiveStartLine = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
				M.DirectiveEndLine = Bytecode::ReadU32LE(Data + Offset);
				Offset += 4;
			}
			if (Offset - TailStart > MaxPublicMacroTableBytes)
			{
				OutError = "invalid public macro tail size or count";
				return nullptr;
			}
			Module->PublicMacros.push_back(std::move(M));
		}

		std::uint32_t SourceMetadataSize = 0;
		if (!Detail::CanReadBytes(Size, Offset, 4))
		{
			OutError = "truncated source metadata length";
			return nullptr;
		}
		SourceMetadataSize = Bytecode::ReadU32LE(Data + Offset);
		Offset += 4;
		if (SourceMetadataSize > 64u * 1024u * 1024u ||
		    !Detail::CanReadBytes(Size, Offset, SourceMetadataSize))
		{
			OutError = "invalid source metadata length";
			return nullptr;
		}
		Module->SourceMetadata = DecodeSourceMetadata(Data + Offset, SourceMetadataSize, OutError);
		if (!Module->SourceMetadata) return nullptr;
		Offset += SourceMetadataSize;
		std::uint32_t ExternalCount = 0;
		if (!Detail::ReadCount(Data, Size, Offset, ExternalCount, OutError, 20) || ExternalCount > 4096)
		{
			OutError = "invalid external dependency count";
			return nullptr;
		}
		std::size_t ExternalBytes = 0;
		for (std::uint32_t I = 0; I < ExternalCount; ++I)
		{
			FExternalDependency D;
			for (auto* V : {&D.Id, &D.Version, &D.Abi, &D.BuildIdentity, &D.ContentIdentity})
			{
				if (!Detail::CanReadBytes(Size, Offset, 4) ||
				    Bytecode::ReadU32LE(Data + Offset) > 1024u * 1024u ||
				    Bytecode::ReadU32LE(Data + Offset) > 8u * 1024u * 1024u - ExternalBytes ||
				    !Detail::ReadString(Data, Size, Offset, *V, OutError))
				{
					OutError = "invalid external dependency identity";
					return nullptr;
				}
				ExternalBytes += V->size();
			}
			Module->RequiredExternalDependencies.push_back(std::move(D));
		}
		const auto ReadScopes = [&](auto& Declarations)
		{
			std::uint32_t Count = 0;
			if (!Detail::ReadCount(Data, Size, Offset, Count, OutError, 72) || Count != Declarations.size())
			{
				OutError = "invalid type scope table size";
				return false;
			}
			for (auto& Declaration : Declarations)
			{
				if (!Detail::ReadTypeScope(Data, Size, Offset, Declaration.Scope, OutError) || !Detail::ReadTemplateInstance(Data, Size, Offset, Declaration.TemplateInstance, OutError))
				{
					return false;
				}
				if constexpr (!std::is_same_v<std::decay_t<decltype(Declaration)>, FBytecodeEnumDesc>)
				{
					std::uint32_t ChildCount = 0;
					if (!Detail::ReadCount(Data, Size, Offset, ChildCount, OutError, 76) || ChildCount > 65536) return false;
					for (std::uint32_t Index = 0; Index < ChildCount; ++Index)
					{
						FNestedTypeDeclaration Child;
						if (!Detail::ReadTypeScope(Data, Size, Offset, Child.Scope, OutError) || !Detail::ReadString(Data, Size, Offset, Child.DeclarationId, OutError))
						{
							return false;
						}
						Declaration.NestedTypes.push_back(std::move(Child));
					}
				}
			}
			return true;
		};
		if (!ReadScopes(Module->Types) || !ReadScopes(Module->Interfaces) || !ReadScopes(Module->Enums)) return nullptr;
		std::uint32_t TemplateDeclarationCount = 0;
		if (!Detail::ReadCount(Data, Size, Offset, TemplateDeclarationCount, OutError, 58) || TemplateDeclarationCount > 65536)
		{
			OutError = "invalid template declaration count";
			return nullptr;
		}
		for (std::uint32_t Index = 0; Index < TemplateDeclarationCount; ++Index)
		{
			FTemplateDeclarationInfo Declaration;
			for (auto* Value : {&Declaration.Id, &Declaration.Kind, &Declaration.Name, &Declaration.Owner,
			         &Declaration.ModulePath, &Declaration.ReturnType, &Declaration.SourceFile})
			{
				if (!Detail::ReadString(Data, Size, Offset, *Value, OutError)) return nullptr;
			}
			if (!Detail::ReadTemplateParameters(Data, Size, Offset, Declaration.Parameters, OutError) || !Detail::ReadTemplateParameters(Data, Size, Offset, Declaration.EnclosingParameters, OutError))
			{
				return nullptr;
			}
			std::uint32_t ParameterCount = 0;
			if (!Detail::ReadCount(Data, Size, Offset, ParameterCount, OutError, 4) || ParameterCount > 65536) return nullptr;
			for (std::uint32_t Parameter = 0; Parameter < ParameterCount; ++Parameter)
			{
				std::string Type;
				if (!Detail::ReadString(Data, Size, Offset, Type, OutError)) return nullptr;
				Declaration.ParameterTypeNames.push_back(std::move(Type));
			}
			if (!Detail::CanReadBytes(Size, Offset, 10) || Data[Offset] > static_cast<std::uint8_t>(EMemberAccess::Private) || Data[Offset + 1] > 7u)
			{
				OutError = "invalid template declaration flags";
				return nullptr;
			}
			Declaration.Access = static_cast<EMemberAccess>(Data[Offset]);
			Declaration.bStatic = (Data[Offset + 1] & 1u) != 0;
			Declaration.bConst = (Data[Offset + 1] & 2u) != 0;
			Declaration.bExported = (Data[Offset + 1] & 4u) != 0;
			Declaration.Line = Bytecode::ReadU32LE(Data + Offset + 2);
			Declaration.Column = Bytecode::ReadU32LE(Data + Offset + 6);
			Offset += 10;
			Module->TemplateDeclarations.push_back(std::move(Declaration));
		}

		if (!Detail::CanReadBytes(Size, Offset, 8) || Bytecode::ReadU64LE(Data + Offset) != VEROSCRIPT_BYTECODE_LAYOUT)
		{
			OutError = "bytecode layout mismatch";
			return nullptr;
		}
		Offset += 8;
		if (Offset != Size)
		{
			OutError = "trailing bytes in bytecode file";
			return nullptr;
		}

		if (Module->MainFunctionIndex >= 0)
		{
			if (Module->MainFunctionIndex > 0xFFFF || static_cast<std::size_t>(Module->MainFunctionIndex) >= Module->Functions.size())
			{
				OutError = "main function index out of range";
				return nullptr;
			}
		}
		if (Module->Functions.size() > 0xFFFF)
		{
			OutError = "too many functions in bytecode module";
			return nullptr;
		}
		for (const FBytecodeTypeDesc& TypeDesc : Module->Types)
		{
			if (TypeDesc.DestructorFunctionIndex != static_cast<std::uint16_t>(-1) && TypeDesc.DestructorFunctionIndex >= Module->Functions.size())
			{
				OutError = "destructor function index out of range";
				return nullptr;
			}
		}
		for (const FBytecodeFunction& Function : Module->Functions)
		{
			if (Function.LocalCount < Function.ParamCount)
			{
				OutError = "function LocalCount is less than ParamCount";
				return nullptr;
			}
		}
		for (const FBytecodeConstant& Constant : Module->Constants)
		{
			if ((Constant.Kind == EConstantKind::Str || Constant.Kind == EConstantKind::Enum) && Constant.StringIndex >= Module->StringPool.size())
			{
				OutError = "constant string index out of range";
				return nullptr;
			}
			if (Constant.Kind == EConstantKind::WStr && Constant.StringIndex >= Module->WStringPool.size())
			{
				OutError = "constant wstring index out of range";
				return nullptr;
			}
		}

		if (!ValidateModule(*Module, OutError))
		{
			return nullptr;
		}
		Runtime::RebuildModuleIndices(*Module);
		return Module;
	}
	catch (const std::bad_alloc&)
	{
		OutError.clear();
		OutError = "out of memory";
		return nullptr;
	}
	catch (const std::length_error&)
	{
		OutError.clear();
		OutError = "size overflow";
		return nullptr;
	}
}
