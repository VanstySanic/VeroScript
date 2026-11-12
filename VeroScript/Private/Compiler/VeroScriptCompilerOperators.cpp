// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

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
	bool VeroScriptCompiler::TryCompileOverloadedBinary(
		FEmitter& Emitter,
		const FBinaryExpr& Binary,
		const FSourceLocation& Location)
	{
		const char* const MethodName = BinaryOperatorMethodName(Binary.Op);
		if (!MethodName || !Binary.Left || !Binary.Right)
		{
			return false;
		}
		std::string LeftType;
		std::string RightType;
		const bool bLeftKnown = TryResolveExprTypeName(*Binary.Left, LeftType);
		const bool bRightKnown = TryResolveExprTypeName(*Binary.Right, RightType);
		if (!bLeftKnown)
		{
			LeftType.clear();
		}
		if (!bRightKnown)
		{
			RightType.clear();
		}
		const bool bRequireOverload =
			Binary.Op == EBinaryOp::Add
				|| Binary.Op == EBinaryOp::Subtract
				|| Binary.Op == EBinaryOp::Multiply
				|| Binary.Op == EBinaryOp::Divide
				|| Binary.Op == EBinaryOp::Modulo
				|| Binary.Op == EBinaryOp::Less
				|| Binary.Op == EBinaryOp::LessEqual
				|| Binary.Op == EBinaryOp::Greater
				|| Binary.Op == EBinaryOp::GreaterEqual
				|| Binary.Op == EBinaryOp::BitAnd
				|| Binary.Op == EBinaryOp::BitOr
				|| Binary.Op == EBinaryOp::BitXor
				|| Binary.Op == EBinaryOp::ShiftLeft
				|| Binary.Op == EBinaryOp::ShiftRight
				|| Binary.Op == EBinaryOp::LogicalAnd
				|| Binary.Op == EBinaryOp::LogicalOr;

		const auto ReportNoMatch = [&]()
		{
			ReportError(
				std::string("no matching ") + MethodName
					+ " for operands "
					+ DisplayTypeName(LeftType.empty() ? "?" : LeftType)
					+ " and "
					+ DisplayTypeName(RightType.empty() ? "?" : RightType),
				Location);
		};

		// Do not soft-match overloads when an operand type is unknown.
		if (!bLeftKnown || !bRightKnown)
		{
			if (bRequireOverload
				&& (TypeSupportsOperatorOverload(LeftType)
					|| TypeSupportsOperatorOverload(RightType)))
			{
				ReportNoMatch();
				return false;
			}
			return false;
		}

		// Emit Left.op(Right): stack [L, R] after LHS→RHS evaluation.
		if (LeftType == "bool" && RightType == "bool"
			&& (Binary.Op == EBinaryOp::LogicalAnd
				|| Binary.Op == EBinaryOp::LogicalOr))
		{
			// Primitive bool &&/|| is never a user-defined operator candidate.
			// Returning here preserves the native short-circuit emission path.
			return false;
		}

		// A user-defined operator must have at least one overloadable operand.
		// Besides matching C++'s operator model, this keeps an exact built-in
		// operation (for example int + int) from being displaced by a free
		// operator that would first require a user-defined conversion such as
		// int -> Vec. Built-in emission below is both the better overload and the
		// only non-recursive choice inside that free operator's own body.
		if (!TypeSupportsOperatorOverload(LeftType)
			&& !TypeSupportsOperatorOverload(RightType))
		{
			return false;
		}

		const std::string LeftArgType = EncodeOperatorArgType(*Binary.Left);
		const std::string RightArgType = EncodeOperatorArgType(*Binary.Right);
		const auto EmitLeftMember = [&](const char* Name, const FMethodDecl& MethodDecl,
			const std::string& OwnerLinkage, bool bNegate) -> bool
		{
			std::unordered_map<std::string, std::string> Subst;
			const std::string& DeclOwner = OwnerLinkage.empty() ? LeftType : OwnerLinkage;
			TryBuildGenericOwnerSubst(DeclOwner, Subst, Location);
			const auto Params = CanonicalMethodParamTypeNames(MethodDecl, ResolveOwnerModulePath(DeclOwner), Subst);
			if (!CompileExpression(*Binary.Left) || !CompileOperatorArgument(Emitter, *Binary.Right, Params.front(), Location) || !EmitMemberOperatorCall(Emitter, LeftType, Name, MethodDecl, OwnerLinkage, 1, Location, true))
			{
				return false;
			}
			if (bNegate)
			{
				Emitter.Emit(EOpcode::Not);
			}
			return true;
		};
		const auto EmitRightMember = [&](const char* Name, const FMethodDecl& MethodDecl,
			const std::string& OwnerLinkage, bool bNegate) -> bool
		{
			std::unordered_map<std::string, std::string> Subst;
			const std::string& DeclOwner = OwnerLinkage.empty() ? RightType : OwnerLinkage;
			TryBuildGenericOwnerSubst(DeclOwner, Subst, Location);
			const auto Params = CanonicalMethodParamTypeNames(MethodDecl, ResolveOwnerModulePath(DeclOwner), Subst);
			const bool bRef = Runtime::IsReferenceTypeName(Params.front());
			if (!CompileOperatorArgument(Emitter, *Binary.Left, Params.front(), Location)) return false;
			const auto Temp = AllocateAssignTemp();
			Emitter.EmitU16Operand(bRef ? EOpcode::BindLocal : EOpcode::StoreLocal, Temp);
			if (!CompileExpression(*Binary.Right)) return false;
			Emitter.EmitU16Operand(bRef ? EOpcode::MakeLocalRef : EOpcode::LoadLocal, Temp);
			if (!EmitMemberOperatorCall(Emitter, RightType, Name, MethodDecl, OwnerLinkage, 1, Location, true)) return false;
			if (bNegate)
			{
				Emitter.Emit(EOpcode::Not);
			}
			return true;
		};
		const auto EmitFreeArgs = [&](const FFunctionDecl& Op, const std::string&,
			const std::string&, const FExpr& First, const FExpr& Second,
			bool bNegate, const std::vector<std::string>& GenericTypes) -> bool
		{
			const auto Params = SpecializedFreeFunctionParamTypes(Op, GenericTypes);
			if (!CompileOperatorArgument(Emitter, First, Params[0], Location) || !CompileOperatorArgument(Emitter, Second, Params[1], Location) || !EmitFreeOperatorCall(Emitter, Op, 2, Location, GenericTypes.empty() ? nullptr : &GenericTypes, true))
			{
				return false;
			}
			if (bNegate)
			{
				Emitter.Emit(EOpcode::Not);
			}
			return true;
		};
		const auto EmitFreeSwapped = [&](const FFunctionDecl& Op, bool bNegate,
			const std::vector<std::string>& GenericTypes) -> bool
		{
			const auto Params = SpecializedFreeFunctionParamTypes(Op, GenericTypes);
			const bool bRef = Runtime::IsReferenceTypeName(Params[1]);
			if (!CompileOperatorArgument(Emitter, *Binary.Left, Params[1], Location)) return false;
			const auto Temp = AllocateAssignTemp();
			Emitter.EmitU16Operand(bRef ? EOpcode::BindLocal : EOpcode::StoreLocal, Temp);
			if (!CompileOperatorArgument(Emitter, *Binary.Right, Params[0], Location)) return false;
			Emitter.EmitU16Operand(bRef ? EOpcode::MakeLocalRef : EOpcode::LoadLocal, Temp);
			if (!EmitFreeOperatorCall(Emitter, Op, 2, Location, GenericTypes.empty() ? nullptr : &GenericTypes, true)) return false;
			if (bNegate)
			{
				Emitter.Emit(EOpcode::Not);
			}
			return true;
		};

		std::string OwnerLinkage;
		const FMethodDecl* MethodDecl = nullptr;
		bool bMemberAmbiguous = false;
		std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
		if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
				LeftType, MethodName, {RightType}, bMemberAmbiguous, &HostNativeIndex,
				IsConstAssignTarget(*Binary.Left)))
		{
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType.clear();
			const bool bCompiled = CompileExpression(*Binary.Left)
				&& CompileExpression(*Binary.Right);
			ExpressionExpectedType = PreviousExpectedType;
			return bCompiled && EmitHostOperatorCall(
				Emitter, *HostOperator, HostNativeIndex, {RightType}, Location,
				IsConstAssignTarget(*Binary.Right));
		}
		if (bMemberAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (TypeSupportsOperatorOverload(LeftType)
			&& ResolveMethodForCall(
				LeftType,
				MethodName,
				{RightArgType},
				OwnerLinkage,
				MethodDecl,
				Location,
				false,
				false,
					&bMemberAmbiguous, IsConstAssignTarget(*Binary.Left))
					&& MethodDecl)
		{
			return EmitLeftMember(MethodName, *MethodDecl, OwnerLinkage, false);
		}
		if (bMemberAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (HasError())
		{
			return false;
		}

		// Right-member: int + Vec via Vec.operator+(int) with swapped stack [R, L].
		std::string RightOwnerLinkage;
		const FMethodDecl* RightMethodDecl = nullptr;
		bool bRightMemberAmbiguous = false;
		std::uint16_t RightHostNativeIndex = static_cast<std::uint16_t>(-1);
		if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
				RightType, MethodName, {LeftType}, bRightMemberAmbiguous,
				&RightHostNativeIndex, IsConstAssignTarget(*Binary.Right)))
		{
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType.clear();
			if (!CompileExpression(*Binary.Left))
			{
				ExpressionExpectedType = PreviousExpectedType;
				return false;
			}
			const std::uint16_t LeftTemp = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::StoreLocal, LeftTemp);
			if (!CompileExpression(*Binary.Right))
			{
				ExpressionExpectedType = PreviousExpectedType;
				return false;
			}
			ExpressionExpectedType = PreviousExpectedType;
			Emitter.EmitU16Operand(EOpcode::LoadLocal, LeftTemp);
			return EmitHostOperatorCall(
				Emitter, *HostOperator, RightHostNativeIndex, {LeftType}, Location,
				IsConstAssignTarget(*Binary.Left));
		}
		if (bRightMemberAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (TypeSupportsOperatorOverload(RightType)
			&& ResolveMethodForCall(
				RightType,
				MethodName,
				{LeftArgType},
				RightOwnerLinkage,
				RightMethodDecl,
				Location,
				false,
				false,
					&bRightMemberAmbiguous, IsConstAssignTarget(*Binary.Right))
					&& RightMethodDecl)
		{
			return EmitRightMember(MethodName, *RightMethodDecl, RightOwnerLinkage, false);
		}
		if (bRightMemberAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (HasError())
		{
			return false;
		}

		// Free-function operator (enables right-side cases like int + Vec).
		bool bAmbiguous = false;
		const FFunctionDecl* FreeOp = nullptr;
		std::vector<std::string> GenericConcreteTypes;
		if (ResolveFreeOperatorOverload(
				MethodName,
				{LeftArgType, RightArgType},
				FreeOp,
				GenericConcreteTypes,
					&bAmbiguous,
				false))
		{
			return EmitFreeArgs(
				*FreeOp,
				LeftType,
				RightType,
					*Binary.Left,
					*Binary.Right,
				false,
				GenericConcreteTypes);
		}
		if (bAmbiguous)
		{
			ReportErrorId("diag.ambiguous_call_to", Location, {MethodName});
			return false;
		}

		// Synthesize != as !(a == b) when operator== is overloaded and != is not.
		if (Binary.Op == EBinaryOp::NotEqual)
		{
			const char* const EqName = "operator==";
			std::string EqOwnerLinkage;
			const FMethodDecl* EqMethod = nullptr;
			bool bEqAmbiguousMember = false;
			if (TypeSupportsOperatorOverload(LeftType)
				&& ResolveMethodForCall(
					LeftType,
					EqName,
					{RightArgType},
					EqOwnerLinkage,
					EqMethod,
					Location,
					false,
					false,
						&bEqAmbiguousMember, IsConstAssignTarget(*Binary.Left))
						&& EqMethod)
			{
				return EmitLeftMember(EqName, *EqMethod, EqOwnerLinkage, true);
			}
			if (bEqAmbiguousMember)
			{
				ReportErrorId("diag.ambiguous_overload_for_method", Location, {EqName});
				return false;
			}
			if (HasError())
			{
				return false;
			}
			std::string EqRightOwner;
			const FMethodDecl* EqRightMethod = nullptr;
			bool bEqRightAmbiguous = false;
			if (TypeSupportsOperatorOverload(RightType)
				&& ResolveMethodForCall(
					RightType,
					EqName,
					{LeftArgType},
					EqRightOwner,
					EqRightMethod,
					Location,
					false,
					false,
						&bEqRightAmbiguous, IsConstAssignTarget(*Binary.Right))
						&& EqRightMethod)
			{
				return EmitRightMember(EqName, *EqRightMethod, EqRightOwner, true);
			}
			if (bEqRightAmbiguous)
			{
				ReportErrorId("diag.ambiguous_overload_for_method", Location, {EqName});
				return false;
			}
			if (HasError())
			{
				return false;
			}
			bool bEqAmbiguous = false;
			const FFunctionDecl* FreeEq = nullptr;
			std::vector<std::string> EqGenericConcreteTypes;
			if (ResolveFreeOperatorOverload(
					EqName,
					{LeftArgType, RightArgType},
					FreeEq,
					EqGenericConcreteTypes,
						&bEqAmbiguous,
					false))
			{
				return EmitFreeArgs(
					*FreeEq,
					LeftType,
					RightType,
						*Binary.Left,
						*Binary.Right,
					true,
					EqGenericConcreteTypes);
			}
			if (bEqAmbiguous)
			{
				ReportErrorId("diag.ambiguous_call_to", Location, {EqName});
				return false;
			}
		}

		// Relational mirrors: a > b via b < a (swapped operands / right-member).
		if (const std::optional<EBinaryOp> MirrorOp = MakeMirrorBinaryOp(Binary.Op))
		{
			const char* const MirrorName = BinaryOperatorMethodName(*MirrorOp);
			if (MirrorName)
			{
				std::string MirrorOwner;
				const FMethodDecl* MirrorMethod = nullptr;
				bool bMirrorAmbiguous = false;
				if (TypeSupportsOperatorOverload(RightType)
					&& ResolveMethodForCall(
						RightType,
						MirrorName,
						{LeftArgType},
						MirrorOwner,
						MirrorMethod,
						Location,
						false,
						false,
							&bMirrorAmbiguous, IsConstAssignTarget(*Binary.Right))
							&& MirrorMethod)
				{
					return EmitRightMember(MirrorName, *MirrorMethod, MirrorOwner, false);
				}
				if (bMirrorAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {MirrorName});
					return false;
				}
				if (HasError())
				{
					return false;
				}
				bool bMirrorFreeAmbiguous = false;
				const FFunctionDecl* FreeMirror = nullptr;
				std::vector<std::string> MirrorGenericConcreteTypes;
				if (ResolveFreeOperatorOverload(
						MirrorName,
						{RightArgType, LeftArgType},
						FreeMirror,
						MirrorGenericConcreteTypes,
							&bMirrorFreeAmbiguous,
						false))
				{
					return EmitFreeSwapped(*FreeMirror, false, MirrorGenericConcreteTypes);
				}
				if (bMirrorFreeAmbiguous)
				{
					ReportErrorId("diag.ambiguous_call_to", Location, {MirrorName});
					return false;
				}
			}
		}

		// A missing user operator can fall back to the ordinary contextual
		// bool conversion. The caller emits the original short-circuit order.
		if ((Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr)
			&& CanAssignBetweenTypes("bool", LeftType) && CanAssignBetweenTypes("bool", RightType))
		{
			return false;
		}
		if (bRequireOverload
			&& (TypeSupportsOperatorOverload(LeftType) || TypeSupportsOperatorOverload(RightType)))
		{
			ReportNoMatch();
			return false;
		}
		// == / != without overload: soft-miss to builtin Eq for same-kind operands
		// (two user types → identity; scalars → value Eq). Reject mixed user-type vs
		// primitive, but allow `T == nullptr` / `nullptr == T` for class/interface handles.
		if ((Binary.Op == EBinaryOp::Equal || Binary.Op == EBinaryOp::NotEqual)
			&& (TypeSupportsOperatorOverload(LeftType) != TypeSupportsOperatorOverload(RightType))
			&& LeftType != "nullptr"
			&& RightType != "nullptr")
		{
			ReportNoMatch();
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::TryCompileOverloadedUnary(
		FEmitter& Emitter,
		const FUnaryExpr& Unary,
		const FSourceLocation& Location)
	{
		const char* const MethodName = UnaryOperatorMethodName(Unary.Op);
		if (!MethodName || !Unary.Operand)
		{
			return false;
		}
		std::string OperandType;
		if (!TryResolveExprTypeName(*Unary.Operand, OperandType))
		{
			// Unknown operand type: do not soft-match an arity-only operator overload.
			return false;
		}

		std::string OwnerLinkage;
		const FMethodDecl* MethodDecl = nullptr;
		bool bUnaryAmbiguous = false;
		std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
		if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
				OperandType, MethodName, {}, bUnaryAmbiguous, &HostNativeIndex,
				IsConstAssignTarget(*Unary.Operand)))
		{
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType.clear();
			const bool bCompiled = CompileExpression(*Unary.Operand);
			ExpressionExpectedType = PreviousExpectedType;
			return bCompiled && EmitHostOperatorCall(
				Emitter, *HostOperator, HostNativeIndex, {}, Location);
		}
		if (bUnaryAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (TypeSupportsOperatorOverload(OperandType)
			&& ResolveMethodForCall(
				OperandType,
				MethodName, {},
				OwnerLinkage,
				MethodDecl,
				Location,
				false,
				false,
					&bUnaryAmbiguous, IsConstAssignTarget(*Unary.Operand))
					&& MethodDecl)
		{
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType.clear();
			const bool bCompiledOperand = CompileExpression(*Unary.Operand);
			ExpressionExpectedType = PreviousExpectedType;
			if (!bCompiledOperand)
			{
				return false;
			}
			return EmitMemberOperatorCall(
				Emitter,
				OperandType,
				MethodName,
					*MethodDecl,
				OwnerLinkage,
				0,
				Location);
		}
		if (bUnaryAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}
		if (HasError())
		{
			return false;
		}

		bool bAmbiguous = false;
		const FFunctionDecl* FreeOp = nullptr;
		std::vector<std::string> GenericConcreteTypes;
		if (ResolveFreeOperatorOverload(
				MethodName,
				{EncodeOperatorArgType(*Unary.Operand)},
				FreeOp,
				GenericConcreteTypes,
					&bAmbiguous,
				false))
		{
			const auto Params = SpecializedFreeFunctionParamTypes(*FreeOp, GenericConcreteTypes);
			if (!CompileOperatorArgument(Emitter, *Unary.Operand, Params.front(), Location)) return false;
			const std::vector<std::string>* GenericArgs = GenericConcreteTypes.empty()
				? nullptr
				: &GenericConcreteTypes;
			return EmitFreeOperatorCall(Emitter, *FreeOp, 1, Location, GenericArgs, true);
		}
		if (bAmbiguous)
		{
			ReportErrorId("diag.ambiguous_call_to", Location, {MethodName});
			return false;
		}

		if (Unary.Op == EUnaryOp::Not && CanAssignBetweenTypes("bool", OperandType)) return false;
		if (TypeSupportsOperatorOverload(OperandType))
		{
			ReportError(
				std::string("no matching ") + MethodName + " for type "
					+ DisplayTypeName(OperandType),
				Location);
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::EmitMemberOperatorCall(
		FEmitter& Emitter,
		const std::string& OwnerTypeName,
		const std::string& MethodName,
		const FMethodDecl& MethodDecl,
		const std::string& OwnerLinkage,
		const std::uint8_t ArgumentCount,
		const FSourceLocation& Location,
		const bool bArgumentsCopied)
	{
		if (!ValidateMemberAccess(Location, OwnerTypeName, MethodName, true, ArgumentCount, &MethodDecl)) return false;
		if (MethodDecl.bDeleted)
		{
			ReportErrorId("diag.use_of_deleted_declaration", Location, {MethodName});
			return false;
		}
		std::unordered_map<std::string, std::string> OwnerSubst;
		const std::string& SignatureOwner = OwnerLinkage.empty() ? OwnerTypeName : OwnerLinkage;
		if (!TryBuildGenericOwnerSubst(SignatureOwner, OwnerSubst, Location) && HasError())
		{
			return false;
		}
		const std::string MethodTypeModulePath = ResolveOwnerModulePath(SignatureOwner);
		const std::vector<std::string> ParamTypes =
			CanonicalMethodParamTypeNames(MethodDecl, MethodTypeModulePath, OwnerSubst);
		// Stack is [receiver, arg0…argN-1]; clone each by-value arg (not the receiver).
		if (!bArgumentsCopied)
		{
			EmitClonePassByValueArgs(Emitter, ParamTypes, ArgumentCount);
		}
		const bool bOwnerIsInterface = IsInterfaceTypeName(OwnerTypeName);
		if (bOwnerIsInterface && MethodDecl.Body && !MethodDecl.bVirtual)
		{
			const std::string DeclOwner = OwnerLinkage.empty()
				? ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath)
				: OwnerLinkage;
			const std::string MethodKey =
				MakeMethodFunctionName(DeclOwner, MethodName, ParamTypes, MethodDecl.bIsConst);
			const std::uint16_t FunctionIndex = FindFunctionIndex(MethodKey);
			if (FunctionIndex != static_cast<std::uint16_t>(-1))
			{
				Emitter.EmitCallBoundMethod(FunctionIndex, ArgumentCount);
				return true;
			}
		}
		if (IsVirtualOrOverridesVirtual(SignatureOwner, MethodDecl)
			|| (bOwnerIsInterface && (!MethodDecl.Body || MethodDecl.bVirtual)))
		{
			Emitter.EmitCallMethod(
				AddString(MakeMethodCallLeafName(
						MethodName,
						MethodDecl,
						MethodTypeModulePath,
						OwnerSubst)),
				ArgumentCount);
			return true;
		}
		const std::string DeclOwner = OwnerLinkage.empty()
			? ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath)
			: OwnerLinkage;
		const std::string MethodKey = MakeMethodFunctionName(DeclOwner, MethodName, ParamTypes, MethodDecl.bIsConst);
		const std::uint16_t FunctionIndex = FindFunctionIndex(MethodKey);
		if (FunctionIndex != static_cast<std::uint16_t>(-1))
		{
			Emitter.EmitCallBoundMethod(FunctionIndex, ArgumentCount);
			return true;
		}
		Emitter.EmitCallMethod(
			AddString(MakeMethodCallLeafName(
					MethodName,
					MethodDecl,
					MethodTypeModulePath,
					OwnerSubst)),
			ArgumentCount);
		return true;
	}

	bool VeroScriptCompiler::EmitFreeOperatorCall(
		FEmitter& Emitter,
		const FFunctionDecl& Function,
		const std::uint8_t ArgumentCount,
		const FSourceLocation& Location,
		const std::vector<std::string>* GenericConcreteTypes,
		const bool bArgumentsCopied)
	{
		if (Function.bDeleted)
		{
			ReportErrorId(
				"diag.use_of_deleted_declaration",
				Location,
				{TypeNameLeaf(Function.Name)});
			return false;
		}
		std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
		if (!TemplateParameterNames(Function).empty())
		{
			if (!GenericConcreteTypes
				|| GenericConcreteTypes->size() != TemplateParameterNames(Function).size())
			{
				ReportError(
					"internal error: missing generic instantiation for "
						+ TypeNameLeaf(Function.Name),
					Location);
				return false;
			}
			if (!EnsureGenericInstantiation(
					Function,
						*GenericConcreteTypes,
					FunctionIndex,
					Location))
			{
				return false;
			}
		}
		else
		{
			const std::string LinkageName = MakeFreeFunctionLinkageName(Function);
			FunctionIndex = FindFunctionIndex(LinkageName);
			if (FunctionIndex == static_cast<std::uint16_t>(-1))
			{
				ReportError(
					"free-function operator not found: " + TypeNameLeaf(Function.Name),
					Location);
				return false;
			}
		}
		const std::vector<std::string> ParamTypes = GenericConcreteTypes
			? SpecializedFreeFunctionParamTypes(Function, *GenericConcreteTypes)
			: CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, {});
		// Stack is [arg0…argN-1] (free ops: typically receiver-as-first-arg included).
		if (!bArgumentsCopied)
		{
			EmitClonePassByValueArgs(Emitter, ParamTypes, ArgumentCount);
		}
		Emitter.EmitCall(FunctionIndex, ArgumentCount);
		return true;
	}

	void VeroScriptCompiler::EmitClonePassByValueArgs(
		FEmitter& Emitter,
		const std::vector<std::string>& ParamTypes,
		const std::uint8_t ArgumentCount)
	{
		if (ArgumentCount == 0 || ParamTypes.empty())
		{
			return;
		}
		bool bAnyNeedsCopy = false;
		const std::uint8_t CloneCount = static_cast<std::uint8_t>(
			std::min<std::size_t>(ArgumentCount, ParamTypes.size()));
		for (std::uint8_t Index = 0; Index < CloneCount; ++Index)
		{
			if (NeedsPassByValueCopy(ParamTypes[Index]))
			{
				bAnyNeedsCopy = true;
				break;
			}
		}
		if (!bAnyNeedsCopy)
		{
			return;
		}
		// Single-arg fast path: TOS is the only argument.
		if (ArgumentCount == 1)
		{
			if (NeedsPassByValueCopy(ParamTypes[0]))
			{
				EmitValueCopy(Emitter, ParamTypes[0]);
			}
			return;
		}
		// Park args right-to-left, then reload left-to-right cloning as needed.
		// Leaves anything below the arg block (e.g. member receiver) untouched.
		std::vector<std::uint16_t> Temps(ArgumentCount);
		for (std::uint8_t FromRight = 0; FromRight < ArgumentCount; ++FromRight)
		{
			const std::uint8_t Index =
				static_cast<std::uint8_t>(ArgumentCount - 1 - FromRight);
			Temps[Index] = AllocateAssignTemp();
			const bool bRef = Index < ParamTypes.size() && Runtime::IsReferenceTypeName(ParamTypes[Index]);
			Emitter.EmitU16Operand(bRef ? EOpcode::BindLocal : EOpcode::StoreLocal, Temps[Index]);
		}
		for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const bool bRef = Index < ParamTypes.size() && Runtime::IsReferenceTypeName(ParamTypes[Index]);
			Emitter.EmitU16Operand(bRef ? EOpcode::MakeLocalRef : EOpcode::LoadLocal, Temps[Index]);
			if (Index < ParamTypes.size() && NeedsPassByValueCopy(ParamTypes[Index]))
			{
				EmitValueCopy(Emitter, ParamTypes[Index]);
			}
		}
	}

	bool VeroScriptCompiler::TryEmitOverloadedCompoundOp(
		FEmitter& Emitter,
		const EBinaryOp Op,
		const std::string& LeftType,
		const std::string& RightType,
		const FSourceLocation& Location,
		std::string* OutResultTypeName,
		const bool bRightIsConst)
	{
		const char* const BinaryMethodName = BinaryOperatorMethodName(Op);
		const char* const CompoundMethodName = CompoundOperatorMethodName(Op);
		if (!BinaryMethodName)
		{
			return false;
		}
		if (OutResultTypeName)
		{
			OutResultTypeName->clear();
		}
		const auto SetResultType = [&](const std::string& ResultType)
		{
			if (OutResultTypeName)
			{
				*OutResultTypeName = ResultType;
			}
		};
		// Compound methods (operator+=): void return is in-place success.
		// Binary (+ etc.): void remains a soft miss so compound can try other candidates.
		const auto ResultUsableForStore =
			[&](const std::string& ResultType, const bool bCompoundMethod) -> bool
			{
				if (ResultType.empty())
				{
					return false;
				}
				if (ResultType == "void")
				{
					return bCompoundMethod;
				}
				// The fallback `a = a + b` must not insert a second user-defined
				// conversion for the operator result. Otherwise an unrelated
				// converting constructor on the left type can make an invalid
				// operator+ appear usable as operator+=.
				return CanAssignBetweenTypes(LeftType, ResultType, false);
			};
		// -1 = soft miss (try next), 0 = hard failure, 1 = emitted.
		const auto TryMember =
			[&](const char* const MethodName, const bool bCompoundMethod) -> int
			{
				if (!MethodName || !TypeSupportsOperatorOverload(LeftType))
				{
					return -1;
				}
				bool bHostAmbiguous = false;
				std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
				if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
							LeftType,
							MethodName,
						{RightType},
							bHostAmbiguous,
							&HostNativeIndex))
				{
					const std::string ResultType =
							HostOperatorReturnTypeName(*HostOperator, LeftType);
					if (!ResultUsableForStore(ResultType, bCompoundMethod)
							|| (ResultType == "void" && !HostOperator->bMutableThis))
					{
						return -1;
					}
					SetResultType(ResultType);
					return EmitHostOperatorCall(
							Emitter,
							*HostOperator,
							HostNativeIndex,
							   {RightType},
							Location, bRightIsConst)
							? 1
							: 0;
				}
				if (bHostAmbiguous)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
					return 0;
				}

				std::string OwnerLinkage;
				const FMethodDecl* MethodDecl = nullptr;
				bool bAmbiguous = false;
				if (!ResolveMethodForCall(
							LeftType,
							MethodName,
						{RightType},
							OwnerLinkage,
							MethodDecl,
							Location,
							false,
							false,
								&bAmbiguous)
								|| !MethodDecl)
				{
					if (bAmbiguous)
					{
						ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
						return 0;
					}
					return HasError() ? 0 : -1;
				}
				std::unordered_map<std::string, std::string> OwnerSubst;
				const std::string& DeclOwner = OwnerLinkage.empty() ? LeftType : OwnerLinkage;
				TryBuildGenericOwnerSubst(DeclOwner, OwnerSubst, Location);
				const std::string DeclModule = ResolveOwnerModulePath(DeclOwner);
				const std::string ReturnType = OwnerSubst.empty()
						? MethodDecl->ReturnType
						: SubstituteTypeNameWith(MethodDecl->ReturnType, OwnerSubst);
				const std::string ResultType =
						QualifyTypeName(NormalizeTypeAlias(ReturnType, DeclModule), DeclModule);
				if (!ResultUsableForStore(ResultType, bCompoundMethod))
				{
					return -1; // e.g. void operator+ / int += — fall through.
				}
				if (!MethodDecl->Parameters.empty())
				{
					const std::string ParamType = QualifyTypeName(
							NormalizeTypeAlias(
								SubstituteTypeNameWith(MethodDecl->Parameters[0].TypeName, OwnerSubst),
								DeclModule),
							DeclModule);
					if (!EmitValueCoercionBetweenTypes(Emitter, ParamType, RightType, Location, false, bRightIsConst))
					{
						return 0;
					}
				}
				SetResultType(ResultType == "void" ? "void" : ResultType);
				return EmitMemberOperatorCall(
						Emitter,
						LeftType,
						MethodName,
							*MethodDecl,
						OwnerLinkage,
						1,
						Location)
						? 1
						: 0;
			};
		const auto TryFree =
			[&](const char* const MethodName, const bool bCompoundMethod) -> int
			{
				if (!MethodName)
				{
					return -1;
				}
				bool bAmbiguous = false;
				const FFunctionDecl* FreeOp = nullptr;
				std::vector<std::string> GenericConcreteTypes;
				if (!ResolveFreeOperatorOverload(
							MethodName,
						{LeftType, RightType},
							FreeOp,
							GenericConcreteTypes,
								&bAmbiguous,
							false))
				{
					if (bAmbiguous)
					{
						ReportErrorId("diag.ambiguous_call_to", Location, {MethodName});
						return 0;
					}
					return -1;
				}
				std::string ResultType;
				if (FreeOp->ReturnType.empty())
				{
					ResultType = "void";
				}
				else if (GenericConcreteTypes.empty())
				{
					ResultType = QualifyTypeName(
							NormalizeTypeAlias(FreeOp->ReturnType, FreeOp->ModulePath),
							FreeOp->ModulePath);
				}
				else
				{
					std::unordered_map<std::string, std::string> FunctionSubst;
					for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(*FreeOp).size(); ++TypeIndex)
					{
						FunctionSubst.emplace(
								TemplateParameterNames(*FreeOp)[TypeIndex],
								GenericConcreteTypes[TypeIndex]);
					}
					ResultType = QualifyTypeName(
							NormalizeTypeAlias(
								SubstituteTypeNameWith(FreeOp->ReturnType, FunctionSubst),
								FreeOp->ModulePath),
							FreeOp->ModulePath);
				}
				if (!ResultUsableForStore(ResultType, bCompoundMethod))
				{
					return -1;
				}
				if (ResultType == "void" && bCompoundMethod && IsStructType(LeftType))
				{
					ReportError(
							"void free-function compound operator requires class left operand",
							Location);
					return 0;
				}
				if (FreeOp->Parameters.size() >= 2)
				{
					const std::vector<std::string> ParamTypes =
							SpecializedFreeFunctionParamTypes(*FreeOp, GenericConcreteTypes);
					const std::string LeftParam = QualifyTypeName(
							NormalizeTypeAlias(ParamTypes[0]),
							FreeOp->ModulePath);
					const std::string RightParam = QualifyTypeName(
							NormalizeTypeAlias(ParamTypes[1]),
							FreeOp->ModulePath);
					const std::uint16_t RightTemp = AllocateAssignTemp();
					Emitter.EmitU16Operand(EOpcode::StoreLocal, RightTemp);
					if (!EmitValueCoercionBetweenTypes(Emitter, LeftParam, LeftType, Location))
					{
						return 0;
					}
					Emitter.EmitU16Operand(EOpcode::LoadLocal, RightTemp);
					if (!EmitValueCoercionBetweenTypes(Emitter, RightParam, RightType, Location, false, bRightIsConst))
					{
						return 0;
					}
				}
				SetResultType(ResultType);
				const std::vector<std::string>* GenericArgs = GenericConcreteTypes.empty()
						? nullptr
						: &GenericConcreteTypes;
				return EmitFreeOperatorCall(Emitter, *FreeOp, 2, Location, GenericArgs) ? 1 : 0;
			};

		// Member += → member + → free += → free + (soft-miss unusable returns).
		if (CompoundMethodName)
		{
			const int MemberCompound = TryMember(CompoundMethodName, true);
			if (MemberCompound >= 0)
			{
				return MemberCompound == 1;
			}
		}
		{
			const int MemberBinary = TryMember(BinaryMethodName, false);
			if (MemberBinary >= 0)
			{
				return MemberBinary == 1;
			}
		}
		if (CompoundMethodName)
		{
			const int FreeCompound = TryFree(CompoundMethodName, true);
			if (FreeCompound >= 0)
			{
				return FreeCompound == 1;
			}
		}
		{
			const int FreeBinary = TryFree(BinaryMethodName, false);
			if (FreeBinary >= 0)
			{
				return FreeBinary == 1;
			}
		}
		if (TypeSupportsOperatorOverload(LeftType) || TypeSupportsOperatorOverload(RightType))
		{
			ReportError(
				std::string("no matching ") + BinaryMethodName + " for compound assignment",
				Location);
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::TryEmitOverloadedIncDec(
		FEmitter& Emitter,
		const FAssignExpr& Assign,
		const std::string& TargetTypeName,
		const FSourceLocation& Location)
	{
		if (!Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec)
		{
			return false;
		}
		if (!TypeSupportsOperatorOverload(TargetTypeName) || !Assign.Target)
		{
			return false;
		}
		if (Assign.CompoundOp != EBinaryOp::Add && Assign.CompoundOp != EBinaryOp::Subtract)
		{
			return false;
		}
		const char* const MethodName =
			Assign.CompoundOp == EBinaryOp::Add ? "operator++" : "operator--";

		// Index on overloadable user type cannot be a ++/-- target.
		// Native array index (element type user) falls through for normal GetIndex/SetIndex path.
		if (Assign.Target->Kind == EExprKind::IndexAccess)
		{
			const FIndexAccessExpr& IndexAccess =
				static_cast<const FIndexAccessExpr&>(*Assign.Target);
			std::string ObjectType;
			if (IndexAccess.Object
				&& TryResolveExprTypeName(*IndexAccess.Object, ObjectType)
				&& TypeSupportsOperatorOverload(ObjectType))
			{
				ReportError(
					"cannot assign through index on type " + DisplayTypeName(ObjectType)
						+ " (use methods; operator[] is not an assignable lvalue)",
					Location);
				return false;
			}
			return false;
		}

		if (Assign.bFromPostfixIncDec && !IsStructType(TargetTypeName))
		{
			ReportError(
				"postfix increment/decrement requires struct type (class identity is shared)",
				Location);
			return false;
		}

		std::string OwnerLinkage;
		const FMethodDecl* MethodDecl = nullptr;
		bool bMemberAmbiguous = false;
		const bool bMemberOk = ResolveMethodForCall(
			TargetTypeName,
			MethodName,
								   {},
			OwnerLinkage,
			MethodDecl,
			Location,
			false,
			false,
				&bMemberAmbiguous)
				&& MethodDecl;
		if (bMemberAmbiguous)
		{
			ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
			return false;
		}

		bool bFreeAmbiguous = false;
		const FFunctionDecl* FreeOp = nullptr;
		std::vector<std::string> GenericConcreteTypes;
		if (!bMemberOk)
		{
			if (!ResolveFreeOperatorOverload(
					MethodName,
					{TargetTypeName},
					FreeOp,
					GenericConcreteTypes,
						&bFreeAmbiguous,
					false))
			{
				if (bFreeAmbiguous)
				{
					ReportErrorId("diag.ambiguous_call_to", Location, {MethodName});
					return false;
				}
			}
		}
		if (!bMemberOk && !FreeOp)
		{
			ReportError(
				std::string("no matching ") + MethodName + " for type "
					+ DisplayTypeName(TargetTypeName),
				Location);
			return false;
		}

		std::string ResultType;
		if (bMemberOk)
		{
			std::unordered_map<std::string, std::string> OwnerSubst;
			const std::string& DeclOwner = OwnerLinkage.empty() ? TargetTypeName : OwnerLinkage;
			TryBuildGenericOwnerSubst(DeclOwner, OwnerSubst, Location);
			const std::string ReturnType = OwnerSubst.empty()
				? MethodDecl->ReturnType
				: SubstituteTypeNameWith(MethodDecl->ReturnType, OwnerSubst);
			const std::string OwnerModulePath = ResolveOwnerModulePath(DeclOwner);
			ResultType = QualifyTypeName(
				NormalizeTypeAlias(ReturnType, OwnerModulePath),
				OwnerModulePath);
		}
		else
		{
			if (FreeOp->ReturnType.empty())
			{
				ResultType = "void";
			}
			else if (GenericConcreteTypes.empty())
			{
				ResultType = QualifyTypeName(
					NormalizeTypeAlias(FreeOp->ReturnType, FreeOp->ModulePath),
					FreeOp->ModulePath);
			}
			else
			{
				std::unordered_map<std::string, std::string> FunctionSubst;
				for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(*FreeOp).size(); ++TypeIndex)
				{
					FunctionSubst.emplace(
						TemplateParameterNames(*FreeOp)[TypeIndex],
						GenericConcreteTypes[TypeIndex]);
				}
				ResultType = QualifyTypeName(
					NormalizeTypeAlias(
						SubstituteTypeNameWith(FreeOp->ReturnType, FunctionSubst),
						FreeOp->ModulePath),
					FreeOp->ModulePath);
			}
		}
		if (ResultType.empty() || ResultType == "void")
		{
			ReportError(std::string(MethodName) + " must return a non-void value", Location);
			return false;
		}
		if (!CanAssignBetweenTypes(TargetTypeName, ResultType, false))
		{
			ReportError(
				std::string(MethodName) + " return type is not assignable to target",
				Location);
			return false;
		}

		const auto EmitCallOnTos = [&]() -> bool
		{
			if (bMemberOk)
			{
				return EmitMemberOperatorCall(
					Emitter,
					TargetTypeName,
					MethodName,
						*MethodDecl,
					OwnerLinkage,
					0,
					Location);
			}
			if (!FreeOp->Parameters.empty())
			{
				const std::vector<std::string> ParamTypes =
					SpecializedFreeFunctionParamTypes(*FreeOp, GenericConcreteTypes);
				const std::string ParamType = QualifyTypeName(
					NormalizeTypeAlias(ParamTypes[0]),
					FreeOp->ModulePath);
				if (!EmitValueCoercionBetweenTypes(Emitter, ParamType, TargetTypeName, Location))
				{
					return false;
				}
			}
			const std::vector<std::string>* GenericArgs = GenericConcreteTypes.empty()
				? nullptr
				: &GenericConcreteTypes;
			return EmitFreeOperatorCall(Emitter, *FreeOp, 1, Location, GenericArgs);
		};

		const auto CoerceResultOnTos = [&]() -> bool
		{
			if (!EmitValueCoercionBetweenTypes(Emitter, TargetTypeName, ResultType, Location))
			{
				return false;
			}
			if (NeedsPassByValueCopy(TargetTypeName))
			{
				EmitValueCopy(Emitter, TargetTypeName);
			}
			return true;
		};

		if (Assign.Target->Kind == EExprKind::Identifier)
		{
			const std::string& Name =
				static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
			enum class EStorage : std::uint8_t
			{
				Local,
				ThisField,
				Global,
			} Storage{};
			std::uint16_t Slot = 0;
			if (ResolveLocal(Name, Slot))
			{
				Storage = EStorage::Local;
			}
			else if (ResolveThisField(Name, Slot))
			{
				if (!ValidateThisFieldAccess(Location, Name))
				{
					return false;
				}
				Storage = EStorage::ThisField;
			}
			else
			{
				const std::string GlobalLinkageName =
					ResolveGlobalLinkageName(Name, CurrentModulePath);
				if (GlobalLinkageName.empty() || !ResolveGlobal(GlobalLinkageName, Slot))
				{
					ReportError("undeclared identifier: " + Name, Location);
					return false;
				}
				Storage = EStorage::Global;
			}
			const auto EmitLoad = [&]()
			{
				if (Storage == EStorage::Local)
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
				}
				else if (Storage == EStorage::ThisField)
				{
					Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
				}
				else
				{
					Emitter.EmitU16Operand(EOpcode::LoadGlobal, Slot);
				}
			};
			const auto EmitStore = [&]()
			{
				if (Storage == EStorage::Local)
				{
					Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
				}
				else if (Storage == EStorage::ThisField)
				{
					Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(Name));
				}
				else
				{
					Emitter.EmitU16Operand(EOpcode::StoreGlobal, Slot);
				}
			};

			std::uint16_t OldTemp = 0;
			if (Assign.bFromPostfixIncDec)
			{
				EmitLoad();
				// Class handles share identity; clone so postfix keeps the pre-mutation value.
				EmitValueCopy(Emitter, TargetTypeName);
				OldTemp = AllocateAssignTemp();
				Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
			}
			EmitLoad();
			if (!EmitCallOnTos() || !CoerceResultOnTos())
			{
				return false;
			}
			EmitStore();
			if (Assign.bFromPostfixIncDec)
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
			}
			else
			{
				EmitLoad();
			}
			return true;
		}

		if (Assign.Target->Kind == EExprKind::MemberAccess)
		{
			const FMemberAccessExpr& MemberAccess =
				static_cast<const FMemberAccessExpr&>(*Assign.Target);
			FScope::FMapEntryViewBinding EntryView;
			std::vector<std::string> EntryMembers;
			if (TryGetMapEntryViewMemberPath(*Assign.Target, EntryView, EntryMembers))
			{
				if (EntryMembers.empty() || EntryMembers[0] == "first")
				{
					ReportErrorId("diag.cannot_assign_map_entry_view_key", Location);
					return false;
				}
				if (EntryMembers[0] != "second")
				{
					ReportErrorId(
						"diag.unknown_field_on_type",
						Location,
						{EntryMembers[0], "pair"});
					return false;
				}
				std::uint16_t WriteSlot = EntryView.RefSlot;
				if (EntryMembers.size() > 1)
				{
					std::vector<std::string> NestedFields(
						EntryMembers.begin() + 1,
						EntryMembers.end());
					if (!EmitMapEntryViewValueFieldRef(Emitter, EntryView, NestedFields))
					{
						return false;
					}
					WriteSlot = AllocateAssignTemp();
					Emitter.EmitU16Operand(EOpcode::BindLocal, WriteSlot);
				}
				Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
				std::uint16_t OldTemp = 0;
				if (Assign.bFromPostfixIncDec)
				{
					OldTemp = AllocateAssignTemp();
					EmitValueCopy(Emitter, TargetTypeName);
					Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
					Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
				}
				if (!EmitCallOnTos() || !CoerceResultOnTos())
				{
					return false;
				}
				if (NeedsPassByValueCopy(TargetTypeName))
				{
					EmitValueCopy(Emitter, TargetTypeName);
				}
				Emitter.EmitU16Operand(EOpcode::StoreLocal, WriteSlot);
				if (Assign.bFromPostfixIncDec)
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
				}
				else
				{
					Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
				}
				return true;
			}
			if (!CompileExpression(*MemberAccess.Object))
			{
				return false;
			}
			{
				std::string ObjectTypeName;
				if (!TryResolveExprTypeName(*MemberAccess.Object, ObjectTypeName)
					|| !ValidateMemberAccess(
						Location, ObjectTypeName, MemberAccess.Member, false))
				{
					if (!HasError())
					{
						ReportErrorId(
							"diag.inaccessible_member",
							Location,
							{MemberAccess.Member, ObjectTypeName.empty() ? "?" : ObjectTypeName});
					}
					return false;
				}
			}
			Emitter.EmitU16Operand(EOpcode::LoadFieldKeep, AddString(MemberAccess.Member));
			std::uint16_t OldTemp = 0;
			if (Assign.bFromPostfixIncDec)
			{
				OldTemp = AllocateAssignTemp();
				EmitValueCopy(Emitter, TargetTypeName);
				Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
				Emitter.EmitU16Operand(EOpcode::LoadFieldKeep, AddString(MemberAccess.Member));
			}
			if (!EmitCallOnTos() || !CoerceResultOnTos())
			{
				return false;
			}
			const std::uint16_t ValueTemp = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
			Emitter.EmitU16Operand(EOpcode::StoreField, AddString(MemberAccess.Member));
			Emitter.Emit(EOpcode::Pop);
			if (Assign.bFromPostfixIncDec)
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
			}
			else
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
			}
			return true;
		}

		ReportError("invalid increment/decrement target for operator overload", Location);
		return false;
	}

	bool VeroScriptCompiler::TryResolveOverloadedBinaryType(
		const FBinaryExpr& Binary,
		std::string& OutTypeName,
		const std::string* const KnownLeftType,
		const std::string* const KnownRightType) const
	{
		const char* const MethodName = BinaryOperatorMethodName(Binary.Op);
		if (!MethodName || !Binary.Left || !Binary.Right)
		{
			return false;
		}
		std::string LeftType = KnownLeftType ? *KnownLeftType : std::string{};
		std::string RightType = KnownRightType ? *KnownRightType : std::string{};
		if (!KnownLeftType && !TryResolveExprTypeName(*Binary.Left, LeftType))
		{
			LeftType.clear();
		}
		if (!KnownRightType && !TryResolveExprTypeName(*Binary.Right, RightType))
		{
			RightType.clear();
		}
		// Same scoring as emit (silent — no diagnostics during type inference).
		if (LeftType.empty() || RightType.empty())
		{
			return false;
		}
		// Keep type inference identical to bytecode emission: an expression with
		// two primitive operands is a built-in operation and cannot be captured by
		// a free operator through a user-defined conversion of either operand.
		if (!TypeSupportsOperatorOverload(LeftType)
			&& !TypeSupportsOperatorOverload(RightType))
		{
			return false;
		}
		const std::string LeftArgType = EncodeOperatorArgType(*Binary.Left);
		const std::string RightArgType = EncodeOperatorArgType(*Binary.Right);
		const auto TryMemberPair = [&](
			const FExpr& OwnerExpression,
			const std::string& OwnerType,
			const char* const Name,
			const std::string& ArgType,
			const FExpr& ArgExpression,
			bool& OutAmbiguous) -> bool
		{
			OutAmbiguous = false;
			if (!TypeSupportsOperatorOverload(OwnerType))
			{
				return false;
			}
			if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
					OwnerType, Name, {ArgType}, OutAmbiguous, nullptr, IsConstAssignTarget(OwnerExpression)))
			{
				OutTypeName = HostOperatorReturnTypeName(*HostOperator, OwnerType);
				return true;
			}
			if (OutAmbiguous)
			{
				return false;
			}
			std::string OwnerLinkage;
			const FMethodDecl* MethodDecl = nullptr;
			if (!ResolveMethodForCall(
					OwnerType,
					Name,
					{EncodeOperatorArgType(ArgExpression)},
					OwnerLinkage,
					MethodDecl,
					Binary.Location,
					false,
					false,
						&OutAmbiguous, IsConstAssignTarget(OwnerExpression))
						|| !MethodDecl)
			{
				return false;
			}
			std::unordered_map<std::string, std::string> OwnerSubst;
			FSourceLocation DummyLocation = {};
			const std::string& DeclOwner = OwnerLinkage.empty() ? OwnerType : OwnerLinkage;
			TryBuildGenericOwnerSubst(DeclOwner, OwnerSubst, DummyLocation);
			const std::string DeclModule = ResolveOwnerModulePath(DeclOwner);
			const std::string ReturnType = OwnerSubst.empty()
				? MethodDecl->ReturnType
				: SubstituteTypeNameWith(MethodDecl->ReturnType, OwnerSubst);
			OutTypeName = QualifyTypeName(
				NormalizeTypeAlias(ReturnType, DeclModule),
				DeclModule);
			return true;
		};

		bool bLeftAmbiguous = false;
		if (TryMemberPair(*Binary.Left, LeftType, MethodName, RightType, *Binary.Right, bLeftAmbiguous))
		{
			return true;
		}
		// Align with emit: member ambiguity blocks free/fallback type claims.
		if (bLeftAmbiguous)
		{
			return false;
		}

		bool bRightAmbiguous = false;
		if (TryMemberPair(*Binary.Right, RightType, MethodName, LeftType, *Binary.Left, bRightAmbiguous))
		{
			return true;
		}
		if (bRightAmbiguous)
		{
			return false;
		}

		bool bFreeAmbiguous = false;
		const FFunctionDecl* FreeOp = nullptr;
		std::vector<std::string> GenericConcreteTypes;
		const auto ResolveFreeReturnType =
			[&](const FFunctionDecl& Function, const std::vector<std::string>& ConcreteTypes) -> std::string
			{
				if (Function.ReturnType.empty())
				{
					return "void";
				}
				if (ConcreteTypes.empty())
				{
					return QualifyTypeName(
							NormalizeTypeAlias(Function.ReturnType, Function.ModulePath),
							Function.ModulePath);
				}
				std::unordered_map<std::string, std::string> FunctionSubst;
				for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(Function).size(); ++TypeIndex)
				{
					FunctionSubst.emplace(TemplateParameterNames(Function)[TypeIndex], ConcreteTypes[TypeIndex]);
				}
				return QualifyTypeName(
						NormalizeTypeAlias(
							SubstituteTypeNameWith(Function.ReturnType, FunctionSubst),
							Function.ModulePath),
						Function.ModulePath);
			};
		if (ResolveFreeOperatorOverload(
				MethodName,
				{LeftArgType, RightArgType},
				FreeOp,
				GenericConcreteTypes,
					&bFreeAmbiguous,
				false))
		{
			OutTypeName = ResolveFreeReturnType(*FreeOp, GenericConcreteTypes);
			return true;
		}
		if (bFreeAmbiguous)
		{
			return false;
		}
		// != synthesizes from == → bool.
		if (Binary.Op == EBinaryOp::NotEqual)
		{
			bool bEqAmbiguous = false;
			if (TryMemberPair(*Binary.Left, LeftType, "operator==", RightType, *Binary.Right, bEqAmbiguous))
			{
				OutTypeName = "bool";
				return true;
			}
			if (bEqAmbiguous)
			{
				return false;
			}
			bool bEqRightAmbiguous = false;
			if (TryMemberPair(*Binary.Right, RightType, "operator==", LeftType, *Binary.Left, bEqRightAmbiguous))
			{
				OutTypeName = "bool";
				return true;
			}
			if (bEqRightAmbiguous)
			{
				return false;
			}
			bool bFreeEqAmbiguous = false;
			const FFunctionDecl* FreeEq = nullptr;
			std::vector<std::string> EqGenericConcreteTypes;
			if (ResolveFreeOperatorOverload(
					"operator==",
					{LeftArgType, RightArgType},
					FreeEq,
					EqGenericConcreteTypes,
						&bFreeEqAmbiguous,
					false))
			{
				OutTypeName = "bool";
				return true;
			}
			if (bFreeEqAmbiguous)
			{
				return false;
			}
		}
		// Relational mirrors: a > b from b < a (swapped operands).
		if (const std::optional<EBinaryOp> MirrorOp = MakeMirrorBinaryOp(Binary.Op))
		{
			const char* const MirrorName = BinaryOperatorMethodName(*MirrorOp);
			if (MirrorName)
			{
				bool bMirrorRightAmbiguous = false;
				if (TryMemberPair(*Binary.Right, RightType, MirrorName, LeftType, *Binary.Left, bMirrorRightAmbiguous))
				{
					return true;
				}
				if (bMirrorRightAmbiguous)
				{
					return false;
				}
				bool bMirrorFreeAmbiguous = false;
				const FFunctionDecl* FreeMirror = nullptr;
				std::vector<std::string> MirrorGenericConcreteTypes;
				if (ResolveFreeOperatorOverload(
						MirrorName,
						{RightArgType, LeftArgType},
						FreeMirror,
						MirrorGenericConcreteTypes,
							&bMirrorFreeAmbiguous,
						false))
				{
					OutTypeName = ResolveFreeReturnType(*FreeMirror, MirrorGenericConcreteTypes);
					return true;
				}
				if (bMirrorFreeAmbiguous)
				{
					return false;
				}
			}
		}
		return false;
	}

	bool VeroScriptCompiler::TryResolveOverloadedUnaryType(
		const FUnaryExpr& Unary,
		std::string& OutTypeName) const
	{
		const char* const MethodName = UnaryOperatorMethodName(Unary.Op);
		if (!MethodName || !Unary.Operand)
		{
			return false;
		}
		std::string OperandType;
		if (!TryResolveExprTypeName(*Unary.Operand, OperandType))
		{
			return false;
		}
		if (TypeSupportsOperatorOverload(OperandType))
		{
			bool bHostAmbiguous = false;
			if (const FHostMemberDesc* const HostOperator = ResolveHostOperator(
					OperandType, MethodName, {}, bHostAmbiguous, nullptr, IsConstAssignTarget(*Unary.Operand)))
			{
				OutTypeName = HostOperatorReturnTypeName(*HostOperator, OperandType);
				return true;
			}
			if (bHostAmbiguous)
			{
				return false;
			}
			std::string OwnerLinkage;
			const FMethodDecl* MethodDecl = nullptr;
			bool bAmbiguous = false;
			if (ResolveMethodForCall(
					OperandType,
					MethodName,
					{},
					OwnerLinkage,
					MethodDecl,
					Unary.Location,
					false,
					false,
						&bAmbiguous, IsConstAssignTarget(*Unary.Operand))
						&& MethodDecl)
			{
				std::unordered_map<std::string, std::string> OwnerSubst;
				FSourceLocation DummyLocation = {};
				const std::string& DeclOwner = OwnerLinkage.empty() ? OperandType : OwnerLinkage;
				TryBuildGenericOwnerSubst(DeclOwner, OwnerSubst, DummyLocation);
				const std::string ReturnType = OwnerSubst.empty()
					? MethodDecl->ReturnType
					: SubstituteTypeNameWith(MethodDecl->ReturnType, OwnerSubst);
				const std::string OwnerModulePath = ResolveOwnerModulePath(DeclOwner);
				OutTypeName = QualifyTypeName(
					NormalizeTypeAlias(ReturnType, OwnerModulePath),
					OwnerModulePath);
				return true;
			}
			if (bAmbiguous)
			{
				return false;
			}
		}
		bool bFreeAmbiguous = false;
		const FFunctionDecl* FreeOp = nullptr;
		std::vector<std::string> GenericConcreteTypes;
		if (ResolveFreeOperatorOverload(
				MethodName,
				{EncodeOperatorArgType(*Unary.Operand)},
				FreeOp,
				GenericConcreteTypes,
					&bFreeAmbiguous,
				false))
		{
			if (FreeOp->ReturnType.empty())
			{
				OutTypeName = "void";
			}
			else if (GenericConcreteTypes.empty())
			{
				OutTypeName = QualifyTypeName(
					NormalizeTypeAlias(FreeOp->ReturnType, FreeOp->ModulePath),
					FreeOp->ModulePath);
			}
			else
			{
				std::unordered_map<std::string, std::string> FunctionSubst;
				for (std::size_t TypeIndex = 0; TypeIndex < TemplateParameterNames(*FreeOp).size(); ++TypeIndex)
				{
					FunctionSubst.emplace(
						TemplateParameterNames(*FreeOp)[TypeIndex],
						GenericConcreteTypes[TypeIndex]);
				}
				OutTypeName = QualifyTypeName(
					NormalizeTypeAlias(
						SubstituteTypeNameWith(FreeOp->ReturnType, FunctionSubst),
						FreeOp->ModulePath),
					FreeOp->ModulePath);
			}
			return true;
		}
		if (bFreeAmbiguous)
		{
			return false;
		}
		return false;
	}

}
