// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

namespace VeroScript
{
	struct FProgram;
	struct FExpr;
	struct FStmt;
	struct FBlockStmt;
	void VisitBlockNodes(const FBlockStmt&, const std::function<void(const FExpr&)>&,
		const std::function<void(const FStmt&)>&);
	void VisitBlockNodes(FBlockStmt&, const std::function<void(FExpr&)>&, const std::function<void(FStmt&)>&);
	void VisitExpressionNodes(FExpr&, const std::function<void(FExpr&)>&);
	void VisitExpressionNodes(const FExpr&, const std::function<void(const FExpr&)>&,
		const std::function<void(const FStmt&)>& = {});
	void VisitStatementNodes(const FStmt&, const std::function<void(const FExpr&)>&,
		const std::function<void(const FStmt&)>&);
	void VisitProgramNodes(FProgram&, const std::function<void(FExpr&)>&, const std::function<void(FStmt&)>&);
	void VisitProgramNodes(const FProgram&, const std::function<void(const FExpr&)>&, const std::function<void(const FStmt&)>&);
	// Shared complete traversal, including defaults, constructors and lambda bodies.
	void VisitProgramExpressions(FProgram&, const std::function<void(FExpr&)>&);
}
