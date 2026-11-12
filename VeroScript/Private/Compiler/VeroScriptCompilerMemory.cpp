// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptRuntime.h"
#include "../Memory/VeroScriptMemoryInternal.h"
#include <algorithm>

namespace VeroScript
{
	void VeroScriptCompiler::RecordMemoryTypeUses(const FProgram& Program)
	{
		const auto SavedModule = CurrentModulePath;
		const auto SavedNamespace = CurrentNamespacePrefix;
		const auto UsesType = [&](const std::string& Type)
		{
			const auto Canonical = CanonicalTypeNameForCoercion(QualifyTypeName(Type, CurrentModulePath));
			bUsesRuntimeMemoryTypes |= Memory::Detail::ContainsMemoryType(Canonical);
		};
		const auto SetScope = [&](const std::string& Name, const std::string& ModulePath)
		{
			CurrentModulePath = ModulePath;
			const auto Dot = Name.rfind('.');
			CurrentNamespacePrefix = Dot == std::string::npos ? std::string{} : Name.substr(0, Dot);
		};
		const auto UsesParameters = [&](const std::vector<FFunctionParam>& Parameters)
		{
			for (const auto& Parameter : Parameters)
			{
				UsesType(Parameter.TypeName);
			}
		};
		const auto DiscardedType = [&](const auto& Type)
		{
			if (!ConstantBranchState || !ConstantBranchState->Guarded(Type.Scope.ScopeId)) return false;
			const auto Name = GetSymbolLinkageName(Type.ModulePath, Type.bExported, Type.Name);
			const auto Matches = [&](const auto& Compiled)
			{
				return Compiled.Name == Name || GenericTemplateBaseLinkage(Compiled.Name) == Name;
			};
			return std::none_of(Module->Types.begin(), Module->Types.end(), Matches) &&
			    std::none_of(Module->Interfaces.begin(), Module->Interfaces.end(), Matches);
		};
		for (const auto& Function : Program.Functions)
		{
			SetScope(Function.Name, Function.ModulePath);
			UsesType(Function.ReturnType);
			UsesParameters(Function.Parameters);
		}
		const auto UsesMembers = [&](const auto& Types)
		{
			for (const auto& Type : Types)
			{
				if (DiscardedType(Type))
				{
					continue;
				}
				SetScope(Type.Name, Type.ModulePath);
				for (const auto& Method : Type.Methods)
				{
					UsesType(Method.ReturnType);
					UsesParameters(Method.Parameters);
				}
			}
		};
		UsesMembers(Program.Structs);
		UsesMembers(Program.Classes);
		UsesMembers(Program.Interfaces);
		const auto UsesConstructors = [&](const auto& Types)
		{
			for (const auto& Type : Types)
			{
				if (DiscardedType(Type))
				{
					continue;
				}
				SetScope(Type.Name, Type.ModulePath);
				for (const auto& Constructor : Type.Constructors)
				{
					UsesParameters(Constructor.Parameters);
				}
			}
		};
		UsesConstructors(Program.Structs);
		UsesConstructors(Program.Classes);
		CurrentModulePath = SavedModule;
		CurrentNamespacePrefix = SavedNamespace;
	}

