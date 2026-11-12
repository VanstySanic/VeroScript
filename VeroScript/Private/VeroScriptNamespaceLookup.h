// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptModuleVisibility.h"

namespace VeroScript
{
	// Namespace selection binds a visible prefix before looking up its members.
	// Index origins once, then reuse the resolved import graph for each caller.
	class FNamespaceLookup
	{
	public:
		explicit FNamespaceLookup(const FProgram& Program)
		{
			AddProgram(Program);
		}

		void AddProgram(const FProgram& Program)
		{
			for (const auto& Namespace : Program.Namespaces)
			{
				Add(Namespace.Name, Namespace.ModulePath, false, true);
			}
			const auto Declarations = [&](const auto& Values)
			{
				for (const auto& Value : Values)
				{
					Add(Value.Name, Value.ModulePath, Value.bExported, false);
				}
			};
			Declarations(Program.Structs);
			Declarations(Program.Classes);
			Declarations(Program.Interfaces);
			Declarations(Program.Enums);
			Declarations(Program.TypeAliases);
			Declarations(Program.Functions);
			for (const auto& Statement : Program.TopLevelStatements)
			{
				if (Statement && Statement->Kind == EStmtKind::VarDecl)
				{
					const auto& Value = static_cast<const FVarDeclStmt&>(*Statement);
					Add(Value.Name, Value.ModulePath, Value.bExported, false);
				}
			}
		}

		void Add(std::string Name, const std::string& Module, const bool bExported, const bool bNamespace)
		{
			for (std::size_t At = 0; (At = Name.find("::", At)) != std::string::npos;)
			{
				Name.replace(At, 2, ".");
				++At;
			}
			if (!bNamespace)
			{
				const auto Dot = Name.rfind('.');
				if (Dot == std::string::npos)
				{
					return;
				}
				Name.resize(Dot);
			}
			while (!Name.empty())
			{
				auto& Sources = Origins[Name];
				if (std::none_of(Sources.begin(), Sources.end(), [&](const FOrigin& Origin)
				{
					return Origin.Module == Module && Origin.bExported == bExported && Origin.bExplicit == bNamespace;
				}))
				{
					Sources.push_back({Module, bExported, bNamespace});
				}
				const auto Dot = Name.rfind('.');
				if (Dot == std::string::npos)
				{
					break;
				}
				Name.resize(Dot);
			}
		}

		bool Contains(const FProgram& Program, const std::string& Name, const std::string& Module,
		    const ModuleVisibility::FImportIndex* Imports,
		    const std::unordered_set<std::string>* VisibleModules = nullptr) const
		{
			const auto Found = Origins.find(Name);
			if (Found == Origins.end())
			{
				return false;
			}
			return std::any_of(Found->second.begin(), Found->second.end(), [&](const FOrigin& Origin)
			{
				if (VisibleModules && !Origin.bExplicit && Origin.bExported && VisibleModules->count(Origin.Module))
				{
					return true;
				}
				return ModuleVisibility::CanAccessSymbol(Program, Origin.Module,
				    !Origin.bExplicit && Origin.bExported, Module, Imports);
			});
		}

	private:
		struct FOrigin
		{
			std::string Module;
			bool bExported;
			bool bExplicit;
		};

		std::unordered_map<std::string, std::vector<FOrigin>> Origins;
	};

	template <typename TExists>
	std::vector<std::string> BindNamespacePath(const std::string& Name, std::string Scope, TExists Exists)
	{
		if (Name.rfind("::", 0) == 0)
		{
			return {Name.substr(2)};
		}
		const auto Dot = Name.find('.');
		const std::string First = Name.substr(0, Dot);
		while (!Scope.empty())
		{
			if (Exists(Scope + "." + First))
			{
				return {Scope + "." + Name};
			}
			const auto Parent = Scope.rfind('.');
			Scope = Parent == std::string::npos ? std::string{} : Scope.substr(0, Parent);
		}
		return Exists(First) ? std::vector<std::string>{Name} : std::vector<std::string>{};
	}
}
