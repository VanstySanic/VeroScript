// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptHostBindingCatalog.h"

#include <algorithm>
#include <limits>

namespace VeroScript
{
	bool VeroScriptCompiler::PreferOperatorReferenceOverload(
	    const std::vector<std::string>& Arguments, const std::vector<std::string>& Left,
	    const std::vector<std::string>& Right) const
	{
		if (Arguments.size() > Left.size() || Arguments.size() > Right.size()) return false;
		bool bBetter = false;
		for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
		{
			if (Left[Index] == Right[Index])
			{
				continue;
			}
			if (Arguments[Index].empty())
			{
				continue;
			}
			int LeftScore = 0;
			int RightScore = 0;
			if (!TryScoreArgParamTypes(Arguments[Index], Left[Index], LeftScore) || !TryScoreArgParamTypes(Arguments[Index], Right[Index], RightScore) || LeftScore < RightScore)
			{
				return false;
			}
			if (LeftScore > RightScore)
			{
				bBetter = true;
				continue;
			}
			const bool bLeftRef = Runtime::IsReferenceTypeName(Left[Index]);
			const bool bRightRef = Runtime::IsReferenceTypeName(Right[Index]);
			const bool bOperatorArgument = Arguments[Index].rfind("lvalue ", 0) == 0 || Arguments[Index].rfind("const lvalue ", 0) == 0 || Arguments[Index].rfind("rvalue ", 0) == 0;
			if (bOperatorArgument && bLeftRef != bRightRef && Runtime::StripReferenceTypeName(Left[Index]) == Runtime::StripReferenceTypeName(Right[Index]))
			{
				// An indistinguishable value/reference conversion on this operand
				// must not cancel a better reference binding on another operand.
				continue;
			}
			// Equal conversion ranks can still differ in reference qualification.
			if (Arguments[Index].rfind("lvalue ", 0) == 0 && bLeftRef && bRightRef && Runtime::StripReferenceTypeName(Left[Index]) == Runtime::StripReferenceTypeName(Right[Index]) && Runtime::IsConstReferenceTypeName(Left[Index]) != Runtime::IsConstReferenceTypeName(Right[Index]))
			{
				if (Runtime::IsConstReferenceTypeName(Left[Index])) return false;
				bBetter = true;
			}
		}
		return bBetter;
	}

	std::string VeroScriptCompiler::EncodeOperatorArgType(const FExpr& RawArgument) const
	{
		const FExpr& Argument = UnwrapRefOperand(RawArgument);
		std::string Type;
		if (!TryResolveExprTypeName(Argument, Type)) return {};
		if (RawArgument.Kind == EExprKind::Ref)
		{
			return EncodeRefCallArgType(Argument, std::move(Type));
		}
		if (CanBindImplicitRefArgument(Argument, false))
		{
			return (IsConstAssignTarget(Argument) ? "const lvalue " : "lvalue ") + Type;
		}
		return "rvalue " + Type;
	}

