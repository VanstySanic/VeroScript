// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptModuleVisibility.h"
#include "ModuleSupport/VeroScriptScopedBindingCatalog.h"
#include <unordered_set>
#include <cctype>

namespace VeroScript::Tooling::Detail
{
	// Mirror the compiler's declaration visibility, including types exposed by
	// public signatures. Query enumeration must not flatten all providers globally.
	class FBindingVisibility
	{
	public:
		FBindingVisibility(const FProgram& P, const IHostBindingCatalog& B, std::string_view Module = {})
			: Program(P), Bindings(B), From(Module.empty() ? P.EntryModulePath : std::string(Module))
		{
			if (!P.ModuleEnvironment) return;
			const auto Captured = P.ModuleSourceOwners.find(From);
			Owner = Captured == P.ModuleSourceOwners.end() ? P.ModuleEnvironment->GetSourceOwner(From)
														   : Captured->second;
			for (const auto& S : P.PackageSourceModules)
			{
				if (S.bFromPackage && S.ModulePath == From)
				{
					bPackage = true;
					if (!S.SourceOwnerId.empty())
					{
						Owner = S.SourceOwnerId;
					}
					break;
				}
			}

			ModuleVisibility::CollectVisibleModulePaths(P.Imports, From, VisibleModules);
		}
		bool Allows(const FBindingOrigin& Origin, std::string Type = {})
		{
			if (!Program.ModuleEnvironment || Origin.ProviderId.empty()) return true;
			const auto& E = *Program.ModuleEnvironment;
			const bool Visible = !Origin.ModulePath.empty() && ModuleVisibility::CanAccessSymbol(Program, Origin.ModulePath, Origin.bExported, From);
			bool Allowed = Visible || (E.CanAccessOwner(Owner, Origin.ProviderId) &&
				(Origin.ModulePath.empty() || bPackage || Origin.ModulePath == From));
			if (!Allowed && Origin.bExported)
			{
				for (const auto& M : VisibleModules)
				{
					if (E.GetSourceOwner(M) == Origin.ProviderId)
					{
						Allowed = true;
						break;
					}
				}
			}

			if (!Origin.bExported && Owner != Origin.ProviderId)
			{
				Allowed = false;
			}

			if (!Allowed && !Type.empty())
			{
				if (!bExposureBuilt)
				{
					BuildExposure();
				}

				Type.resize(Type.find('<') == std::string::npos ? Type.size() : Type.find('<'));
				for (std::size_t P = 0; (P = Type.find("::", P)) != std::string::npos;)
				{
					Type.replace(P, 2, ".");
				}

				Allowed = ExposedTypes.count(Type) != 0;
			}
			return Allowed && (Origin.bExported || Owner == Origin.ProviderId || Visible);
		}

	private:
		static void Tokens(const std::string& Text, std::unordered_set<std::string>& Into)
		{
			const auto Part = [](unsigned char C)
			{
				return C >= 128 || std::isalnum(C) || C == '_' || C == ':' || C == '.';
			};
			for (std::size_t I = 0; I < Text.size();)
			{
				if (!Part(static_cast<unsigned char>(Text[I])))
				{
					++I;
					continue;
				}
				const auto Begin = I;
				while (I < Text.size() && Part(static_cast<unsigned char>(Text[I])))
				{
					++I;
				}

				auto Token = Text.substr(Begin, I - Begin);
				Into.insert(Token);
				for (std::size_t P = 0; (P = Token.find("::", P)) != std::string::npos;)
				{
					Token.replace(P, 2, ".");
				}

				Into.insert(std::move(Token));
			}
		}
		void BuildExposure()
		{
			bExposureBuilt = true;
			const auto Signature = [&](const auto& F)
			{
				Tokens(F.ReturnType, ExposedTypes);
				for (const auto& P : F.Parameters)
				{
					Tokens(P.TypeName, ExposedTypes);
				}
			};
			const auto Visible = [&](const auto& D)
			{
				return D.ModulePath != From &&
					ModuleVisibility::CanAccessSymbol(Program, D.ModulePath, D.bExported, From);
			};
			for (const auto& F : Program.Functions)
			{
				if (Visible(F))
				{
					Signature(F);
				}
			}

			const auto Types = [&](const auto& Declarations)
			{
				for (const auto& D : Declarations)
				{
					if (Visible(D))
					{
						for (const auto& F : D.Fields)
						{
							if (F.Access == EMemberAccess::Public)
							{
								Tokens(F.TypeName, ExposedTypes);
							}
						}

						for (const auto& F : D.Methods)
						{
							if (F.Access == EMemberAccess::Public)
							{
								Signature(F);
							}
						}
					}
				}
			};
			Types(Program.Classes);
			Types(Program.Structs);
			Types(Program.Interfaces);
			for (const auto& A : Program.TypeAliases)
			{
				if (Visible(A))
				{
					Tokens(A.TypeName, ExposedTypes);
				}
			}

			for (const auto& F : Bindings.GetNativeFunctions())
			{
				if (!F.Name.empty() && F.Name.front() != '\x1f' &&
					(F.Origin.ProviderId.empty() ||
						Program.ModuleEnvironment->CanAccessOwner(Owner, F.Origin.ProviderId)) &&
					(F.Origin.ModulePath.empty() ||
						ModuleVisibility::CanAccessSymbol(
							Program, F.Origin.ModulePath, F.Origin.bExported, From)))
				{
					Tokens(F.ReturnHostTypeName, ExposedTypes);
					Tokens(F.ReturnElementTypeName, ExposedTypes);
					Tokens(F.ReturnKeyTypeName, ExposedTypes);
					Tokens(F.ReturnValueTypeName, ExposedTypes);
					for (const auto* Metadata : {&F.ParamHostTypeNames, &F.ParamElementTypeNames,
							 &F.ParamKeyTypeNames, &F.ParamValueTypeNames})
					{
						for (const auto& T : *Metadata)
						{
							Tokens(T, ExposedTypes);
						}
					}
				}
			}

			for (unsigned Pass = 0; Pass < 64; ++Pass)
			{
				const auto Count = ExposedTypes.size();
				for (const auto& A : Program.TypeAliases)
				{
					if (ExposedTypes.count(A.Name))
					{
						Tokens(A.TypeName, ExposedTypes);
					}
				}

				for (const auto& A : Bindings.GetTypeAliases())
				{
					if (ExposedTypes.count(A.Name))
					{
						Tokens(A.TargetTypeName, ExposedTypes);
					}
				}

				if (Count == ExposedTypes.size())
				{
					break;
				}
			}
		}
		const FProgram& Program;
		const IHostBindingCatalog& Bindings;
		std::string From, Owner;
		std::unordered_set<std::string> VisibleModules, ExposedTypes;
		bool bExposureBuilt = false, bPackage = false;
	};
	inline std::shared_ptr<const IHostBindingCatalog> ScopedQueryBindings(
		const FProgram& Program, const IHostBindingCatalog* Bindings, std::string_view Module = {})
	{
		std::shared_ptr<const IHostBindingCatalog> Source(std::shared_ptr<const IHostBindingCatalog>{}, Bindings);
		if (!Program.ModuleEnvironment || !Bindings) return Source;
		if (dynamic_cast<const FScopedBindingCatalog*>(Bindings)) return Source;
		auto Visibility = std::make_shared<FBindingVisibility>(Program, *Bindings, Module);
		return std::make_shared<FScopedBindingCatalog>(std::move(Source),
			[Visibility](const FBindingOrigin& Origin, const std::string& Type)
			{
				return Visibility->Allows(Origin, Type);
			});
	}
}
