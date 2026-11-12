// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNumericBindings.h"
#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConstantEvaluation.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptStandardLibraryServices.h"

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
		std::size_t ExpressionCompileStackCost(const EExprKind Kind) noexcept
		{
			// Conservative cross-build estimates. In particular, MSVC /Od reserves
			// stack for locals from mutually exclusive semantic branches instead of
			// reusing their slots. Keeping the budget semantic rather than build-mode
			// dependent also makes malformed-source behavior consistent across hosts.
			switch (Kind)
			{
			case EExprKind::Call:
				return 40u * 1024u;
			case EExprKind::Assign:
			case EExprKind::Ref:
				return 24u * 1024u;
			case EExprKind::MethodCall:
				return 16u * 1024u;
			case EExprKind::Binary:
			case EExprKind::Conditional:
			case EExprKind::MemberAccess:
			case EExprKind::IndexAccess:
				return 10u * 1024u;
			default:
				return 6u * 1024u;
			}
		}
	}

	bool VeroScriptCompiler::CompileLiteral(const FExpr& Expression)
	{
		CurrentEmitLocation = Expression.Location;
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		case EExprKind::IntegerLiteral:
			{
				const FIntegerLiteralExpr& Literal = static_cast<const FIntegerLiteralExpr&>(Expression);
				FBytecodeConstant Constant;
				if (Literal.bIs64 && Literal.bUnsigned)
				{
					Constant.Kind = EConstantKind::U64;
					Constant.U64 = Literal.UnsignedValue64;
				}
				else if (Literal.bIs64)
				{
					Constant.Kind = EConstantKind::I64;
					Constant.I64 = Literal.Value64;
				}
				else if (Literal.bUnsigned)
				{
					Constant.Kind = EConstantKind::U32;
					Constant.U32 = Literal.UnsignedValue;
				}
				else
				{
					Constant.Kind = EConstantKind::I32;
					Constant.I32 = Literal.Value;
				}
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
				return true;
			}
		case EExprKind::CharacterLiteral:
			{
				FBytecodeConstant Constant;
				Constant.Kind = EConstantKind::I8;
				Constant.I8 = static_cast<const FCharacterLiteralExpr&>(Expression).Value;
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
				return true;
			}
		case EExprKind::FloatLiteral:
			{
				const auto& Literal = static_cast<const FFloatLiteralExpr&>(Expression);
				FBytecodeConstant Constant;
				if (Literal.bFloat32)
				{
					Constant.Kind = EConstantKind::F32;
					Constant.F32 = static_cast<float>(Literal.Value);
				}
				else
				{
					Constant.Kind = EConstantKind::F64;
					Constant.F64 = Literal.Value;
				}
				Emitter.EmitU16Operand(EOpcode::ConstF64, AddConstant(Constant));
				return true;
			}
		case EExprKind::BooleanLiteral:
			{
				Emitter.EmitU8Operand(
				    EOpcode::ConstBool,
				    static_cast<const FBooleanLiteralExpr&>(Expression).Value ? 1 : 0);
				return true;
			}
		case EExprKind::StringLiteral:
			{
				FBytecodeConstant Constant;
				Constant.Kind = EConstantKind::Str;
				Constant.StringIndex = AddString(static_cast<const FStringLiteralExpr&>(Expression).Value);
				if (bHasError)
				{
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
				return true;
			}
		case EExprKind::WStringLiteral:
			{
				FBytecodeConstant Constant;
				Constant.Kind = EConstantKind::WStr;
				Constant.StringIndex = AddWString(static_cast<const FWStringLiteralExpr&>(Expression).Value);
				if (bHasError)
				{
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
				return true;
			}
		case EExprKind::NullptrLiteral:
			{
				Emitter.Emit(EOpcode::ConstNull);
				return true;
			}
		default:
			return false;
		}
	}

	bool VeroScriptCompiler::CompileExpression(const FExpr& Expression)
	{
		constexpr std::size_t MaxExpressionCompileStackCost = 640u * 1024u;
		const std::size_t StackCost = ExpressionCompileStackCost(Expression.Kind);
		if (StackCost > MaxExpressionCompileStackCost || ActiveExpressionCompileStackCost > MaxExpressionCompileStackCost - StackCost)
		{
			ReportError("expression nested too deeply", Expression.Location);
			return false;
		}
		struct FExpressionCompileStackScope
		{
			~FExpressionCompileStackScope()
			{
				ActiveCost -= Cost;
			}

			std::size_t& ActiveCost;
			std::size_t Cost;

		} StackScope{ActiveExpressionCompileStackCost, StackCost};
		ActiveExpressionCompileStackCost += StackCost;

		const bool bPreserveReference = bPreserveNextReferenceResult;
		bPreserveNextReferenceResult = false;
		const bool bPreviousCurrentPreserve = bCurrentExpressionPreservesReference;
		bCurrentExpressionPreservesReference = bPreserveReference;
		const bool bCompiled = CompileExpressionImpl(Expression);
		bCurrentExpressionPreservesReference = bPreviousCurrentPreserve;
		if (!bCompiled)
		{
			return false;
		}
		if (!bPreserveReference)
		{
			std::string TypeName;
			if (TryResolveExprRawTypeName(Expression, TypeName) && Runtime::IsReferenceTypeName(TypeName))
			{
				MakeEmitter().Emit(EOpcode::DerefRef);
			}
		}
		return true;
	}

	bool VeroScriptCompiler::CompileExpressionImpl(const FExpr& Expression)
	{
		if (bHasError)
		{
			return false;
		}

		CurrentEmitLocation = Expression.Location;

		if (CompileLiteral(Expression))
		{
			return true;
		}

		switch (Expression.Kind)
		{
		case EExprKind::Lambda:
		case EExprKind::Identifier:
		case EExprKind::This:
		case EExprKind::Unary:
			return CompilePrimaryExpression(Expression);
		case EExprKind::Binary:
			return CompileBinaryExpression(Expression);
		case EExprKind::Ref:
		case EExprKind::Assign:
			return CompileAssignmentExpression(Expression);
		case EExprKind::Conditional:
		case EExprKind::MemberAccess:
		case EExprKind::IndexAccess:
			return CompileMemberAccessExpression(Expression);
		case EExprKind::MethodCall:
			return CompileMethodCallExpression(Expression);
		case EExprKind::Call:
			return CompileCallExpression(Expression);
		case EExprKind::ArrayInit:
		case EExprKind::MapInit:
			return CompileInitializerExpression(Expression);
		default:
			{
				ReportError("unsupported expression", Expression.Location);
				return false;
			}
		}
	}

	// Keep each expression family in its own function. Besides making the source
	// boundary explicit, this prevents MSVC Debug builds from reserving stack for
	// every local in every expression case on each recursive AST visit. The former
	// monolithic switch used roughly 76 KiB per level and could exhaust a default
	// Windows thread stack on an ordinary right-associated expression.
	bool VeroScriptCompiler::CompilePrimaryExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionPrimary.inl"
		default:
			{
				ReportError("internal compiler error: invalid primary expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileBinaryExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionBinary.inl"
		default:
			{
				ReportError("internal compiler error: invalid binary expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileAssignmentExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionAssignment.inl"
		default:
			{
				ReportError("internal compiler error: invalid assignment expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileMemberAccessExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionMemberAccess.inl"
		default:
			{
				ReportError("internal compiler error: invalid member expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileMethodCallExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionMethodCall.inl"
		default:
			{
				ReportError("internal compiler error: invalid method call expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileCallExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionCall.inl"
		default:
			{
				ReportError("internal compiler error: invalid call expression", Expression.Location);
				return false;
			}
		}
	}

	bool VeroScriptCompiler::CompileInitializerExpression(const FExpr& Expression)
	{
		FEmitter Emitter = MakeEmitter();
		switch (Expression.Kind)
		{
		#include "Expression/VeroScriptCompilerExpressionInitializers.inl"
		}
	}

}