	bool VeroScriptCompiler::CompileOperatorArgument(FEmitter& Emitter,
	    const FExpr& RawArgument, const std::string& ParamType, const FSourceLocation& Location)
	{
		const FExpr& Argument = UnwrapRefOperand(RawArgument);
		const bool bReference = Runtime::IsReferenceTypeName(ParamType);
		const bool bConstReference = Runtime::IsConstReferenceTypeName(ParamType);
		const std::string ValueType(Runtime::StripReferenceTypeName(ParamType));
		if (RawArgument.Kind == EExprKind::Ref && !bReference)
		{
			ReportErrorId("diag.ref_argument_requires_ref_parameter", Location);
			return false;
		}
		if (bReference)
		{
			std::string ArgumentType;
			if (!TryResolveExprTypeName(Argument, ArgumentType)) return false;
			const std::string ArgCanonical = CanonicalTypeNameForCoercion(ArgumentType);
			const std::string ParamCanonical = CanonicalTypeNameForCoercion(ValueType);
			if ((ArgCanonical == ParamCanonical || (bConstReference && IsSubtypeOf(ArgCanonical, ParamCanonical))) && CanBindImplicitRefArgument(Argument, !bConstReference))
			{
				return CompileRefArgument(Emitter, Argument, Location, !bConstReference);
			}
			if (bConstReference && RawArgument.Kind != EExprKind::Ref && (ArgCanonical == ParamCanonical || IsSubtypeOf(ArgCanonical, ParamCanonical)))
			{
				if (!CompileExpression(Argument)) return false;
				if (NeedsPassByValueCopy(ArgumentType) && !CanTransferCoercedValue(Argument, ArgumentType))
				{
					EmitValueCopy(Emitter, ArgumentType);
				}
				const auto Slot = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
				Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
				return true;
			}
			if (!bConstReference || RawArgument.Kind == EExprKind::Ref)
			{
				ReportError("operator reference parameter requires compatible addressable storage", Location);
				return false;
			}
		}
		const std::string PreviousExpected = ExpressionExpectedType;
		ExpressionExpectedType = ValueType;
		const bool bCompiled = CompileExpression(Argument) && EmitValueCoercionForTarget(Emitter, ValueType, Argument, Location);
		ExpressionExpectedType = PreviousExpected;
		if (!bCompiled) return false;
		// Capture a by-value operand before evaluating the next operand, which
		// may mutate the original storage. References deliberately keep aliasing.
		if (!bReference && NeedsPassByValueCopy(ValueType) && !CanTransferCoercedValue(Argument, ValueType))
		{
			EmitValueCopy(Emitter, ValueType);
		}
		if (bReference)
		{
			// A converted const-reference argument owns its temporary, rather than
			// aliasing an incompatible slot or an existing by-value struct.
			if (NeedsPassByValueCopy(ValueType) && !CanTransferCoercedValue(Argument, ValueType))
			{
				EmitValueCopy(Emitter, ValueType);
			}
			const std::uint16_t Slot = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
			Emitter.EmitU16Operand(EOpcode::MakeLocalRef, Slot);
		}
		return true;
	}

	bool VeroScriptCompiler::ResolveScriptOperator(const std::string& Name, const FExpr& Receiver,
	    const std::vector<const FExpr*>& Arguments, FResolvedScriptOperator& Out,
	    bool& OutAmbiguous) const
	{
		Out = {};
		OutAmbiguous = false;
		if (!TryResolveExprTypeName(Receiver, Out.OwnerType)) return false;
		std::vector<std::string> Types;
		for (const FExpr* Arg : Arguments)
		{
			Types.push_back(EncodeOperatorArgType(*Arg));
		}
		std::unordered_map<std::string, std::string> Subst;
		std::string DeclModule;
		if (TypeSupportsOperatorOverload(Out.OwnerType) && ResolveMethodForCall(Out.OwnerType, Name, Types, Out.OwnerLinkage, Out.Method, Receiver.Location, false, false, &OutAmbiguous, IsConstAssignTarget(Receiver)))
		{
			const std::string& DeclOwner = Out.OwnerLinkage.empty() ? Out.OwnerType : Out.OwnerLinkage;
			TryBuildGenericOwnerSubst(DeclOwner, Subst, Receiver.Location);
			DeclModule = ResolveOwnerModulePath(DeclOwner);
			Out.ParamTypes = CanonicalMethodParamTypeNames(*Out.Method, DeclModule, Subst);
			Out.ReturnType = Out.Method->ReturnType;
		}
		else
		{
			if (OutAmbiguous) return false;
			Types.insert(Types.begin(), EncodeOperatorArgType(Receiver));
			if (!ResolveFreeOperatorOverload(Name, Types, Out.Function, Out.GenericTypes,
			        &OutAmbiguous, false))
			{
				return false;
			}
			DeclModule = Out.Function->ModulePath;
			for (std::size_t Index = 0; Index < Out.GenericTypes.size(); ++Index)
			{
				Subst.emplace(TemplateParameterNames(*Out.Function)[Index], Out.GenericTypes[Index]);
			}
			Out.ParamTypes = SpecializedFreeFunctionParamTypes(*Out.Function, Out.GenericTypes);
			Out.ReturnType = Out.Function->ReturnType;
		}
		Out.ReturnType = Out.ReturnType.empty() ? "void" : QualifyTypeName(NormalizeTypeAlias(SubstituteTypeNameWith(Out.ReturnType, Subst), DeclModule), DeclModule);
		return true;
	}

