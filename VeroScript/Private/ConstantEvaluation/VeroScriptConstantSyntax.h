// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAnalysisControl.h"

namespace VeroScript
{
	inline FTemplateEvaluationResult CheckConstantFunctionSyntax(const FBlockStmt &Body,
	    const std::function<bool(const FCallExpr &)> &CanBeConstantCall)
	{
		FTemplateEvaluationResult Result;
		Result.Status = ETemplateEvaluationStatus::Constant;
		// An open template has no selected constexpr branch yet. Preserve its
		// syntax, and validate only the surrounding pure body at declaration time.
		bool HasBranches = false;
		VisitBlockNodes(Body, [](const FExpr &)
		{
		}, [&](const FStmt &Node)
		{
			HasBranches = HasBranches || (Node.Kind == EStmtKind::If && static_cast<const FIfStmt &>(Node).bConstexpr);
		});
		std::unique_ptr<FBlockStmt> Skeleton;
		if (HasBranches)
		{
			Skeleton = CloneBlockStatement(Body);
			VisitBlockNodes(*Skeleton, [](FExpr &)
			{
			}, [](FStmt &Node)
			{
				if (Node.Kind != EStmtKind::If || !static_cast<FIfStmt &>(Node).bConstexpr) return;
				auto &If = static_cast<FIfStmt &>(Node);
				If.Condition = std::make_unique<FBooleanLiteralExpr>(true);
				If.ThenBranch.reset();
				If.ElseBranch.reset();
			});
		}
		const auto Reject = [&](const FSourceLocation &Location, const char *Reason)
		{
			if (!Result.Succeeded()) return;
			Result.Status = ETemplateEvaluationStatus::Invalid;
			Result.Location = Location;
			Result.Reason = Reason;
		};
		VisitBlockNodes(Skeleton ? static_cast<const FBlockStmt &>(*Skeleton) : Body, [&](const FExpr &Expression)
		{
			CheckAnalysisCancellation();
			switch (Expression.Kind)
			{
			case EExprKind::IntegerLiteral:
			case EExprKind::CharacterLiteral:
			case EExprKind::BooleanLiteral:
			case EExprKind::Identifier:
			case EExprKind::MemberAccess:
			case EExprKind::Unary:
			case EExprKind::Binary:
			case EExprKind::Conditional:
			case EExprKind::MethodCall:
				{
					break;
				}
			case EExprKind::Assign:
				{
					if (!static_cast<const FAssignExpr &>(Expression).Target ||
					    static_cast<const FAssignExpr &>(Expression).Target->Kind != EExprKind::Identifier)
					{
						Reject(Expression.Location, "constexpr assignment requires a local scalar");
					}
					break;
				}
			case EExprKind::Call:
				{
					const auto &Call = static_cast<const FCallExpr &>(Expression);
					if (Call.CalleeExpr || (Call.Callee != "cast" && !CanBeConstantCall(Call)))
					{
						Reject(Expression.Location, "constexpr call has no constexpr candidate");
					}
					break;
				}
			default:
				{
					Reject(Expression.Location, "expression is outside the pure scalar constexpr subset");
					break;
				}
			}
		}, [&](const FStmt &Statement)
		{
			CheckAnalysisCancellation();
			switch (Statement.Kind)
			{
			case EStmtKind::Expr:
			case EStmtKind::Block:
			case EStmtKind::VarDeclGroup:
			case EStmtKind::Return:
			case EStmtKind::If:
			case EStmtKind::While:
			case EStmtKind::DoWhile:
			case EStmtKind::For:
			case EStmtKind::Switch:
			case EStmtKind::Break:
			case EStmtKind::Continue:
			case EStmtKind::Fallthrough:
				{
					break;
				}
			case EStmtKind::VarDecl:
				{
					if (static_cast<const FVarDeclStmt &>(Statement).bIsRef || !static_cast<const FVarDeclStmt &>(Statement).Initializer)
					{
						Reject(Statement.Location, "constexpr local requires an initialized scalar value");
					}
					break;
				}
			default:
				{
					Reject(Statement.Location, "statement is outside the pure scalar constexpr subset");
					break;
				}
			}
		});
		return Result;
	}
}
