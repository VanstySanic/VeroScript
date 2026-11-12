// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace VeroScript
{
	struct FFunctionDecl;
	struct FProgram;
	struct FNativeFunction;
	class IHostBindingCatalog;
}

namespace VeroScript::StandardLibrary
{
	/**
	 * A built-in type alias or a name exported by the reserved `std` namespace.
	 *
	 * QualifiedName uses the parser's normalized dotted spelling. CanonicalName is
	 * the existing language/runtime identity. Keeping those identities separate
	 * makes `std::array<T>` a zero-cost spelling of `array<T>` instead of a second
	 * container ABI.
	 */
	struct FNameAlias
	{
		std::string_view QualifiedName;
		std::string_view CanonicalName;
	};

	// Reserved implementation markers survive package path sanitization. A name
	// alone never makes a user function a standard-library compiler factory.
	bool IsMethodBindingFactoryDeclaration(const FFunctionDecl& Function);
	bool IsAttributeImplementationModule(const FProgram& Program, std::string_view ModulePath);

	// Resolve built-in aliases (`shared`) and `std::` / `std.` type spellings.
	std::string_view ResolveTypeName(std::string_view QualifiedName);

	// Resolve a standard-namespace function spelling to its existing callable.
	std::string_view ResolveFunctionName(std::string_view QualifiedName);

	/**
	 * Return the required public `std` spelling for a builtin that is deliberately
	 * excluded from the global namespace, or an empty view for an ordinary global.
	 */
	std::string_view RequiredNamespaceForFunction(std::string_view CanonicalName);

	// True for `std` and for nested standard namespaces such as `std::chrono`.
	bool IsNamespace(std::string_view Name);

	// True when a declaration would add names to the reserved global `std` tree.
	bool IsReservedDeclarationNamespace(std::string_view QualifiedName);

	const FNameAlias* GetTypeAliases(std::size_t& OutCount);
	const FNameAlias* GetFunctionAliases(std::size_t& OutCount);

	// One canonical core-native table for compilation, execution and portable calls.
	const IHostBindingCatalog& GetBindingCatalog();
	// Runtime signature/callback/provider identity; ancillary ownership leases may differ.
	bool NativeFunctionsEquivalent(const FNativeFunction&, const FNativeFunction&);
	std::shared_ptr<const IHostBindingCatalog> MergeBindingCatalog(
		const IHostBindingCatalog* Source, const std::vector<FNativeFunction>* Natives);
	const std::vector<FNativeFunction>* MergeNativeFunctions(
		const std::vector<FNativeFunction>* Natives, std::vector<FNativeFunction>& Storage);
}
