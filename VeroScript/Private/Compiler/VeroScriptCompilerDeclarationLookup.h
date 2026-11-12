// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptBytecode.h"

namespace VeroScript
{
	// Signature expressions belong to the declaration, without caller locals.
	template<typename TScopes, typename TAliases>
	class FFunctionDeclarationLookupScope
	{
	public:
		FFunctionDeclarationLookupScope(TScopes &Scopes, TAliases &Aliases, FBytecodeFunction *&Current,
			std::string &Module, std::string &Namespace, std::string &Owner, std::string &Access,
			FSourceLocation &Location, const FFunctionDecl &Declaration)
			: Scopes(Scopes), Aliases(Aliases), Current(Current), Module(Module), Namespace(Namespace), Owner(Owner), Access(Access), Location(Location),
			SavedModule(Declaration.ModulePath), SavedNamespace(Declaration.Name.substr(0, Declaration.Name.rfind('.') == std::string::npos ? 0 : Declaration.Name.rfind('.'))),
			SavedAccess(Declaration.bExported || Declaration.ModulePath.empty() ? Declaration.Name : Declaration.ModulePath + '\x1F' + Declaration.Name),
			SavedFunction(Current), SavedLocation(Location)
		{
			Module.swap(SavedModule);
			Namespace.swap(SavedNamespace);
			Owner.swap(SavedOwner);
			Access.swap(SavedAccess);
			SavedScopes = std::move(Scopes);
			SavedAliases = std::move(Aliases);
			Current = nullptr;
			Location = Declaration.Location;
		}
		~FFunctionDeclarationLookupScope()
		{
			Module.swap(SavedModule);
			Namespace.swap(SavedNamespace);
			Owner.swap(SavedOwner);
			Access.swap(SavedAccess);
			Scopes = std::move(SavedScopes);
			Aliases = std::move(SavedAliases);
			Current = SavedFunction;
			Location = SavedLocation;
		}
		FFunctionDeclarationLookupScope(const FFunctionDeclarationLookupScope &) = delete;
		FFunctionDeclarationLookupScope &operator=(const FFunctionDeclarationLookupScope &) = delete;
	private:
		TScopes &Scopes;
		TAliases &Aliases;
		FBytecodeFunction *&Current;
		std::string &Module, &Namespace, &Owner, &Access;
		FSourceLocation &Location;
		std::string SavedModule, SavedNamespace, SavedOwner, SavedAccess;
		FBytecodeFunction *SavedFunction;
		FSourceLocation SavedLocation;
		TScopes SavedScopes;
		TAliases SavedAliases;
	};
}