	bool VeroScriptCompiler::CompileScriptOperator(FEmitter& Emitter, const std::string& Name,
	    const FExpr& Receiver, const std::vector<const FExpr*>& Arguments,
	    const FResolvedScriptOperator& Resolved, const FSourceLocation& Location)
	{
		const auto& Parameters = Resolved.Method ? Resolved.Method->Parameters : Resolved.Function->Parameters;
		if (Parameters.size() > std::numeric_limits<std::uint8_t>::max())
		{
			ReportError("operator call supports at most 255 parameters", Location);
			return false;
		}
		std::vector<const FExpr*> Operands = Arguments;
		std::unordered_map<std::string, std::string> Subst;
		const std::string& DeclOwner = Resolved.OwnerLinkage.empty() ? Resolved.OwnerType : Resolved.OwnerLinkage;
		const std::string DeclModule = Resolved.Method
		    ? ResolveOwnerModulePath(DeclOwner)
		    : Resolved.Function->ModulePath;
		if (Resolved.Method)
		{
			if (!ValidateMemberAccess(Location, Resolved.OwnerType, Name, true,
			        Arguments.size(), Resolved.Method))
			{
				return false;
			}
			if (!CompileBorrowedReceiver(Emitter, Receiver)) return false;
			TryBuildGenericOwnerSubst(DeclOwner, Subst, Location);
		}
		else
		{
			Operands.insert(Operands.begin(), &Receiver);
			for (std::size_t Index = 0; Index < Resolved.GenericTypes.size(); ++Index)
			{
				Subst.emplace(TemplateParameterNames(*Resolved.Function)[Index], Resolved.GenericTypes[Index]);
			}
		}
		const std::size_t FixedCount = FixedParamCount(Parameters);
		for (std::size_t Index = 0; Index < (std::min)(FixedCount, Operands.size()); ++Index)
		{
			if (!CompileOperatorArgument(Emitter, *Operands[Index], Resolved.ParamTypes[Index], Location)) return false;
		}
		if (IsVariadicParamList(Parameters) && Operands.size() > FixedCount)
		{
			if (!EmitVariadicRestArgument(Emitter, Parameters, FExprArgumentView(&Operands),
			        FixedCount, DeclModule, Subst, Location))
			{
				return false;
			}
		}
		else if (!EmitTrailingDefaultArguments(Emitter, Parameters, Operands.size(), DeclModule, Subst, Location))
		{
			return false;
		}
		const auto Count = static_cast<std::uint8_t>(Parameters.size());
		return Resolved.Method
		    ? EmitMemberOperatorCall(Emitter, Resolved.OwnerType, Name, *Resolved.Method,
		          Resolved.OwnerLinkage, Count, Location, true)
		    : EmitFreeOperatorCall(Emitter, *Resolved.Function, Count, Location,
		          Resolved.GenericTypes.empty() ? nullptr : &Resolved.GenericTypes, true);
	}