	bool VeroScriptCompiler::CompileMemoryCall(const FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter)
	{
		const auto Fail = [&](const char* Id)
		{
			ReportErrorId(Id, Call.Location);
			return false;
		};
		const bool Typed = Builtin == EBuiltinId::MemoryRead || Builtin == EBuiltinId::MemoryWrite;
		const bool Identity =
		    Builtin == EBuiltinId::MemoryObjectId || Builtin == EBuiltinId::MemoryObjectAddress;
		bool HostClass = false;
		std::string Scalar;
		if (Typed)
		{
			if (Call.TypeArgs.size() != 1)
			{
				return Fail("diag.memory_numeric_type");
			}
			Scalar = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
			if (Scalar != "int8" && Scalar != "uint8" && Scalar != "char" && Scalar != "unsigned char" &&
			    Scalar != "int16" && Scalar != "uint16" && Scalar != "short" && Scalar != "unsigned short" &&
			    Scalar != "int" && Scalar != "int32" && Scalar != "uint32" && Scalar != "unsigned int" &&
			    Scalar != "int64" && Scalar != "uint64" && Scalar != "long" && Scalar != "unsigned long" &&
			    Scalar != "float" && Scalar != "double")
			{
				return Fail("diag.memory_numeric_type");
			}
		}
		else if (!Call.TypeArgs.empty())
		{
			return Fail("diag.memory_arguments");
		}
		const auto Args = Call.GetArguments();
		const auto* Desc = Builtins::LookupById(Builtin);
		const auto Expected = Typed ? (Builtin == EBuiltinId::MemoryRead ? 2u : 3u)
		    : Identity              ? 1u
		                            : Desc->ArgCount;
		if (Args.size() != Expected && !(Typed && Args.size() == Expected + 1))
		{
			return Fail("diag.memory_arguments");
		}
		if (Builtin == EBuiltinId::MemoryAddress)
		{
			if (Args[0]->Kind != EExprKind::Ref)
			{
				return Fail("diag.memory_address_target");
			}
			const auto& Ref = static_cast<const FRefExpr&>(*Args[0]);
			if (!Ref.Operand || Ref.Operand->Kind == EExprKind::This ||
			    !CanBindImplicitRefArgument(*Ref.Operand, false))
			{
				return Fail("diag.memory_address_target");
			}
			if (!CompileRefArgument(Emitter, *Ref.Operand, Call.Location, false))
			{
				return false;
			}
			Emitter.EmitCallBuiltin(Builtin, 1);
			return true;
		}
		for (std::size_t I = 0; I < Args.size(); ++I)
		{
			std::string Type;
			if (Args[I]->Kind == EExprKind::Ref || !TryResolveExprTypeName(*Args[I], Type))
			{
				return Fail("diag.memory_arguments");
			}
			Type = CanonicalTypeNameForCoercion(std::string(Runtime::StripReferenceTypeName(Type)));
			bool Region = false, Writable = false, Integer = false;
			std::string Coerce;
			switch (Builtin)
			{
			case EBuiltinId::MemoryObjectId:
			case EBuiltinId::MemoryObjectAddress:
				{
					const auto* Host = ActiveBindings ? ActiveBindings->FindType(Type) : nullptr;
					HostClass = Host && Host->Kind == EHostTypeKind::Class;
					const bool Object = Host ? Host->Kind == EHostTypeKind::Class
					                         : (IsClassOrStructType(Type) && !IsStructType(Type)) ||
					        IsInterfaceTypeName(Type);
					if (!Object && !Runtime::IsFunctionTypeName(Type) &&
					    !Runtime::IsDynamicArrayTypeName(Type) && !Runtime::IsCommonContainerTypeName(Type) &&
					    Type.rfind("channel<", 0) != 0)
					{
						return Fail("diag.memory_object_type");
					}
					break;
				}
			case EBuiltinId::MemoryDataAddress:
			case EBuiltinId::MemoryClone:
			case EBuiltinId::MemoryToArray:
				{
					Region = true;
					break;
				}
			case EBuiltinId::MemoryFromArray:
				{
					if (Type != "array<uint8>" && Type != "array<unsigned char>")
					{
						return Fail("diag.memory_byte_array");
					}
					break;
				}
			case EBuiltinId::MemoryCopy:
			case EBuiltinId::MemoryEqual:
				{
					Region = I == 0 || I == 2;
					Writable = Builtin == EBuiltinId::MemoryCopy && I == 0;
					Integer = !Region;
					break;
				}
			case EBuiltinId::MemoryFill:
				{
					Region = I == 0;
					Writable = Region;
					Integer = I == 1 || I == 2;
					if (I == 3)
					{
						Coerce = "unsigned char";
					}
					break;
				}
			case EBuiltinId::MemoryRead:
			case EBuiltinId::MemoryWrite:
				{
					Region = I == 0;
					Writable = Region && Builtin == EBuiltinId::MemoryWrite;
					Integer = I == 1;
					if (Builtin == EBuiltinId::MemoryWrite && I == 2)
					{
						Coerce = Scalar;
					}
					if (I == Expected && Type != "std.memory.byte_order")
					{
						return Fail("diag.memory_byte_order");
					}
					break;
				}
			default:
				{
					break;
				}
			}
			if (Region && (Type != "std.memory.buffer" && Type != "std.memory.view" && Type != "std.memory.readonly_view"))
			{
				return Fail("diag.memory_type");
			}
			if (Writable && (Type == "std.memory.readonly_view" || IsConstAssignTarget(*Args[I])))
			{
				return Fail("diag.memory_readonly");
			}
			if (Integer)
			{
				if (!IsNumericTypeName(Type) || Type == "float" || Type == "double" || Type == "bool")
				{
					return Fail("diag.memory_arguments");
				}
				Coerce = "int64";
			}
			if (!CompileExpression(*Args[I]))
			{
				return false;
			}
			if (!Coerce.empty() && !EmitValueCoercionBetweenTypes(Emitter, Coerce, Type, Call.Location))
			{
				return false;
			}
		}
		if (Typed)
		{
			if (Args.size() == Expected)
			{
				FBytecodeConstant Order;
				Order.Kind = EConstantKind::Enum;
				Order.StringIndex = AddString("std.memory.byte_order");
				Order.I64 = 0;
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Order));
			}
			FBytecodeConstant Type;
			Type.Kind = EConstantKind::Str;
			Type.bIsLinkageString = true;
			Type.StringIndex = AddString(Scalar);
			Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
		}
		if (Identity)
		{
			Emitter.EmitU8Operand(EOpcode::ConstBool, HostClass ? 1 : 0);
		}
		Emitter.EmitCallBuiltin(Builtin, static_cast<std::uint8_t>(Typed ? Expected + 2 : Identity ? 2u
		                                                                                           : Expected));
		if (Desc->bPushVoidResult)
		{
			FBytecodeConstant Zero;
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
		}
		return true;
	}
} // namespace VeroScript
