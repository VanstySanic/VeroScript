// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptTypes.h"

#include <string>
#include <functional>

namespace VeroScript
{
	namespace ModuleVisibility
	{
		class FImportIndex;
	}

	struct FMemberAccessInfo
	{
		bool bFound = false;
		EMemberAccess EffectiveAccess = EMemberAccess::Private;
		std::string DeclaringOwnerLinkage;
		// Type-parameter arity of the declaring type (`Box` → 0, `Box<T>` → 1).
		std::size_t DeclaringArity = 0;
	};

	EMemberAccess ApplyInheritanceAccess(
		EMemberAccess MemberAccess,
		EMemberAccess InheritanceAccess);

	// True when NewAccess is strictly tighter than OldAccess (public < protected < private).
	bool IsAccessMoreRestricted(EMemberAccess NewAccess, EMemberAccess OldAccess);

	bool CanAccessMemberFromContext(
		const FMemberAccessInfo& Info,
		const std::string& ContextOwnerLinkage,
		const FProgram& Program,
	// Static type of the object expression; empty = treat as Context (bare `this` field).
		const std::string& ReceiverTypeLinkage = {}, bool bTypeName = false,
		const ModuleVisibility::FImportIndex* ImportIndex = nullptr);

	bool ValidateScopedTypeDeclarations(const FProgram& Program,
		const ModuleVisibility::FImportIndex* ImportIndex, FScriptError& Error,
		const std::function<bool(const FMemberAccessInfo&, const FScopedTypeUse&)>& FunctionAccess = {},
		const std::vector<FScopedTypeUse>* Uses = nullptr,
		const std::function<bool(std::string_view)>& CheckScope = {});

	// True only when Context is an explicitly named type friend of DeclaringOwner.
	bool IsFriendTypeContext(
		const FProgram& Program,
		const std::string& DeclaringOwnerLinkage,
		std::size_t DeclaringArity,
		const std::string& ContextOwnerLinkage,
		const std::string& ReceiverTypeLinkage = {},
		const ModuleVisibility::FImportIndex* ImportIndex = nullptr);

	/**
	 * True when Base is an accessible base of Derived for `using Base::member`
	 * (direct bases always count; do not walk through private inheritance).
	 */
	bool IsAccessibleBase(
		const FProgram& Program,
		const std::string& DerivedLinkage,
		std::size_t DerivedArity,
		const std::string& BaseLinkage,
		std::size_t BaseArity);
}
