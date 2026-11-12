// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
namespace VeroScript
{
	std::unique_ptr<FProgram> CloneProgram(const FProgram&);
	// Borrowed source strings remain anchored by the immutable input program.
	FMethodDecl CloneMethodDeclaration(const FMethodDecl&);
	FFunctionParam CloneFunctionParameter(const FFunctionParam&);
	std::unique_ptr<FBlockStmt> CloneBlockStatement(const FBlockStmt&);
	std::unique_ptr<FExpr> CloneExpression(const FExpr&);
}
