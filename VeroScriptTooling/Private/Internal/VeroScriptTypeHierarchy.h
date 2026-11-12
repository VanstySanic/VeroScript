// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptSymbolQuery.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	// Strip trailing `<...>` type arguments for simple-name lookup.
	std::string StripGenericArgs(std::string_view TypeNameAsWritten);

	/**
	 * Match outline/AST decl name against an identifier under the caret.
	 * Accepts exact names, generic heads (`Foo` vs `Foo<T>`), and namespace leaves
	 * (`C` / `N.C` vs `N.C` / `N.C<T>`).
	 */
	bool SymbolNameMatchesIdent(std::string_view SymbolName, std::string_view Ident);

	/**
	 * Fuzzy completion prefix match:
	 * - normal / leaf prefix
	 * - underscore-insensitive (`tostring` → `to_string`)
	 * - camel/snake segment subsequence (`SetValue` → `SetNextValue`)
	 */
	bool FuzzyIdentifierPrefixMatches(std::string_view Label, std::string_view Prefix);

	// Follow `using`/`typedef` aliases on the program until a concrete type name.
	std::string ResolveTypeAlias(const FProgram& Program, std::string_view TypeName);

	// Resolve a declaration's aliases in its own lexical/module scope. Supplied
	// generic bindings are already resolved in the caller's scope.
	// bResolveBindings expands raw local aliases in the supplied scope instead;
	// leave it false for generic substitutions to preserve caller type identities.
	std::string ResolveDeclarationTypeName(const FProgram& Program, std::string_view Owner,
		std::string_view ModulePath, std::string_view TypeName,
		const std::vector<std::pair<std::string, std::string>>& Bindings = {},
		bool bResolveBindings = false);

	/**
	 * Substitute open-generic type params in a member Detail using TypeNameAsWritten
	 * (`IA<int>` + Detail `T ()` → `int ()`). Soft declarations can supply their
	 * formal parameters through TypeSymbol instead of a script AST declaration.
	 * Additional bindings (e.g. a method's explicit arguments) are applied in the
	 * same pass so introduced type names cannot be mistaken for placeholders.
	 */
	std::string SpecializeMemberDetail(
		const FProgram& Program,
		std::string_view TypeNameAsWritten,
		std::string_view DetailTemplate,
		const FDocumentSymbol* TypeSymbol = nullptr,
		const std::vector<std::pair<std::string, std::string>>& AdditionalBindings = {});

	/**
	 * Substitute explicit and value-inferred generic function arguments, followed by
	 * declaration defaults, into a source-facing signature Detail. Unknown argument
	 * types leave the corresponding generic parameter unresolved.
	 */
	std::string SpecializeFunctionDetail(
		const FProgram& Program,
		std::string_view FunctionLabel,
		const std::vector<std::string>& ExplicitTypeArgs,
		const std::vector<std::string>& ArgumentTypeHints,
		std::string_view DetailTemplate);

	/**
	 * Declaration-order DFS type chain (Root first, then each base's full subtree).
	 * Matches compiler LookupMemberAccess / member binding under multiple inheritance.
	 * Uses FProgram AST only — no compile / bytecode.
	 */
	void CollectTypeHierarchy(
		const FProgram& Program,
		std::string_view RootTypeName,
		std::vector<std::string>& OutClosestFirst);

	struct FOverrideMemberCandidate
	{
		std::string Name;
		std::string Signature;
		std::string Declaration;
		bool bInterfaceImplementation = false;
	};

	/** Methods that can legally be declared in RootType to override or implement an ancestor slot. */
	void CollectOverrideMemberCandidates(
		const FProgram& Program,
		std::string_view RootTypeName,
		std::vector<FOverrideMemberCandidate>& OutCandidates);

	const FDocumentSymbol* FindTypeSymbolInTree(
		const std::vector<FDocumentSymbol>& Symbols,
		std::string_view TypeName);

	/**

	 * Lightweight completion slot (avoids copying full FDocumentSymbol trees).

	 */
	struct FInheritedMemberCompletion
	{
		EDocumentSymbolKind Kind = EDocumentSymbolKind::Field;
		std::string Label;
		std::string Detail;
		std::string Documentation;
		std::string ValueType;
		bool bSoftBuiltin = false;
		bool bIsStatic = false;
	};

	// Match the compiler's callable identity: variadic and reference qualifiers
	// participate, while top-level const on a value parameter does not.
	std::string CallableParameterTypeName(const FFunctionParam& Parameter);

	/**
	 * Whether a member declared on DeclaringTypeName is visible from AccessContextType
	 * (empty = outside any type body → public only).
	 */
	bool IsMemberAccessibleFromContext(
		const FProgram& Program,
		std::string_view DeclaringTypeName,
		EMemberAccess Access,
		std::string_view AccessContextType,
		const FCallableAccessContext* CallableContext = nullptr);

	/**
	 * Access check for a member found on DeclaringType while completing/helping on RootType.
	 * Applies inheritance tightening, then evaluates visibility from AccessContextType.
	 */
	bool IsHierarchyMemberAccessible(
		const FProgram& Program,
		std::string_view RootTypeName,
		std::string_view DeclaringTypeName,
		EMemberAccess DeclaredAccess,
		std::string_view AccessContextType,
		const FCallableAccessContext* CallableContext = nullptr);

	/**
	 * Member completion along the inheritance chain (closer declarations hide base names;
	 * overloads declared on the same type retain independent entries).
	 * Skips constructors/destructors. Filters by effective access after inheritance.
	 */
	void AppendCompletionMembersAlongHierarchy(
		const FProgram& Program,
		const std::vector<FDocumentSymbol>& Symbols,
		std::string_view RootTypeName,
		std::string_view Prefix,
		std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel,
		std::string_view AccessContextType = {},
		const FCallableAccessContext* CallableContext = nullptr,
		bool bResolveValueTypes = false);

	/**
	 * Resolve a member identifier on RootTypeName: nearest declaring type in the hierarchy.
	 * Returns true when at least one hit was appended.
	 */
	bool CollectMemberHitsAlongHierarchy(
		const FProgram& Program,
		const std::vector<FDocumentSymbol>& Symbols,
		std::string_view RootTypeName,
		std::string_view MemberName,
		std::vector<FSymbolQueryHit>& OutHits);
}