	bool VeroScriptCompiler::TryCompileOverloadedAssignment(FEmitter& Emitter,
	    const FAssignExpr& Assign, const FSourceLocation& Location)
	{
		if (!Assign.Target || !Assign.Value) return false;
		std::string Type;
		if (!TryResolveExprTypeName(*Assign.Target, Type) || !TypeSupportsOperatorOverload(Type)) return false;
		if (!Assign.bIsCompound && Assign.Target->Kind == EExprKind::IndexAccess)
		{
			std::string Container;
			if (TryResolveExprTypeName(*static_cast<const FIndexAccessExpr&>(*Assign.Target).Object, Container) && (Runtime::IsMapTypeName(Container) || Runtime::IsOrderedMapTypeName(Container)))
			{
				// SetIndex inserts or assigns at run time. Keep the ordinary lexical
				// copy/access checks without reading a potentially missing entry.
				CopyRequirements.push_back({Type, CurrentFunction ? CurrentFunction->Name : CurrentOwnerTypeLinkage.empty() ? ""
				                                                                                                            : CurrentOwnerTypeLinkage + "::@initializer",
				    Location, false, CurrentAccessFunctionLinkage});
				const FMethodDecl* Assignment = nullptr;
				std::string AssignmentOwner;
				bool Ambiguous = false;
				// The stored value is coerced to Type before SetIndex. Its checked
				// assignment service receives a const Type lvalue, just like this query.
				if (ResolveMethodForCall(Type, "operator=", {"const lvalue " + Type}, AssignmentOwner,
				        Assignment, Location, false, false, &Ambiguous, false))
				{
					if (Assignment->bDeleted)
					{
						ReportErrorId("diag.use_of_deleted_declaration", Location, {"operator="});
					}
					else
					{
						ValidateMemberAccess(Location, Type, "operator=", true, 1, Assignment);
					}
				}
				else if (Ambiguous && !HasError())
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {"operator="});
				}
				return false;
			}
		}
		const char* Name = Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec
		    ? (Assign.CompoundOp == EBinaryOp::Add ? "operator++" : "operator--")
		    : (Assign.bIsCompound ? CompoundOperatorMethodName(Assign.CompoundOp) : "operator=");
		if (!Name) return false;
		FIntegerLiteralExpr PostfixTag(0);
		std::vector<const FExpr*> Args;
		if (Assign.bFromPostfixIncDec)
		{
			Args.push_back(&PostfixTag);
		}
		else if (!Assign.bFromPrefixIncDec)
		{
			Args.push_back(Assign.Value.get());
		}
		FResolvedScriptOperator Resolved;
		bool bAmbiguous = false;
		if (ResolveScriptOperator(Name, *Assign.Target, Args, Resolved, bAmbiguous))
		{
			return CompileScriptOperator(Emitter, Name, *Assign.Target, Args, Resolved, Location);
		}
		if (bAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {Name});
		}
		if (HasError()) return false;
		if (!Assign.bIsCompound && !Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec && IsStructType(Type) && !(ActiveBindings && ActiveBindings->FindType(Type)))
		{
			std::string SourceType;
			if (TryResolveExprTypeName(*Assign.Value, SourceType) && CanonicalTypeNameForCoercion(Type) == CanonicalTypeNameForCoercion(SourceType))
			{
				return CompileMemberwiseAssignment(Emitter, *Assign.Target, *Assign.Value, Type, Location);
			}
		}
		if (Assign.bIsCompound && !Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec && !IsConstAssignTarget(*Assign.Target))
		{
			// Preserve the language's a += b -> a = a + b extension, with the
			// same reference binding as a direct binary expression. Bind the
			// target once so indexing and receiver side effects are not repeated.
			const char* BinaryName = BinaryOperatorMethodName(Assign.CompoundOp);
			if (BinaryName && ResolveScriptOperator(BinaryName, *Assign.Target, Args, Resolved, bAmbiguous))
			{
				const std::string ResultType(Runtime::StripReferenceTypeName(Resolved.ReturnType));
				if (ResultType == "void" || !CanAssignBetweenTypes(Type, ResultType, false))
				{
					ReportError("binary operator result cannot be assigned to compound target", Location);
					return false;
				}
				if (!CompileRefArgument(Emitter, *Assign.Target, Location, true)) return false;
				const auto Slot = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::BindLocal, Slot);
				Emitter.EmitU16Operand(!Resolved.Method && Runtime::IsReferenceTypeName(Resolved.ParamTypes.front())
				        ? EOpcode::MakeLocalRef
				        : EOpcode::LoadLocal,
				    Slot);
				if (!Resolved.Method && !Runtime::IsReferenceTypeName(Resolved.ParamTypes.front()) && !EmitValueCoercionBetweenTypes(Emitter, Resolved.ParamTypes.front(), Type, Location))
				{
					return false;
				}
				if (!Resolved.Method && NeedsPassByValueCopy(Resolved.ParamTypes.front()))
				{
					EmitValueCopy(Emitter, Resolved.ParamTypes.front());
				}
				if (!CompileOperatorArgument(Emitter, *Assign.Value, Resolved.ParamTypes.back(), Location)) return false;
				const bool bEmitted = Resolved.Method
				    ? EmitMemberOperatorCall(Emitter, Type, BinaryName, *Resolved.Method, Resolved.OwnerLinkage, 1, Location, true)
				    : EmitFreeOperatorCall(Emitter, *Resolved.Function, 2, Location,
				          Resolved.GenericTypes.empty() ? nullptr : &Resolved.GenericTypes, true);
				if (!bEmitted) return false;
				if (Runtime::IsReferenceTypeName(Resolved.ReturnType))
				{
					Emitter.Emit(EOpcode::DerefRef);
				}
				if (!EmitValueCoercionBetweenTypes(Emitter, Type, ResultType, Location, false)) return false;
				if (NeedsPassByValueCopy(Type))
				{
					EmitValueCopy(Emitter, Type);
				}
				Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
				Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
				return true;
			}
			if (bAmbiguous)
			{
				ReportErrorId("diag.ambiguous_overload_for_method", Location, {BinaryName});
			}
		}
		// Keep the established struct postfix / compound-binary fallback when no
		// corresponding declaration exists. An explicit overload always wins.
		return false;
	}

	bool VeroScriptCompiler::TryResolveOverloadedAssignmentType(const FAssignExpr& Assign,
	    std::string& OutTypeName) const
	{
		if (!Assign.Target || !Assign.Value) return false;
		std::string Type;
		if (!TryResolveExprTypeName(*Assign.Target, Type) || !TypeSupportsOperatorOverload(Type)) return false;
		if (!Assign.bIsCompound && Assign.Target->Kind == EExprKind::IndexAccess)
		{
			std::string Container;
			if (TryResolveExprTypeName(*static_cast<const FIndexAccessExpr&>(*Assign.Target).Object, Container) && (Runtime::IsMapTypeName(Container) || Runtime::IsOrderedMapTypeName(Container)))
			{
				return false;
			}
		}
		const char* Name = Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec
		    ? (Assign.CompoundOp == EBinaryOp::Add ? "operator++" : "operator--")
		    : (Assign.bIsCompound ? CompoundOperatorMethodName(Assign.CompoundOp) : "operator=");
		if (!Name) return false;
		FIntegerLiteralExpr PostfixTag(0);
		std::vector<const FExpr*> Args;
		if (Assign.bFromPostfixIncDec)
		{
			Args.push_back(&PostfixTag);
		}
		else if (!Assign.bFromPrefixIncDec)
		{
			Args.push_back(Assign.Value.get());
		}
		FResolvedScriptOperator Resolved;
		bool bAmbiguous = false;
		if (!ResolveScriptOperator(Name, *Assign.Target, Args, Resolved, bAmbiguous))
		{
			std::string Source;
			if (!Assign.bIsCompound && !bAmbiguous && IsStructType(Type) && !(ActiveBindings && ActiveBindings->FindType(Type)) && TryResolveExprTypeName(*Assign.Value, Source) && CanonicalTypeNameForCoercion(Type) == CanonicalTypeNameForCoercion(Source))
			{
				OutTypeName = "ref " + Type;
				return true;
			}
			return false;
		}
		OutTypeName = Resolved.ReturnType;
		return true;
	}
}
