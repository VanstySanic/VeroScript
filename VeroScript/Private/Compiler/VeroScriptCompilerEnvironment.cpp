// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptModuleVisibility.h"
#include <algorithm>

namespace VeroScript
{
	namespace
	{
		void Tokens(const std::string& S, std::unordered_set<std::string>& Into)
		{
			auto Part = [](unsigned char C)
			{
				return C >= 128 || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
					(C >= '0' && C <= '9') || C == '_' || C == ':' || C == '.';
			};
			for (std::size_t I = 0; I < S.size();)
			{
				if (!Part(static_cast<unsigned char>(S[I])))
				{
					++I;
					continue;
				}
				const auto Begin = I;
				while (I < S.size() && Part(static_cast<unsigned char>(S[I])))
				{
					++I;
				}

				auto Token = S.substr(Begin, I - Begin);
				Into.insert(Token);
				for (std::size_t P = 0; (P = Token.find("::", P)) != std::string::npos;)
				{
					Token.replace(P, 2, ".");
				}

				Into.insert(std::move(Token));
			}
		}
	}
	bool VeroScriptCompiler::CanAccessNativeBinding(const FBindingOrigin& Origin, const std::string& Type) const
	{
		if (!ActiveProgram || !ActiveProgram->ModuleEnvironment || Origin.ProviderId.empty()) return true;
		const auto& Environment = *ActiveProgram->ModuleEnvironment;
		const auto& From = CurrentModulePath.empty() ? ActiveProgram->EntryModulePath : CurrentModulePath;
		const auto CapturedOwner = ActiveProgram->ModuleSourceOwners.find(From);
		auto Owner = CapturedOwner == ActiveProgram->ModuleSourceOwners.end() ? Environment.GetSourceOwner(From) : CapturedOwner->second;
		bool Package = false;
		for (const auto& S : ActiveProgram->PackageSourceModules)
		{
			if (S.bFromPackage && S.ModulePath == From)
			{
				Package = true;
				if (!S.SourceOwnerId.empty())
				{
					Owner = S.SourceOwnerId;
				}
				break;
			}
		}

		const bool Visible = !Origin.ModulePath.empty() &&
			CanAccessSymbol(Origin.ModulePath, Origin.bExported, From);
		bool Allowed = Visible || (Environment.CanAccessOwner(Owner, Origin.ProviderId) &&
			(Origin.ModulePath.empty() || Package || Origin.ModulePath == From));
		if (!Allowed && Origin.bExported)
		{
			for (const auto& M : ActiveImportIndex->VisibleFrom(From))
			{
				if (Environment.GetSourceOwner(M) == Origin.ProviderId)
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
			auto Found = NativeExposure.find(From);
			if (Found == NativeExposure.end())
			{
				std::unordered_set<std::string> Values;
				auto Signature = [&](const auto& F)
				{
					Tokens(F.ReturnType, Values);
					for (const auto& P : F.Parameters)
					{
						Tokens(P.TypeName, Values);
					}
				};
				for (const auto& F : ActiveProgram->Functions)
				{
					if (F.ModulePath != From &&
						CanAccessSymbol(F.ModulePath, F.bExported, From))
					{
						Signature(F);
					}
				}

				auto Types = [&](const auto& Ds)
				{
					for (const auto& D : Ds)
					{
						if (D.ModulePath != From &&
							CanAccessSymbol(D.ModulePath, D.bExported, From))
						{
							for (const auto& F : D.Fields)
							{
								if (F.Access == EMemberAccess::Public)
								{
									Tokens(F.TypeName, Values);
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
				Types(ActiveProgram->Classes);
				Types(ActiveProgram->Structs);
				Types(ActiveProgram->Interfaces);
				for (const auto& A : ActiveProgram->TypeAliases)
				{
					if (A.ModulePath != From &&
						CanAccessSymbol(A.ModulePath, A.bExported, From))
					{
						Tokens(A.TypeName, Values);
					}
				}

				for (const auto& F : StandardBindings->GetNativeFunctions())
				{
					if ((!F.Name.empty() && F.Name.front() != '\x1f') &&
						(F.Origin.ProviderId.empty() ||
							Environment.CanAccessOwner(Owner, F.Origin.ProviderId)) &&
						(F.Origin.ModulePath.empty() ||
							CanAccessSymbol(F.Origin.ModulePath, F.Origin.bExported, From)))
					{
						Tokens(F.ReturnHostTypeName, Values);
						Tokens(F.ReturnElementTypeName, Values);
						Tokens(F.ReturnKeyTypeName, Values);
						Tokens(F.ReturnValueTypeName, Values);
						for (const auto* Ts : {&F.ParamHostTypeNames, &F.ParamElementTypeNames,
								 &F.ParamKeyTypeNames, &F.ParamValueTypeNames})
						{
							for (const auto& T : *Ts)
							{
								Tokens(T, Values);
							}
						}
					}
				}

				for (unsigned Pass = 0; Pass < 64; ++Pass)
				{
					const auto Before = Values.size();
					for (const auto& A : ActiveProgram->TypeAliases)
					{
						if (Values.count(A.Name))
						{
							Tokens(A.TypeName, Values);
						}
					}

					for (const auto& A : StandardBindings->GetTypeAliases())
					{
						if (Values.count(A.Name))
						{
							Tokens(A.TargetTypeName, Values);
						}
					}

					if (Before == Values.size())
					{
						break;
					}
				}
				Found = NativeExposure.emplace(From, std::move(Values)).first;
			}
			// Generic arguments (for example int) cannot grant visibility to
			// their enclosing native type. Only its own qualified name can.
			auto Root = Type.substr(0, Type.find('<'));
			for (std::size_t P = 0; (P = Root.find("::", P)) != std::string::npos;)
			{
				Root.replace(P, 2, ".");
			}

			if (Found->second.count(Root))
			{
				Allowed = true;
			}
		}
		if (!Origin.bExported && Owner != Origin.ProviderId && !Visible)
		{
			Allowed = false;
		}

		if (!Allowed)
		{
			ReportError("native declaration is outside the declared dependency/visibility scope: " + Origin.ProviderId, CurrentEmitLocation);
			return false;
		}
		const auto& Available = Environment.GetDescription().AvailableDependencies;
		const auto D = std::find_if(Available.begin(), Available.end(),
			[&](const auto& V)
			{
				return V.Id == Origin.ProviderId;
			});
		if (D != Available.end())
		{
			std::string Failure;
			if (!MergeExternalDependencies(Module->RequiredExternalDependencies, {*D}, Failure))
			{
				ReportError(Failure, CurrentEmitLocation);
				return false;
			}
		}
		return true;
	}
}
