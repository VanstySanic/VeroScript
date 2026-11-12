// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

namespace VeroScript
{
	struct FBlockStmt;
	struct FStmt;
	struct FSwitchStmt;

	namespace CompilerDetail
	{
		bool StatementContainsLocalFallthrough(const FStmt& Statement);
		bool BlockContainsLocalFallthrough(const FBlockStmt& Block);
		bool SwitchAlwaysReturns(const FSwitchStmt& Switch);
		bool BlockAlwaysReturns(const FBlockStmt& Block);
		bool TypeNameMentionsAuto(const std::string& TypeName);
	}
}
