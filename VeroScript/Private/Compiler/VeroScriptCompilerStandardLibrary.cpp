// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptNative.h"
#include "VeroScriptStandardLibraryServices.h"

namespace VeroScript
{
	bool VeroScriptCompiler::CompileTypedStandardLibraryCall(const FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter)
	{
		const auto Fail = [&]()
		{
			ReportErrorId("diag.stdlib_arguments", Call.Location);
			return false;
		};
		const auto Args = Call.GetArguments();
		const bool Try = Builtin == EBuiltinId::ScalarTryParse;
		if (Builtin == EBuiltinId::TypedTaskAwait || Builtin == EBuiltinId::TypedTaskAwaitFor || Builtin == EBuiltinId::TaskGroupAwaitAll || Builtin == EBuiltinId::TaskArrayAwaitAll)
		{
			const bool Timed = Builtin == EBuiltinId::TypedTaskAwaitFor;
			const bool Array = Builtin == EBuiltinId::TaskGroupAwaitAll || Builtin == EBuiltinId::TaskArrayAwaitAll;
			if (Call.TypeArgs.size() != 1 || Args.size() != (Array ? 1u : Timed ? 3u
			                                                                    : 2u))
			{
				return Fail();
			}
			const auto Element = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
			if (!NativeChannelElementTypeIsValid(Element)) return Fail();
			for (std::size_t I = 0; I < Args.size(); ++I)
			{
				if (Timed && I == 2)
				{
					std::string Type;
					if (!TryResolveExprTypeName(*Args[I], Type) || !StandardLibrary::IsScalarParseType(CanonicalTypeNameForCoercion(Type), true)) return Fail();
				}
				if (!CompileExpression(*Args[I])) return false;
			}
			std::string Extra = Element;
			if (Timed)
			{
				std::uint16_t TypeIndex;
				if (!EnsureStructTypeResolved("optional<" + Element + ">", Extra, &TypeIndex, Call.Location)) return false;
			}
			if (Timed || Array)
			{
				FBytecodeConstant Type;
				Type.Kind = EConstantKind::Str;
				Type.bIsLinkageString = true;
				Type.StringIndex = AddString(Extra);
				Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
			}
			Emitter.EmitCallBuiltin(Builtin, static_cast<std::uint8_t>(Args.size() + (Timed || Array ? 1u : 0u)));
			return true;
		}
		if (Builtin >= EBuiltinId::MakeSynchronized && Builtin <= EBuiltinId::SynchronizedFetchAdd)
		{
			const bool Make = Builtin == EBuiltinId::MakeSynchronized;
			const bool Load = Builtin == EBuiltinId::SynchronizedLoad;
			if (Call.TypeArgs.size() != 1 || Args.size() != (Make || Load ? 1u : 2u)) return Fail();
			const auto Element = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
			if (!NativeChannelElementTypeIsValid(Element)) return Fail();
			if (Builtin == EBuiltinId::SynchronizedFetchAdd && (Element == "bool" || !StandardLibrary::IsScalarParseType(Element))) return Fail();
			for (std::size_t I = 0; I < Args.size(); ++I)
			{
				std::string Type;
				if (!TryResolveExprTypeName(*Args[I], Type)) return Fail();
				Type = CanonicalTypeNameForCoercion(std::string(Runtime::StripReferenceTypeName(Type)));
				if (!NativeElementTypeNamesEqual(Type, !Make && I == 0 ? "channel<" + Element + ">" : Element)) return Fail();
				if (!CompileExpression(*Args[I])) return false;
			}
			if (Make)
			{
				FBytecodeConstant Type;
				Type.Kind = EConstantKind::Str;
				Type.bIsLinkageString = true;
				Type.StringIndex = AddString(Element);
				Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
			}
			Emitter.EmitCallBuiltin(Builtin, static_cast<std::uint8_t>(Args.size() + (Make ? 1u : 0u)));
			if (Builtin == EBuiltinId::SynchronizedStore)
			{
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(FBytecodeConstant{}));
			}
			return true;
		}
		if (Builtin == EBuiltinId::NumericTypeValidate)
		{
			if (!Args.empty() || Call.TypeArgs.size() != 1) return Fail();
			const auto Scalar = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
			if (Scalar == "bool" || !StandardLibrary::IsScalarParseType(Scalar)) return Fail();
			FBytecodeConstant Type;
			Type.Kind = EConstantKind::Str;
			Type.bIsLinkageString = true;
			Type.StringIndex = AddString(Scalar);
			Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
			Emitter.EmitCallBuiltin(Builtin, 1);
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(FBytecodeConstant{}));
			return true;
		}
		const std::size_t BasicCount = Try ? 2 : 1;
		if (Call.TypeArgs.size() != 1 || (Args.size() != BasicCount && Args.size() != BasicCount + 1)) return Fail();
		const auto Scalar = CanonicalTypeNameForCoercion(NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs[0])));
		if (!StandardLibrary::IsScalarParseType(Scalar, Args.size() != BasicCount)) return Fail();
		for (std::size_t I = 0; I < Args.size(); ++I)
		{
			const FExpr* Operand = Args[I];
			if (Try && I == 1 && Operand->Kind == EExprKind::Ref)
			{
				Operand = static_cast<const FRefExpr&>(*Operand).Operand.get();
			}
			std::string Type;
			if (!Operand || !TryResolveExprTypeName(*Operand, Type)) return Fail();
			Type = CanonicalTypeNameForCoercion(std::string(Runtime::StripReferenceTypeName(Type)));
			if (Try && I == 1)
			{
				if (Type != Scalar || IsConstAssignTarget(*Operand) || !CanBindImplicitRefArgument(*Operand, false)) return Fail();
				if (!CompileRefArgument(Emitter, *Operand, Call.Location, false)) return false;
			}
			else
			{
				if (I == 0 ? Type != "string" : !StandardLibrary::IsScalarParseType(Type, true)) return Fail();
				// Keep the original base value intact. VM checks its actual range before
				// conversion, including uint64 values that cannot fit an int64.
				if (!CompileExpression(*Operand)) return false;
			}
		}
		if (Args.size() == BasicCount)
		{
			FBytecodeConstant Base;
			Base.I32 = 10;
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Base));
		}
		FBytecodeConstant Type;
		Type.Kind = EConstantKind::Str;
		Type.bIsLinkageString = true;
		Type.StringIndex = AddString(Scalar);
		Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
		Emitter.EmitCallBuiltin(Builtin, static_cast<std::uint8_t>(BasicCount + 2));
		return true;
	}

	bool VeroScriptCompiler::CompileStandardLibraryOptionalCall(const FCallExpr& Call, EBuiltinId Builtin, FEmitter& Emitter)
	{
		const auto Args = Call.GetArguments();
		if (!Call.TypeArgs.empty() || Args.size() != (Builtin == EBuiltinId::TryGetEnv ? 1u : 0u))
		{
			ReportErrorId("diag.stdlib_arguments", Call.Location);
			return false;
		}
		for (const FExpr* Arg : Args)
		{
			std::string Type;
			if (!TryResolveExprTypeName(*Arg, Type) || Type != "string")
			{
				ReportErrorId("diag.stdlib_arguments", Call.Location);
				return false;
			}
			if (!CompileExpression(*Arg)) return false;
		}
		std::string Linkage;
		std::uint16_t TypeIndex;
		if (!EnsureStructTypeResolved("optional<string>", Linkage, &TypeIndex, Call.Location)) return false;
		FBytecodeConstant Type;
		Type.Kind = EConstantKind::Str;
		Type.bIsLinkageString = true;
		Type.StringIndex = AddString(Linkage);
		Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Type));
		Emitter.EmitCallBuiltin(Builtin, static_cast<std::uint8_t>(Args.size() + 1));
		return true;
	}
}
