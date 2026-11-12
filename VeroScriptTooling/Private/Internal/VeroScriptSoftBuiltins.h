// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptTypeHierarchy.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	bool ProgramHasUserPairArity2(const FProgram& Program);
	bool ProgramHasUserHandleArity1(const FProgram& Program);

	// True for multicast_delegate / multicast_delegate<…> (including linked identities).
	bool IsSoftMulticastDelegateTypeName(std::string_view TypeName);

	// Tooling projection of compiler UFCS (`value.f(args)` -> `f(value, args)`).
	std::string FindActiveNamespaceAt(
		std::string_view Source,
		std::string_view FileLabel,
		FTextPosition Position);
	std::string ExtensionMethodLeafName(const std::string& Name);
	bool ExtensionFunctionIsVisible(
		const FProgram& Program,
		const FFunctionDecl& Function,
		std::string_view ReferenceModulePath,
		std::string_view ActiveNamespace,
		FTextPosition ReferencePosition,
		const VeroScript::IHostBindingCatalog* Bindings = nullptr);
	bool ExtensionReceiverTypeMatches(
		const FFunctionDecl& Function,
		std::string ReceiverType);
	bool ResolveExtensionMethodReturnType(
		const FFunctionDecl& Function,
		std::string ReceiverType,
		std::string& OutReturnType);
	std::string FormatExtensionMethodDetail(const FFunctionDecl& Function);

	// pair / handle / delegate / multicast_delegate / host + concurrent free functions.
	void AppendSoftBuiltinSymbols(
		const FProgram& Program,
		std::vector<FDocumentSymbol>& OutSymbols);

	// The reserved namespace tree alone, for lexical completion recovery.
	void AppendStandardNamespaceSymbols(std::vector<FDocumentSymbol>& OutSymbols);

	/**
	 * Declaration annotations (`UCLASS()` / `@rpc(...)`) the host declared in its
	 * binding catalog. The language accepts any annotation, so tooling only offers
	 * what the host registered — nothing is built in.
	 */
	void AppendHostAnnotationSymbols(
		const VeroScript::IHostBindingCatalog* Bindings,
		std::vector<FDocumentSymbol>& OutSymbols);

	// Symbol produced by AppendHostAnnotationSymbols.
	bool IsAnnotationSymbol(const FDocumentSymbol& Symbol);

	/**
	 * Append array/map/multimap/set/multiset/queue/priority_queue/stack/deque/
	 * ordered-map variants/delegate/multicast_delegate methods
	 * for TypeName into a completion map (Prefix-filtered). Substitutes T/K/V/R/A/B in
	 * Details when possible. Returned details are already specialized and must not
	 * be substituted again. Chaining mutators (`push` / `clear` / …) Detail return type
	 * matches the owner type. Does not invent channel methods (stdlib free functions).
	 * Delegate projections require Program so actual declarations take precedence;
	 * without parsed context, those uncertain members are omitted.
	 */
	void AppendSoftContainerMembers(
		std::string_view TypeName,
		std::string_view Prefix,
		std::unordered_map<std::string, FInheritedMemberCompletion>& OutMembersByLabel,
		const FProgram* Program = nullptr);

	// Element / value type for index access and foreach onto containers.
	bool ResolveContainerIndexElementType(
		std::string_view OwnerTypeName,
		std::string& OutElementType);

	/**
	 * When foreach bindings declare `auto` / `auto& [k,v]`, fill key/element types from
	 * CollectionTypeName (map variants/array/deque/set variants/queue variants/stack).
	 * Returns true if
	 * any binding was rewritten.
	 */
	bool InferForeachBindingTypes(
		std::string_view CollectionTypeName,
		bool bHasKeyBinding,
		std::string& InOutKeyTypeName,
		std::string& InOutElementTypeName);

	// Soft container method return type (`entries` → array<pair<K,V>>, …).
	bool ResolveSoftContainerMethodReturnType(
		std::string_view OwnerTypeName,
		std::string_view MethodName,
		std::string& OutReturnType,
		const FProgram* Program = nullptr);

	// Rewrite soft template Details (`T`/`U`/`K`/`V`) using type arguments of OwnerType.
	std::string SubstituteSoftTypeParams(
		std::string_view OwnerTypeName,
		std::string_view DetailTemplate);
}
