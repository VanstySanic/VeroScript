// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptEnumValueIdentity.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		class FStaticInitializationDeferral
		{
		public:
			explicit FStaticInitializationDeferral(bool& bStarted)
			    : Started(bStarted), bPrevious(bStarted)
			{
				Started = false;
			}

			~FStaticInitializationDeferral()
			{
				Started = bPrevious;
			}

		private:
			bool& Started;
			bool bPrevious;
		};
	}

	bool VeroScriptCompiler::CompileDefaultValue(const FExpr& Expression, FBytecodeConstant& OutConstant)
	{
		if (TryCompileDefaultConstant(Expression, OutConstant))
		{
			return true;
		}
		ReportError("field default must be a literal", Expression.Location);
		return false;
	}

	bool VeroScriptCompiler::TryCompileDefaultConstant(const FExpr& Expression, FBytecodeConstant& OutConstant)
	{
		switch (Expression.Kind)
		{
		case EExprKind::IntegerLiteral:
			{
				const FIntegerLiteralExpr& Literal = static_cast<const FIntegerLiteralExpr&>(Expression);
				if (Literal.bIs64 && Literal.bUnsigned)
				{
					OutConstant.Kind = EConstantKind::U64;
					OutConstant.U64 = Literal.UnsignedValue64;
				}
				else if (Literal.bIs64)
				{
					OutConstant.Kind = EConstantKind::I64;
					OutConstant.I64 = Literal.Value64;
				}
				else if (Literal.bUnsigned)
				{
					OutConstant.Kind = EConstantKind::U32;
					OutConstant.U32 = Literal.UnsignedValue;
				}
				else
				{
					OutConstant.Kind = EConstantKind::I32;
					OutConstant.I32 = Literal.Value;
				}
				return true;
			}
		case EExprKind::CharacterLiteral:
			{
				OutConstant.Kind = EConstantKind::I8;
				OutConstant.I8 = static_cast<const FCharacterLiteralExpr&>(Expression).Value;
				return true;
			}
		case EExprKind::FloatLiteral:
			{
				{
					const auto& Literal = static_cast<const FFloatLiteralExpr&>(Expression);
					if (Literal.bFloat32)
					{
						OutConstant.Kind = EConstantKind::F32;
						OutConstant.F32 = static_cast<float>(Literal.Value);
					}
					else
					{
						OutConstant.Kind = EConstantKind::F64;
						OutConstant.F64 = Literal.Value;
					}
				}
				return true;
			}
		case EExprKind::BooleanLiteral:
			{
				OutConstant.Kind = EConstantKind::Bool;
				OutConstant.Bool = static_cast<const FBooleanLiteralExpr&>(Expression).Value;
				return true;
			}
		case EExprKind::StringLiteral:
			{
				OutConstant.Kind = EConstantKind::Str;
				OutConstant.StringIndex = AddString(static_cast<const FStringLiteralExpr&>(Expression).Value);
				return !bHasError;
			}
		case EExprKind::WStringLiteral:
			{
				OutConstant.Kind = EConstantKind::WStr;
				OutConstant.StringIndex = AddWString(static_cast<const FWStringLiteralExpr&>(Expression).Value);
				return !bHasError;
			}
		default:
			{
				if (ActiveTypeSubst.empty()) return false;
				const auto Result = EvaluateTemplateExpression(Expression, CurrentModulePath,
				    ActiveTypeSubst, CurrentNamespacePrefix, false, nullptr, false);
				if (!Result.Succeeded()) return false;
				const auto& Value = Result.Value;
				if (Value.Type.bBoolean)
				{
					OutConstant.Kind = EConstantKind::Bool;
					OutConstant.Bool = Value.Bits != 0;
					return true;
				}
				const auto Mask = Value.Type.Width == 64 ? std::numeric_limits<std::uint64_t>::max()
				                                         : (std::uint64_t{1} << Value.Type.Width) - 1;
				const auto Signed = (!Value.Type.bUnsigned || Value.Type.Width == 64) && (Value.Bits & (std::uint64_t{1} << (Value.Type.Width - 1))) != 0
				    ? -1 - static_cast<std::int64_t>((~Value.Bits) & Mask)
				    : static_cast<std::int64_t>(Value.Bits);
				if (Value.Type.bEnum && Value.Type.bScopedEnum)
				{
					OutConstant.Kind = EConstantKind::Enum;
					OutConstant.I64 = Signed;
					OutConstant.U64 = Value.Bits;
					OutConstant.StringIndex = AddString(Runtime::EnumValueTypeName(*Module, Value.Type.Name));
				}
				else if (Value.Type.bUnsigned)
				{
					OutConstant.Kind = Value.Type.Width == 64 ? EConstantKind::U64 : Value.Type.Width == 32 ? EConstantKind::U32
					    : Value.Type.Width == 16                                                            ? EConstantKind::U16
					                                                                                        : EConstantKind::U8;
					OutConstant.U64 = Value.Bits;
					OutConstant.U32 = static_cast<std::uint32_t>(Value.Bits);
					OutConstant.U16 = static_cast<std::uint16_t>(Value.Bits);
					OutConstant.U8 = static_cast<std::uint8_t>(Value.Bits);
				}
				else
				{
					OutConstant.Kind = Value.Type.Width == 64 ? EConstantKind::I64 : Value.Type.Width == 32 ? EConstantKind::I32
					    : Value.Type.Width == 16                                                            ? EConstantKind::I16
					                                                                                        : EConstantKind::I8;
					OutConstant.I64 = Signed;
					OutConstant.I32 = static_cast<std::int32_t>(Signed);
					OutConstant.I16 = static_cast<std::int16_t>(Signed);
					OutConstant.I8 = static_cast<std::int8_t>(Signed);
				}
				return true;
			}
		}
	}

	std::size_t VeroScriptCompiler::CountRequiredParams(const std::vector<FFunctionParam>& Parameters)
	{
		for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
		{
			if (Parameters[Index].DefaultValue || Parameters[Index].bIsVariadic)
			{
				return Index;
			}
		}
		return Parameters.size();
	}

	bool VeroScriptCompiler::IsVariadicParamList(const std::vector<FFunctionParam>& Parameters)
	{
		return !Parameters.empty() && Parameters.back().bIsVariadic;
	}

	std::size_t VeroScriptCompiler::FixedParamCount(const std::vector<FFunctionParam>& Parameters)
	{
		return IsVariadicParamList(Parameters) ? Parameters.size() - 1 : Parameters.size();
	}

	bool VeroScriptCompiler::ParamsAcceptArgCount(
	    const std::vector<FFunctionParam>& Parameters,
	    const std::size_t ArgCount)
	{
		if (CountRequiredParams(Parameters) > ArgCount)
		{
			return false;
		}
		if (IsVariadicParamList(Parameters))
		{
			return true;
		}
		return ArgCount <= Parameters.size();
	}

	std::string VeroScriptCompiler::MakeDynamicArrayTypeName(const std::string& ElementTypeName)
	{
		return "array<" + ElementTypeName + ">";
	}

	bool VeroScriptCompiler::EmitTrailingDefaultArguments(
	    FEmitter& Emitter,
	    const std::vector<FFunctionParam>& Parameters,
	    const std::size_t ProvidedCount,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst,
	    const FSourceLocation& Location)
	{
		const bool bVariadic = IsVariadicParamList(Parameters);
		const std::size_t FixedCount = FixedParamCount(Parameters);
		if (!bVariadic && ProvidedCount > Parameters.size())
		{
			ReportError("too many arguments", Location);
			return false;
		}
		if (bVariadic)
		{
			if (ProvidedCount > FixedCount)
			{
				// Rest already packed / passed as array by the caller.
				if (ProvidedCount == Parameters.size())
				{
					return true;
				}
				ReportError("too many arguments", Location);
				return false;
			}
		}
		const std::size_t EmitThrough = bVariadic ? FixedCount : Parameters.size();
		for (std::size_t Index = ProvidedCount; Index < EmitThrough; ++Index)
		{
			const FFunctionParam& Param = Parameters[Index];
			if (!Param.DefaultValue)
			{
				ReportError("missing argument for parameter: " + Param.Name, Location);
				return false;
			}
			FBytecodeConstant Constant;
			if (!TryCompileDefaultConstant(*Param.DefaultValue, Constant))
			{
				ReportError("parameter default must be a literal", Param.DefaultValue->Location);
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
			case EConstantKind::Enum:
				{
					Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
					break;
				}
			case EConstantKind::F64:
			case EConstantKind::F32:
				{
					Emitter.EmitU16Operand(EOpcode::ConstF64, AddConstant(Constant));
					break;
				}
			case EConstantKind::Bool:
				{
					Emitter.EmitU8Operand(EOpcode::ConstBool, Constant.Bool ? 1 : 0);
					break;
				}
			case EConstantKind::Str:
			case EConstantKind::WStr:
				{
					Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
					break;
				}
			default:
				{
					ReportError("unsupported parameter default kind", Param.DefaultValue->Location);
					return false;
				}
			}
			std::string LitType;
			if (!TryResolveExprTypeName(*Param.DefaultValue, LitType))
			{
				ReportError("could not type parameter default", Param.DefaultValue->Location);
				return false;
			}
			const std::string ParamType = QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeNameWith(Param.TypeName, Subst), DeclModulePath),
			    DeclModulePath);
			if (!EmitValueCoercionBetweenTypes(Emitter, ParamType, LitType, Location))
			{
				return false;
			}
			if (NeedsPassByValueCopy(ParamType))
			{
				EmitValueCopy(Emitter, ParamType);
			}
			if (Param.bIsRef)
			{
				const auto Slot = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
				Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
			}
		}
		if (bVariadic)
		{
			const FFunctionParam& Variadic = Parameters.back();
			const std::string ElementType = QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeNameWith(Variadic.TypeName, Subst), DeclModulePath),
			    DeclModulePath);
			const std::string ArrayType = MakeDynamicArrayTypeName(ElementType);
			Emitter.EmitU16Operand(EOpcode::NewDynamicArray, AddString(ArrayType));
		}
		return true;
	}

	bool VeroScriptCompiler::EmitVariadicRestArgument(
	    FEmitter& Emitter,
	    const std::vector<FFunctionParam>& Parameters,
	    const FExprArgumentView Arguments,
	    const std::size_t FixedCount,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst,
	    const FSourceLocation& Location)
	{
		if (!IsVariadicParamList(Parameters))
		{
			ReportError("internal: EmitVariadicRestArgument without variadic", Location);
			return false;
		}
		const FFunctionParam& Variadic = Parameters.back();
		const std::string ElementType = QualifyTypeName(
		    NormalizeTypeAlias(SubstituteTypeNameWith(Variadic.TypeName, Subst), DeclModulePath),
		    DeclModulePath);
		const std::string ArrayType = MakeDynamicArrayTypeName(ElementType);
		const std::size_t ArgCount = Arguments.size();
		if (ArgCount < FixedCount)
		{
			ReportError("internal: EmitVariadicRestArgument ArgCount < FixedCount", Location);
			return false;
		}
		if (ArgCount == FixedCount + 1)
		{
			const FExpr& RestExpr = UnwrapRefOperand(*Arguments[FixedCount]);
			std::string RestType;
			if (TryResolveExprTypeName(RestExpr, RestType))
			{
				const std::string RestCanonical = CanonicalTypeNameForCoercion(
				    QualifyTypeName(RestType, CurrentModulePath));
				if (RestCanonical == CanonicalTypeNameForCoercion(ArrayType) || IsSubtypeOf(RestCanonical, ArrayType))
				{
					if (!ValidateDynamicArrayAssignment(ArrayType, RestExpr, Location))
					{
						return false;
					}
					const std::string PreviousExpectedType = ExpressionExpectedType;
					ExpressionExpectedType = ArrayType;
					const bool bCompiled = CompileExpression(RestExpr);
					const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, ArrayType, RestExpr, Location);
					ExpressionExpectedType = PreviousExpectedType;
					if (!bCompiled || !bCoerced)
					{
						return false;
					}
					if (NeedsPassByValueCopy(ArrayType) && !CanTransferCoercedValue(RestExpr, ArrayType))
					{
						EmitValueCopy(Emitter, ArrayType);
					}
					return true;
				}
			}
		}
		Emitter.EmitU16Operand(EOpcode::NewDynamicArray, AddString(ArrayType));
		for (std::size_t Index = FixedCount; Index < ArgCount; ++Index)
		{
			const FExpr& RawArgument = *Arguments[Index];
			if (RawArgument.Kind == EExprKind::Ref)
			{
				ReportError("variadic arguments cannot be ref", RawArgument.Location);
				return false;
			}
			const FExpr& Argument = UnwrapRefOperand(RawArgument);
			std::string ArgType;
			if (TryResolveExprTypeName(Argument, ArgType))
			{
				const std::string ArgCanonical = CanonicalTypeNameForCoercion(
				    QualifyTypeName(ArgType, CurrentModulePath));
				const std::string ElemCanonical = CanonicalTypeNameForCoercion(ElementType);
				// Null uses the same target validation as an ordinary argument below.
				if (ArgCanonical != ElemCanonical && ArgCanonical != "nullptr" && !IsSubtypeOf(ArgCanonical, ElemCanonical))
				{
					ReportErrorId("diag.variadic_argument_type_mismatch_expected", Argument.Location, {DisplayTypeName(ElementType)});
					return false;
				}
			}
			if (!ValidateDynamicArrayAssignment(ElementType, Argument, Location))
			{
				return false;
			}
			Emitter.Emit(EOpcode::Dup);
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType = ElementType;
			const bool bCompiled = CompileExpression(Argument);
			const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, ElementType, Argument, Location);
			ExpressionExpectedType = PreviousExpectedType;
			if (!bCompiled || !bCoerced)
			{
				return false;
			}
			if (NeedsPassByValueCopy(ElementType) && !CanTransferCoercedValue(Argument, ElementType))
			{
				EmitValueCopy(Emitter, ElementType);
			}
			Emitter.EmitCallMethod(AddString("push"), 1);
			Emitter.Emit(EOpcode::Pop);
		}
		return true;
	}

	bool VeroScriptCompiler::TryEvalSwitchCaseConstant(
	    const FExpr& Expression,
	    std::int64_t& OutValue,
	    const FSourceLocation& Location)
	{
		auto Result = EvaluateTemplateExpression(Expression, CurrentModulePath, ActiveTypeSubst, CurrentNamespacePrefix, true);
		if (!Result.Succeeded() || Result.Value.Type.bBoolean)
		{
			ReportError("case label must be an integer or enum constant: " + Result.Reason, Location);
			return false;
		}
		auto Bits = Result.Value.Bits;
		if (!Result.Value.Type.bUnsigned && Result.Value.Type.Width < 64 && (Bits & (std::uint64_t{1} << (Result.Value.Type.Width - 1))))
		{
			Bits |= ~((std::uint64_t{1} << Result.Value.Type.Width) - 1);
		}
		OutValue = static_cast<std::int64_t>(Bits);
		return true;
	}

	bool VeroScriptCompiler::TryEvalSwitchCaseStringConstant(
	    const FExpr& Expression,
	    std::string& OutValue,
	    const FSourceLocation& Location)
	{
		if (Expression.Kind == EExprKind::StringLiteral)
		{
			OutValue = static_cast<const FStringLiteralExpr&>(Expression).Value;
			return true;
		}
		ReportError("case label must be a string literal", Location);
		return false;
	}

	bool VeroScriptCompiler::TryEvalSwitchCaseWStringConstant(
	    const FExpr& Expression,
	    std::u16string& OutValue,
	    const FSourceLocation& Location)
	{
		if (Expression.Kind == EExprKind::WStringLiteral)
		{
			OutValue = static_cast<const FWStringLiteralExpr&>(Expression).Value;
			return true;
		}
		ReportError("case label must be a wstring literal", Location);
		return false;
	}

	bool VeroScriptCompiler::CompileFieldInitExpr(
	    const FExpr& Expression,
	    const std::string& TargetTypeName,
	    std::vector<std::uint8_t>& OutCode,
	    std::vector<FDebugLine>* const OutDebugLines)
	{
		// This buffer executes per value. Global static initialization must remain
		// queued for the module initializer instead of being captured in it.
		FStaticInitializationDeferral DeferredStatics(bStaticFieldInitializationStarted);
		if (OutDebugLines)
		{
			OutDebugLines->clear();
		}
		// Keep CurrentFunction so brace-element temps update LocalCount; field-default
		// compilation already clears CurrentFunction in CompileFieldDefault.
		const std::string PreviousExpectedType = ExpressionExpectedType;
		ExpressionExpectedType = TargetTypeName;
		std::vector<std::uint8_t> SavedInitCode = std::move(Module->InitCode);
		std::vector<FDebugLine> SavedInitDebugLines = std::move(Module->InitDebugLines);
		Module->InitCode.clear();
		Module->InitDebugLines.clear();
		FBytecodeFunction* const ScratchFunction = CurrentFunction;
		// Emit into a scratch buffer (InitCode), not the live function stream.
		CurrentFunction = nullptr;
		if (OutDebugLines && Expression.Location.Line != 0 && ExpressionCrossesSourceLine(Expression, Expression.Location))
		{
			CurrentEmitLocation = Expression.Location;
			FEmitter AnchorEmitter = MakeEmitter(Module->InitCode, &Module->InitDebugLines);
			AnchorEmitter.Emit(EOpcode::Nop);
		}
		bool bCompiled = CompileExpression(Expression);
		if (bCompiled && !bHasError)
		{
			FEmitter Emitter = MakeEmitter(Module->InitCode, &Module->InitDebugLines);
			bCompiled = EmitValueCoercionForTarget(Emitter, TargetTypeName, Expression, Expression.Location);
		}
		if (bCompiled && !bHasError)
		{
			OutCode = std::move(Module->InitCode);
			if (OutDebugLines)
			{
				*OutDebugLines = std::move(Module->InitDebugLines);
			}
		}
		Module->InitCode = std::move(SavedInitCode);
		Module->InitDebugLines = std::move(SavedInitDebugLines);
		CurrentFunction = ScratchFunction;
		ExpressionExpectedType = PreviousExpectedType;
		// Temps allocated while CurrentFunction was null did not bump LocalCount; sync now.
		if (CurrentFunction && NextLocalSlot > CurrentFunction->LocalCount)
		{
			CurrentFunction->LocalCount = NextLocalSlot;
		}
		return bCompiled && !bHasError;
	}

	bool VeroScriptCompiler::CompileNativeArrayInit(
	    const std::string& TypeName,
	    const FArrayInitExpr& Init,
	    FEmitter& Emitter,
	    const FSourceLocation& Location)
	{
		if (Init.Location.Line != 0)
		{
			CurrentEmitLocation = Init.Location;
		}
		std::string ElementType;
		std::uint32_t Length = 0;
		if (!Runtime::ParseNativeArrayType(TypeName, ElementType, Length))
		{
			ReportError("invalid native array type: " + TypeName, Location);
			return false;
		}
		std::string LimitError;
		if (!Runtime::ValidateNativeArrayTypeLimits(TypeName, &LimitError))
		{
			ReportErrorId("diag.native_array_size_exceeds_limit", Location);
			return false;
		}
		if (Init.Elements.size() > Length)
		{
			ReportError("too many elements in array initializer", Location);
			return false;
		}

		std::string LifetimeElement = ElementType;
		std::string NestedElement;
		std::uint32_t NestedLength = 0;
		while (Runtime::ParseNativeArrayType(LifetimeElement, NestedElement, NestedLength))
		{
			LifetimeElement = NestedElement;
		}
		const bool RawStorage = (!Init.Elements.empty() && IsStructType(LifetimeElement)) || GenericTemplateBaseLinkage(LifetimeElement) == "type";
		if (Init.Elements.size() < Length && !EnsureDefaultValueConstruction(ElementType, Location)) return false;
		Emitter.EmitU16Operand(RawStorage ? EOpcode::NewUninitializedNativeArray : EOpcode::NewNativeArray,
		    AddString(TypeName));
		if (!RawStorage && Init.Elements.empty()) return true;
		// Keep partial value arrays on the operand stack so exception unwinding
		// releases completed elements before entering the caller's handler.
		const std::uint16_t TempSlot = RawStorage ? 0 : AllocateAssignTemp();
		if (!RawStorage)
		{
			Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		}
		for (std::size_t Index = 0; Index < Init.Elements.size(); ++Index)
		{
			if (!RawStorage)
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
			}
			FBytecodeConstant IndexConstant;
			IndexConstant.Kind = EConstantKind::I32;
			IndexConstant.I32 = static_cast<std::int32_t>(Index);
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(IndexConstant));
			const FExpr& Element = *Init.Elements[Index];
			if (Element.Kind == EExprKind::ArrayInit && Runtime::IsNativeArrayTypeName(ElementType))
			{
				if (!CompileNativeArrayInit(
				        ElementType,
				        static_cast<const FArrayInitExpr&>(Element),
				        Emitter,
				        Element.Location))
				{
					return false;
				}
			}
			else if (Element.Kind == EExprKind::ArrayInit && (Runtime::IsDynamicArrayTypeName(ElementType) || Runtime::IsSetTypeName(ElementType) || Runtime::IsMultiSetTypeName(ElementType) || Runtime::IsOrderedMultiSetTypeName(ElementType) || Runtime::IsDequeTypeName(ElementType) || Runtime::IsQueueTypeName(ElementType) || Runtime::IsStackTypeName(ElementType)))
			{
				if (!CompileContainerListInit(
				        ElementType,
				        static_cast<const FArrayInitExpr&>(Element),
				        Emitter,
				        Element.Location))
				{
					return false;
				}
			}
			else if (Element.Kind == EExprKind::MapInit && (Runtime::IsMapTypeName(ElementType) || Runtime::IsOrderedMapTypeName(ElementType)))
			{
				if (!CompileMapInit(
				        ElementType,
				        static_cast<const FMapInitExpr&>(Element),
				        Emitter,
				        Element.Location))
				{
					return false;
				}
			}
			else
			{
				std::vector<std::uint8_t> ElementCode;
				std::vector<FDebugLine> ElementLines;
				if (!CompileFieldInitExpr(Element, ElementType, ElementCode, &ElementLines))
				{
					return false;
				}
				Emitter.AppendCode(ElementCode, &ElementLines);
			}
			if (NeedsPassByValueCopy(ElementType) && !CanTransferCoercedValue(Element, ElementType))
			{
				EmitValueCopy(Emitter, ElementType);
			}
			Emitter.Emit(EOpcode::InitIndex);
			if (!RawStorage)
			{
				Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
			}
		}
		if (RawStorage && Init.Elements.size() < Length)
		{
			// Emit a loop so a short initializer for a large value array does not
			// expand the bytecode by the number of default-constructed elements.
			const auto Counter = AllocateAssignTemp();
			FBytecodeConstant Index;
			Index.Kind = EConstantKind::I32;
			Index.I32 = static_cast<std::int32_t>(Init.Elements.size());
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Index));
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Counter);
			const auto Loop = Emitter.CurrentOffset();
			Emitter.EmitU16Operand(EOpcode::LoadLocal, Counter);
			Index.I32 = static_cast<std::int32_t>(Length);
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Index));
			Emitter.Emit(EOpcode::Lt);
			const auto Exit = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, Counter);
			if (!EmitDefaultInitializedValue(Emitter, ElementType, Location)) return false;
			Emitter.Emit(EOpcode::InitIndex);
			Emitter.EmitU16Operand(EOpcode::IncLocal, Counter);
			const auto Back = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
			Emitter.PatchI32(Back, Loop - static_cast<std::int32_t>(Back + 4));
			Emitter.PatchI32(Exit, Emitter.CurrentOffset() - static_cast<std::int32_t>(Exit + 4));
		}
		if (!RawStorage)
		{
			Emitter.EmitU16Operand(EOpcode::TakeLocal, TempSlot);
		}
		return true;
	}

	bool VeroScriptCompiler::CompileContainerListInit(
	    const std::string& TypeName,
	    const FArrayInitExpr& Init,
	    FEmitter& Emitter,
	    const FSourceLocation& Location)
	{
		if (Init.Location.Line != 0)
		{
			CurrentEmitLocation = Init.Location;
		}
		std::string ElementType;
		const char* FillMethod = nullptr;
		EOpcode NewOpcode = EOpcode::NewDynamicArray;
		if (Runtime::IsDynamicArrayTypeName(TypeName))
		{
			if (!Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
			{
				ReportError("invalid dynamic array type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewDynamicArray;
			FillMethod = "push";
		}
		else if (Runtime::IsSetTypeName(TypeName))
		{
			if (!ParseSetElementTypeForCompile(TypeName, ElementType))
			{
				ReportError("invalid set type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewSet;
			FillMethod = "add";
		}
		else if (Runtime::IsMultiSetTypeName(TypeName) || Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			if (!Runtime::ParseMultiSetElementType(TypeName, ElementType) || !IsAllowedMapOrSetKeyTypeForCompile(ElementType))
			{
				ReportError("invalid multiset type: " + TypeName, Location);
				return false;
			}
			NewOpcode = Runtime::IsOrderedMultiSetTypeName(TypeName)
			    ? EOpcode::NewOrderedMap
			    : EOpcode::NewMap;
			FillMethod = "add";
		}
		else if (Runtime::IsDequeTypeName(TypeName))
		{
			if (!Runtime::ParseDequeElementType(TypeName, ElementType))
			{
				ReportError("invalid deque type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewDeque;
			FillMethod = "push_back";
		}
		else if (Runtime::IsQueueTypeName(TypeName))
		{
			if (!Runtime::ParseQueueElementType(TypeName, ElementType))
			{
				ReportError("invalid queue type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewQueue;
			FillMethod = "push";
		}
		else if (Runtime::IsStackTypeName(TypeName))
		{
			if (!Runtime::ParseStackElementType(TypeName, ElementType))
			{
				ReportError("invalid stack type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewStack;
			FillMethod = "push";
		}
		else
		{
			ReportError("brace list initializer is not supported for " + TypeName, Location);
			return false;
		}

		Emitter.EmitU16Operand(NewOpcode, AddString(TypeName));
		const std::uint16_t TempSlot = AllocateAssignTemp();
		Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		const bool bReservable = NewOpcode == EOpcode::NewDynamicArray || NewOpcode == EOpcode::NewSet || NewOpcode == EOpcode::NewMap || NewOpcode == EOpcode::NewOrderedMap;
		if (bReservable && !Init.Elements.empty() && Init.Elements.size() <= static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
			FBytecodeConstant CapacityConstant;
			CapacityConstant.Kind = EConstantKind::I32;
			CapacityConstant.I32 = static_cast<std::int32_t>(Init.Elements.size());
			Emitter.EmitU16Operand(
			    EOpcode::ConstI32, AddConstant(CapacityConstant));
			Emitter.EmitCallMethod(AddString("reserve"), 1);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		}
		for (const std::unique_ptr<FExpr>& Element : Init.Elements)
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
			if (Element->Kind == EExprKind::ArrayInit && Runtime::IsNativeArrayTypeName(ElementType))
			{
				if (!CompileNativeArrayInit(
				        ElementType,
				        static_cast<const FArrayInitExpr&>(*Element),
				        Emitter,
				        Element->Location))
				{
					return false;
				}
			}
			else if (Element->Kind == EExprKind::ArrayInit && (Runtime::IsDynamicArrayTypeName(ElementType) || Runtime::IsSetTypeName(ElementType) || Runtime::IsMultiSetTypeName(ElementType) || Runtime::IsOrderedMultiSetTypeName(ElementType) || Runtime::IsDequeTypeName(ElementType) || Runtime::IsQueueTypeName(ElementType) || Runtime::IsStackTypeName(ElementType)))
			{
				if (!CompileContainerListInit(
				        ElementType,
				        static_cast<const FArrayInitExpr&>(*Element),
				        Emitter,
				        Element->Location))
				{
					return false;
				}
			}
			else if (Element->Kind == EExprKind::MapInit && (Runtime::IsMapTypeName(ElementType) || Runtime::IsOrderedMapTypeName(ElementType)))
			{
				if (!CompileMapInit(
				        ElementType,
				        static_cast<const FMapInitExpr&>(*Element),
				        Emitter,
				        Element->Location))
				{
					return false;
				}
			}
			else
			{
				std::vector<std::uint8_t> ElementCode;
				std::vector<FDebugLine> ElementLines;
				if (!CompileFieldInitExpr(*Element, ElementType, ElementCode, &ElementLines))
				{
					return false;
				}
				Emitter.AppendCode(ElementCode, &ElementLines);
			}
			Emitter.EmitCallMethod(AddString(FillMethod), 1);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		}
		Emitter.EmitU16Operand(EOpcode::TakeLocal, TempSlot);
		return true;
	}

	bool VeroScriptCompiler::CompileMapInit(
	    const std::string& TypeName,
	    const FMapInitExpr& Init,
	    FEmitter& Emitter,
	    const FSourceLocation& Location)
	{
		if (Init.Location.Line != 0)
		{
			CurrentEmitLocation = Init.Location;
		}
		std::string KeyType;
		std::string ValueType;
		EOpcode NewOpcode = EOpcode::NewMap;
		if (Runtime::IsMapTypeName(TypeName))
		{
			if (!ParseMapTypeForCompile(TypeName, KeyType, ValueType))
			{
				ReportError("invalid map type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewMap;
		}
		else if (Runtime::IsOrderedMapTypeName(TypeName))
		{
			if (!ParseOrderedMapTypeForCompile(TypeName, KeyType, ValueType))
			{
				ReportError("invalid ordered_map type: " + TypeName, Location);
				return false;
			}
			NewOpcode = EOpcode::NewOrderedMap;
		}
		else
		{
			ReportError("map initializer is not supported for " + TypeName, Location);
			return false;
		}

		Emitter.EmitU16Operand(NewOpcode, AddString(TypeName));
		const std::uint16_t TempSlot = AllocateAssignTemp();
		Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		if (!Init.Entries.empty() && Init.Entries.size() <= static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)()))
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
			FBytecodeConstant CapacityConstant;
			CapacityConstant.Kind = EConstantKind::I32;
			CapacityConstant.I32 = static_cast<std::int32_t>(Init.Entries.size());
			Emitter.EmitU16Operand(
			    EOpcode::ConstI32, AddConstant(CapacityConstant));
			Emitter.EmitCallMethod(AddString("reserve"), 1);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		}
		for (const FMapInitEntry& Entry : Init.Entries)
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
			std::vector<std::uint8_t> KeyCode;
			std::vector<FDebugLine> KeyLines;
			if (!CompileFieldInitExpr(*Entry.Key, KeyType, KeyCode, &KeyLines))
			{
				return false;
			}
			Emitter.AppendCode(KeyCode, &KeyLines);
			if (Entry.Value->Kind == EExprKind::ArrayInit && Runtime::IsNativeArrayTypeName(ValueType))
			{
				if (!CompileNativeArrayInit(
				        ValueType,
				        static_cast<const FArrayInitExpr&>(*Entry.Value),
				        Emitter,
				        Entry.Value->Location))
				{
					return false;
				}
			}
			else if (Entry.Value->Kind == EExprKind::ArrayInit && (Runtime::IsDynamicArrayTypeName(ValueType) || Runtime::IsSetTypeName(ValueType) || Runtime::IsMultiSetTypeName(ValueType) || Runtime::IsOrderedMultiSetTypeName(ValueType) || Runtime::IsDequeTypeName(ValueType) || Runtime::IsQueueTypeName(ValueType) || Runtime::IsStackTypeName(ValueType)))
			{
				if (!CompileContainerListInit(
				        ValueType,
				        static_cast<const FArrayInitExpr&>(*Entry.Value),
				        Emitter,
				        Entry.Value->Location))
				{
					return false;
				}
			}
			else if (Entry.Value->Kind == EExprKind::MapInit && (Runtime::IsMapTypeName(ValueType) || Runtime::IsOrderedMapTypeName(ValueType)))
			{
				if (!CompileMapInit(
				        ValueType,
				        static_cast<const FMapInitExpr&>(*Entry.Value),
				        Emitter,
				        Entry.Value->Location))
				{
					return false;
				}
			}
			else
			{
				std::vector<std::uint8_t> ValueCode;
				std::vector<FDebugLine> ValueLines;
				if (!CompileFieldInitExpr(*Entry.Value, ValueType, ValueCode, &ValueLines))
				{
					return false;
				}
				Emitter.AppendCode(ValueCode, &ValueLines);
			}
			Emitter.Emit(EOpcode::SetIndex);
			Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
		}
		Emitter.EmitU16Operand(EOpcode::TakeLocal, TempSlot);
		return true;
	}

	bool VeroScriptCompiler::CompileFieldDefault(const FFieldDecl& Field, FBytecodeFieldDesc& OutFieldDesc)
	{
		OutFieldDesc.bIsMutable = Field.bIsMutable;
		OutFieldDesc.bIsConst = Field.bIsConst;
		OutFieldDesc.bConstexpr = Field.bConstexpr;
		OutFieldDesc.Access = Field.Access;
		if (!Field.DefaultValue)
		{
			return true;
		}
		// Some defaults lower directly into field-owned buffers (for example
		// fixed arrays), so guard the complete default compilation as well.
		FStaticInitializationDeferral DeferredStatics(bStaticFieldInitializationStarted);
		FLambdaInitializerContext InitializerContext(*this);
		CurrentInitializerStatement = nullptr;
		CurrentInitializerExpression = Field.DefaultValue.get();
		CurrentInitializerField = &Field;

		const std::string FieldTypeForDefault = !OutFieldDesc.TypeName.empty()
		    ? OutFieldDesc.TypeName
		    : SubstituteTypeName(Field.TypeName);

		const std::uint16_t SavedNextLocalSlot = NextLocalSlot;
		const std::uint32_t SavedNextAssignTempId = NextAssignTempId;
		FBytecodeFunction* const SavedFunction = CurrentFunction;
		std::vector<FScope> SavedScopes;
		SavedScopes.swap(Scopes);
		CurrentFunction = nullptr;
		NextLocalSlot = 0;
		NextAssignTempId = 0;

		const auto RestoreFieldDefaultContext = [&]()
		{
			Scopes.swap(SavedScopes);
			CurrentFunction = SavedFunction;
			NextLocalSlot = SavedNextLocalSlot;
			NextAssignTempId = SavedNextAssignTempId;
		};

		const auto FinishFieldDefault = [&]() -> bool
		{
			OutFieldDesc.InitLocalCount = NextLocalSlot;
			RestoreFieldDefaultContext();
			return !bHasError;
		};

		const auto FailFieldDefaultEarly = [&]() -> bool
		{
			RestoreFieldDefaultContext();
			return false;
		};

		if (Field.DefaultValue->Kind == EExprKind::ArrayInit && Runtime::IsNativeArrayTypeName(FieldTypeForDefault))
		{
			FEmitter InitEmitter = MakeEmitter(OutFieldDesc.InitCode, nullptr);
			if (!CompileNativeArrayInit(
			        OutFieldDesc.TypeName.empty() ? FieldTypeForDefault : OutFieldDesc.TypeName,
			        static_cast<const FArrayInitExpr&>(*Field.DefaultValue),
			        InitEmitter,
			        Field.Location))
			{
				return FailFieldDefaultEarly();
			}
			OutFieldDesc.bHasInitCode = true;
			return FinishFieldDefault();
		}

		if (Field.DefaultValue->Kind == EExprKind::ArrayInit && (Runtime::IsDynamicArrayTypeName(FieldTypeForDefault) || Runtime::IsSetTypeName(FieldTypeForDefault) || Runtime::IsMultiSetTypeName(FieldTypeForDefault) || Runtime::IsOrderedMultiSetTypeName(FieldTypeForDefault) || Runtime::IsDequeTypeName(FieldTypeForDefault) || Runtime::IsQueueTypeName(FieldTypeForDefault) || Runtime::IsStackTypeName(FieldTypeForDefault)))
		{
			FEmitter InitEmitter = MakeEmitter(OutFieldDesc.InitCode, nullptr);
			if (!CompileContainerListInit(
			        OutFieldDesc.TypeName.empty() ? FieldTypeForDefault : OutFieldDesc.TypeName,
			        static_cast<const FArrayInitExpr&>(*Field.DefaultValue),
			        InitEmitter,
			        Field.Location))
			{
				return FailFieldDefaultEarly();
			}
			OutFieldDesc.bHasInitCode = true;
			return FinishFieldDefault();
		}

		if (Field.DefaultValue->Kind == EExprKind::MapInit && (Runtime::IsMapTypeName(FieldTypeForDefault) || Runtime::IsOrderedMapTypeName(FieldTypeForDefault)))
		{
			FEmitter InitEmitter = MakeEmitter(OutFieldDesc.InitCode, nullptr);
			if (!CompileMapInit(
			        OutFieldDesc.TypeName.empty() ? FieldTypeForDefault : OutFieldDesc.TypeName,
			        static_cast<const FMapInitExpr&>(*Field.DefaultValue),
			        InitEmitter,
			        Field.Location))
			{
				return FailFieldDefaultEarly();
			}
			OutFieldDesc.bHasInitCode = true;
			return FinishFieldDefault();
		}

		// Keep scoped enum defaults on the ordinary typed initializer path.
		// Its coercion validates enum identity; numeric literal folding does not.
		if (!IsScopedEnumTypeName(FieldTypeForDefault) && TryCompileDefaultConstant(*Field.DefaultValue, OutFieldDesc.DefaultValue))
		{
			const auto FailFieldDefault = [&](const char* Message) -> bool
			{
				ReportError(Message, Field.Location);
				return FailFieldDefaultEarly();
			};

			if (FieldTypeForDefault == "wstring" && OutFieldDesc.DefaultValue.Kind == EConstantKind::Str)
			{
				return FailFieldDefault("cannot assign string to wstring without to_wstring");
			}
			if (FieldTypeForDefault == "string" && OutFieldDesc.DefaultValue.Kind == EConstantKind::WStr)
			{
				return FailFieldDefault("cannot assign wstring to string without to_string");
			}

			const std::string EffectiveFieldType = NormalizeTypeAlias(FieldTypeForDefault);
			const auto TryDefaultNumber = [&](long double& OutValue) -> bool
			{
				switch (OutFieldDesc.DefaultValue.Kind)
				{
				case EConstantKind::I8:
					{
						OutValue = OutFieldDesc.DefaultValue.I8;
						return true;
					}
				case EConstantKind::U8:
					{
						OutValue = OutFieldDesc.DefaultValue.U8;
						return true;
					}
				case EConstantKind::I16:
					{
						OutValue = OutFieldDesc.DefaultValue.I16;
						return true;
					}
				case EConstantKind::U16:
					{
						OutValue = OutFieldDesc.DefaultValue.U16;
						return true;
					}
				case EConstantKind::I32:
					{
						OutValue = OutFieldDesc.DefaultValue.I32;
						return true;
					}
				case EConstantKind::U32:
					{
						OutValue = OutFieldDesc.DefaultValue.U32;
						return true;
					}
				case EConstantKind::I64:
					{
						OutValue = static_cast<long double>(OutFieldDesc.DefaultValue.I64);
						return true;
					}
				case EConstantKind::U64:
					{
						OutValue = static_cast<long double>(OutFieldDesc.DefaultValue.U64);
						return true;
					}
				case EConstantKind::F32:
					{
						OutValue = OutFieldDesc.DefaultValue.F32;
						return true;
					}
				case EConstantKind::F64:
					{
						OutValue = OutFieldDesc.DefaultValue.F64;
						return true;
					}
				case EConstantKind::Bool:
					{
						OutValue = OutFieldDesc.DefaultValue.Bool ? 1.0L : 0.0L;
						return true;
					}
				default:
					return false;
				}
			};
			if (EffectiveFieldType == "char" || EffectiveFieldType == "short" || EffectiveFieldType == "unsigned char" || EffectiveFieldType == "unsigned short")
			{
				long double Numeric = 0.0L;
				if (!TryDefaultNumber(Numeric) || !std::isfinite(Numeric))
				{
					return FailFieldDefault("invalid narrow integer field default");
				}
				const long double Truncated = std::trunc(Numeric);
				if (EffectiveFieldType == "char")
				{
					if (Truncated < std::numeric_limits<std::int8_t>::min() || Truncated > std::numeric_limits<std::int8_t>::max())
					{
						return FailFieldDefault("char field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I8;
					OutFieldDesc.DefaultValue.I8 = static_cast<std::int8_t>(Truncated);
				}
				else if (EffectiveFieldType == "short")
				{
					if (Truncated < std::numeric_limits<std::int16_t>::min() || Truncated > std::numeric_limits<std::int16_t>::max())
					{
						return FailFieldDefault("short field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I16;
					OutFieldDesc.DefaultValue.I16 = static_cast<std::int16_t>(Truncated);
				}
				else if (EffectiveFieldType == "unsigned char")
				{
					if (Numeric < 0.0L || Truncated > std::numeric_limits<std::uint8_t>::max())
					{
						return FailFieldDefault("unsigned char field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U8;
					OutFieldDesc.DefaultValue.U8 = static_cast<std::uint8_t>(Truncated);
				}
				else
				{
					if (Numeric < 0.0L || Truncated > std::numeric_limits<std::uint16_t>::max())
					{
						return FailFieldDefault("unsigned short field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U16;
					OutFieldDesc.DefaultValue.U16 = static_cast<std::uint16_t>(Truncated);
				}
			}
			else if (EffectiveFieldType == "uint64" || EffectiveFieldType == "unsigned long")
			{
				if (OutFieldDesc.DefaultValue.Kind == EConstantKind::I64)
				{
					if (OutFieldDesc.DefaultValue.I64 < 0)
					{
						return FailFieldDefault("uint64 field default overflow");
					}
					const std::uint64_t Value =
					    static_cast<std::uint64_t>(OutFieldDesc.DefaultValue.I64);
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U64;
					OutFieldDesc.DefaultValue.U64 = Value;
				}
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::U64)
				{
					long double Numeric = 0.0L;
					if (!TryDefaultNumber(Numeric) || !std::isfinite(Numeric))
					{
						return FailFieldDefault("invalid uint64 field default");
					}
					const long double Truncated = std::trunc(Numeric);
					if (Numeric < 0.0L || Truncated >= 18446744073709551616.0L)
					{
						return FailFieldDefault("uint64 field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U64;
					OutFieldDesc.DefaultValue.U64 = static_cast<std::uint64_t>(Truncated);
				}
			}
			else if (EffectiveFieldType == "int64" || EffectiveFieldType == "long")
			{
				if (OutFieldDesc.DefaultValue.Kind == EConstantKind::U64)
				{
					if (OutFieldDesc.DefaultValue.U64 > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
					{
						return FailFieldDefault("int64 field default overflow");
					}
					const std::int64_t Value =
					    static_cast<std::int64_t>(OutFieldDesc.DefaultValue.U64);
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I64;
					OutFieldDesc.DefaultValue.I64 = Value;
				}
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::I64)
				{
					long double Numeric = 0.0L;
					if (!TryDefaultNumber(Numeric) || !std::isfinite(Numeric))
					{
						return FailFieldDefault("invalid int64 field default");
					}
					const long double Truncated = std::trunc(Numeric);
					if (Truncated < -9223372036854775808.0L || Truncated >= 9223372036854775808.0L)
					{
						return FailFieldDefault("int64 field default overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I64;
					OutFieldDesc.DefaultValue.I64 = static_cast<std::int64_t>(Truncated);
				}
			}
			else if (Runtime::IsUnsignedTypeName(EffectiveFieldType))
			{
				if (OutFieldDesc.DefaultValue.Kind == EConstantKind::I64)
				{
					const std::int64_t Value = OutFieldDesc.DefaultValue.I64;
					if (Value < 0 || static_cast<std::uint64_t>(Value) > std::numeric_limits<std::uint32_t>::max())
					{
						return FailFieldDefault("to_unsigned overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U32;
					OutFieldDesc.DefaultValue.U32 = static_cast<std::uint32_t>(Value);
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::U64)
				{
					const std::uint64_t Value = OutFieldDesc.DefaultValue.U64;
					if (Value > std::numeric_limits<std::uint32_t>::max())
					{
						return FailFieldDefault("to_unsigned overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U32;
					OutFieldDesc.DefaultValue.U32 = static_cast<std::uint32_t>(Value);
				}
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::U32)
				{
					long double Numeric = 0.0L;
					if (!TryDefaultNumber(Numeric) || !std::isfinite(Numeric))
					{
						return FailFieldDefault("invalid unsigned field default");
					}
					const long double Truncated = std::trunc(Numeric);
					if (Numeric < 0.0L || Truncated >= 4294967296.0L)
					{
						return FailFieldDefault("to_unsigned overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::U32;
					OutFieldDesc.DefaultValue.U32 = static_cast<std::uint32_t>(Truncated);
				}
			}
			else if (IsSignedIntegerTypeName(EffectiveFieldType))
			{
				if (OutFieldDesc.DefaultValue.Kind == EConstantKind::I64)
				{
					const std::int64_t Value = OutFieldDesc.DefaultValue.I64;
					if (Value < std::numeric_limits<std::int32_t>::min() || Value > std::numeric_limits<std::int32_t>::max())
					{
						return FailFieldDefault("to_int overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I32;
					OutFieldDesc.DefaultValue.I32 = static_cast<std::int32_t>(Value);
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::U64)
				{
					const std::uint64_t Value = OutFieldDesc.DefaultValue.U64;
					if (Value > static_cast<std::uint64_t>(
					                std::numeric_limits<std::int32_t>::max()))
					{
						return FailFieldDefault("to_int overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I32;
					OutFieldDesc.DefaultValue.I32 = static_cast<std::int32_t>(Value);
				}
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::I32)
				{
					long double Numeric = 0.0L;
					if (!TryDefaultNumber(Numeric) || !std::isfinite(Numeric))
					{
						return FailFieldDefault("invalid integer field default");
					}
					const long double Truncated = std::trunc(Numeric);
					if (Truncated < -2147483648.0L || Truncated >= 2147483648.0L)
					{
						return FailFieldDefault("to_int overflow");
					}
					OutFieldDesc.DefaultValue = {};
					OutFieldDesc.DefaultValue.Kind = EConstantKind::I32;
					OutFieldDesc.DefaultValue.I32 = static_cast<std::int32_t>(Truncated);
				}
			}
			else if (EffectiveFieldType == "bool")
			{
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::Bool)
				{
					return FailFieldDefault("invalid bool field default");
				}
			}
			else if (EffectiveFieldType == "float" || EffectiveFieldType == "double")
			{
				const bool bFloat32Target = EffectiveFieldType == "float";
				const auto StoreFloating = [&](const double Value)
				{
					if (bFloat32Target)
					{
						OutFieldDesc.DefaultValue.Kind = EConstantKind::F32;
						OutFieldDesc.DefaultValue.F32 = static_cast<float>(Value);
					}
					else
					{
						OutFieldDesc.DefaultValue.Kind = EConstantKind::F64;
						OutFieldDesc.DefaultValue.F64 = Value;
					}
				};
				if (OutFieldDesc.DefaultValue.Kind == EConstantKind::I32)
				{
					StoreFloating(static_cast<double>(OutFieldDesc.DefaultValue.I32));
					OutFieldDesc.DefaultValue.I32 = 0;
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::U32)
				{
					StoreFloating(static_cast<double>(OutFieldDesc.DefaultValue.U32));
					OutFieldDesc.DefaultValue.U32 = 0;
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::I64)
				{
					StoreFloating(static_cast<double>(OutFieldDesc.DefaultValue.I64));
					OutFieldDesc.DefaultValue.I64 = 0;
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::U64)
				{
					StoreFloating(static_cast<double>(OutFieldDesc.DefaultValue.U64));
					OutFieldDesc.DefaultValue.U64 = 0;
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::F32 && !bFloat32Target)
				{
					StoreFloating(static_cast<double>(OutFieldDesc.DefaultValue.F32));
					OutFieldDesc.DefaultValue.F32 = 0.0f;
				}
				else if (OutFieldDesc.DefaultValue.Kind == EConstantKind::F64 && bFloat32Target)
				{
					const double Value = OutFieldDesc.DefaultValue.F64;
					StoreFloating(Value);
					OutFieldDesc.DefaultValue.F64 = 0.0;
				}
				else if (OutFieldDesc.DefaultValue.Kind != (bFloat32Target ? EConstantKind::F32 : EConstantKind::F64))
				{
					return FailFieldDefault("invalid floating field default");
				}
			}
			else if (EffectiveFieldType == "string")
			{
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::Str)
				{
					return FailFieldDefault("invalid string field default");
				}
			}
			else if (EffectiveFieldType == "wstring")
			{
				if (OutFieldDesc.DefaultValue.Kind != EConstantKind::WStr)
				{
					return FailFieldDefault("invalid wstring field default");
				}
			}
			else
			{
				return FailFieldDefault("invalid field default");
			}

			OutFieldDesc.bHasDefault = true;
			return FinishFieldDefault();
		}
		if (!ValidateDynamicArrayAssignment(
		        OutFieldDesc.TypeName.empty()
		            ? QualifyTypeName(Field.TypeName, CurrentModulePath)
		            : OutFieldDesc.TypeName,
		        *Field.DefaultValue,
		        Field.Location))
		{
			return FailFieldDefaultEarly();
		}
		if (!CompileFieldInitExpr(*Field.DefaultValue, OutFieldDesc.TypeName, OutFieldDesc.InitCode))
		{
			return FailFieldDefaultEarly();
		}
		if (NeedsPassByValueCopy(OutFieldDesc.TypeName) && !CanTransferCoercedValue(*Field.DefaultValue, OutFieldDesc.TypeName))
		{
			auto Emitter = MakeEmitter(OutFieldDesc.InitCode, nullptr);
			EmitValueCopy(Emitter, OutFieldDesc.TypeName);
		}
		OutFieldDesc.bHasInitCode = true;
		return FinishFieldDefault();
	}

	bool VeroScriptCompiler::CompileOwnFieldDefaults(
	    const std::string& OwnerTypeName,
	    const std::uint16_t OwnerIndex,
	    const std::vector<FFieldDecl>& Fields,
	    const bool bInterface)
	{
		struct FRestoreContext
		{
			~FRestoreContext()
			{
				Compiler.CurrentOwnerTypeLinkage = std::move(Owner);
				Compiler.CurrentAccessFunctionLinkage = std::move(Access);
				Compiler.CurrentNamespacePrefix = std::move(Namespace);
			}
			VeroScriptCompiler& Compiler;
			std::string Owner, Access, Namespace;
		} Restore{*this, CurrentOwnerTypeLinkage, CurrentAccessFunctionLinkage, CurrentNamespacePrefix};
		CurrentOwnerTypeLinkage = OwnerTypeName;
		CurrentAccessFunctionLinkage = OwnerTypeName + "::@initializer";
		const bool Local = bInterface ? Module->Interfaces[OwnerIndex].Scope.bLocal
		                              : Module->Types[OwnerIndex].Scope.bLocal;
		if (!Local)
		{
			CurrentNamespacePrefix = NamespaceFromFunctionLinkage(OwnerTypeName);
		}
		const auto TargetFields = [&]() -> std::vector<FBytecodeFieldDesc>&
		{
			return bInterface ? Module->Interfaces[OwnerIndex].Fields : Module->Types[OwnerIndex].Fields;
		};
		const auto OwnCount = static_cast<std::size_t>(std::count_if(Fields.begin(), Fields.end(),
		    [](const auto& Field)
		{
			return !Field.bIsStatic;
		}));
		std::size_t Index = TargetFields().size() - OwnCount;
		for (const auto& Field : Fields)
		{
			if (Field.bIsStatic)
			{
				continue;
			}
			if (Field.DefaultValue)
			{
				// Initializer compilation can grow either descriptor table. Retain indices,
				// and keep declaration metadata out of the temporary compilation result.
				FBytecodeFieldDesc Default;
				Default.TypeName = TargetFields()[Index].TypeName;
				if (!CompileFieldDefault(Field, Default)) return false;
				auto& Target = TargetFields()[Index];
				Target.DefaultValue = Default.DefaultValue;
				Target.bHasDefault = Default.bHasDefault;
				Target.InitCode = std::move(Default.InitCode);
				Target.InitLocalCount = Default.InitLocalCount;
				Target.bHasInitCode = Default.bHasInitCode;
			}
			++Index;
		}
		return true;
	}
}
