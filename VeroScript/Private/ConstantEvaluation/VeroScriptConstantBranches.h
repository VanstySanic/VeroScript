// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include <unordered_map>

namespace VeroScript
{
	// Conditional scopes are indexed once per compilation. Eager declaration
	// validation defers their semantics until an enclosing instance selects them.
	struct FConstantBranchState
	{
		struct FGuard
		{
			std::string Key;
			bool bThen;
		};
		std::unordered_map<std::string, FGuard> Guards;
		std::unordered_map<std::string, bool> Selections;
		explicit FConstantBranchState(const FProgram &);
		static std::string Key(const FIfStmt &);
		bool Includes(std::string_view Scope) const;
		bool Guarded(std::string_view Scope) const;
		void SelectEnclosing(std::string_view Scope);
		bool Includes(const FScopedTypeUse &Use) const
		{
			return !Use.Context || Includes(Use.Context->ScopeId);
		}
	};
	struct FConstantBranchSelectionScope
	{
		std::shared_ptr<FConstantBranchState> State;
		std::unordered_map<std::string, bool> Previous;
		explicit FConstantBranchSelectionScope(std::shared_ptr<FConstantBranchState> State)
		    : State(std::move(State)), Previous(this->State ? this->State->Selections : decltype(Previous){})
		{
		}
		~FConstantBranchSelectionScope()
		{
			if (State)
			{
				State->Selections = std::move(Previous);
			}
		}
	};
}
