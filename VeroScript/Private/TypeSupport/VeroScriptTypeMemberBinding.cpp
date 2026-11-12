// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTypeMemberBinding.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptMethodKey.h"

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <type_traits>

namespace VeroScript
{
	std::string SubstituteScopedTypeParameters(const std::string_view Text,
											   const std::vector<std::string>& Parameters,
											   const std::vector<std::string>& Arguments)
	{
		std::string Result;
		std::size_t HeadEnd = 0;
		bool Opaque = false;
		const auto Identifier = [](const char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' ||
				   C == '$';
		};
		for (std::size_t Index = 0; Index < Text.size();)
		{
			if (Index >= HeadEnd)
			{
				HeadEnd = Text.find_first_of("<>,()", Index);
				if (HeadEnd == std::string_view::npos)
				{
					HeadEnd = Text.size();
				}
				Opaque = Text.substr(Index, HeadEnd - Index).find('\x1F') != std::string_view::npos;
			}
			if (!Identifier(Text[Index]))
			{
				Result += Text[Index++];
				continue;
			}
			const auto Start = Index++;
			while (Index < Text.size() && Identifier(Text[Index]))
			{
				++Index;
			}
			const auto Name = std::string(Text.substr(Start, Index - Start));
			const auto Found = std::find(Parameters.begin(), Parameters.end(), Name);
			const auto Argument = static_cast<std::size_t>(Found - Parameters.begin());
			const bool Qualified = Start > 0 && (Text[Start - 1] == '.' || Text[Start - 1] == ':');
			Result += !Opaque && !Qualified && Argument < Arguments.size() ? Arguments[Argument] : Name;
		}
		return Result;
	}

	std::string CallableScopeName(const std::string& Owner, const std::string& Name,
								  const std::vector<FFunctionParam>& Parameters, const bool Const)
	{
		std::string Result = (Owner.empty() || Name.rfind(Owner + ".", 0) == 0 ? Name : Owner + "." + Name) + "(";
		for (const auto& Parameter : Parameters)
		{
			Result += std::to_string(Parameter.TypeName.size()) + ":" + Parameter.TypeName;
			Result += Parameter.bIsRef ? (Parameter.bIsConst ? " const ref;" : " ref;") : ";";
		}
		return Result + (Const ? ")const" : ")");
	}
	std::string MakeTypeMemberLookupKey(const std::string& Module, const std::string& Scope,
		const std::string& Owner, const std::string& Member)
	{
		std::string Result;
		for (const auto* Part : {&Module, &Scope, &Owner, &Member})
		{
			Result += std::to_string(Part->size()) + ':' + *Part;
		}
		return Result;
	}

	FTypeMemberBinding FindTypeMember(const FProgram& Program, const FProgram* OtherProgram,
									  const std::string& Owner, const std::vector<std::string>& Arguments,
									  const std::string& Member, const std::string& ReferenceModule,
									  const FTypeMemberOwnerDeclarations* OpenDeclarations,
									  const FTypeMemberLookupView* Lookup)
	{
		struct FOwner
		{
			std::string Name, Module, Linkage, Id;
			std::vector<std::string> Parameters, Bases;
			std::vector<EMemberAccess> BaseAccess;
			std::vector<FUsingMemberDecl> Using;
		};
		const auto FindOwner = [&](const std::string& Name, const std::string& Module,
								   const std::size_t Arity) -> FOwner
		{
			FOwner Result;
			const auto Inspect = [&](const auto& Declaration)
			{
				if (Declaration.Name != Name || TemplateParameterNames(Declaration).size() != Arity)
				{
					return;
				}
				if (!Result.Name.empty() && Result.Module == Module && Declaration.ModulePath != Module)
				{
					return;
				}
				Result.Name = Declaration.Name;
				Result.Module = Declaration.ModulePath;
				Result.Id = Declaration.Scope.Id;
				Result.Linkage = Declaration.Name;
				if (!Declaration.bExported && !Declaration.ModulePath.empty())
				{
					std::string Path = Declaration.ModulePath;
					std::replace(Path.begin(), Path.end(), '\\', '/');
					Result.Linkage = Path + '\x1F' + Declaration.Name;
				}
				Result.Parameters = TemplateParameterNames(Declaration);
				using T = std::decay_t<decltype(Declaration)>;
				if constexpr (std::is_same_v<T, FClassDecl>)
				{
					Result.Bases = Declaration.BaseTypeNames;
					Result.BaseAccess = Declaration.BaseInheritanceAccess;
				}
				else if constexpr (std::is_same_v<T, FStructDecl>)
				{
					Result.Bases = Declaration.BaseTypeName.empty()
									   ? std::vector<std::string>{}
									   : std::vector<std::string>{Declaration.BaseTypeName};
					Result.BaseAccess = {Declaration.BaseInheritanceAccess};
				}
				else
				{
					Result.Bases = Declaration.BaseInterfaceNames;
					Result.BaseAccess.assign(Result.Bases.size(), EMemberAccess::Public);
				}
				if constexpr (!std::is_same_v<T, FInterfaceDecl>)
				{
					Result.Using = Declaration.UsingMembers;
				}
			};
			const auto Find = [&](const FProgram& Target, const auto& Declarations,
				const ETypeMemberDeclarationKind Kind)
			{
				const auto Visit = [&](const auto& Declaration)
				{
					// Keep the common non-match path outside the large owner projection.
					if (Declaration.Name == Name && TemplateParameterNames(Declaration).size() == Arity)
					{
						Inspect(Declaration);
					}
				};
				if (Lookup && Lookup->Owners)
				{
					for (const auto Position : Lookup->Owners(Lookup->Context, Target, Kind, Name))
					{
						Visit(Declarations[Position]);
					}
				}
				else
				{
					for (const auto& Declaration : Declarations)
					{
						Visit(Declaration);
					}
				}
			};
			const auto Search = [&](const FProgram& P)
			{
				Find(P, P.Classes, ETypeMemberDeclarationKind::Class);
				Find(P, P.Structs, ETypeMemberDeclarationKind::Struct);
				Find(P, P.Interfaces, ETypeMemberDeclarationKind::Interface);
			};
			if (OpenDeclarations)
			{
				if (OpenDeclarations->Struct && OpenDeclarations->Struct->ModulePath == Module)
				{
					Inspect(*OpenDeclarations->Struct);
				}
				if (OpenDeclarations->Class && OpenDeclarations->Class->ModulePath == Module)
				{
					Inspect(*OpenDeclarations->Class);
				}
				if (OpenDeclarations->Interface && OpenDeclarations->Interface->ModulePath == Module)
				{
					Inspect(*OpenDeclarations->Interface);
				}
				if (!Result.Name.empty()) return Result;
			}
			if (OtherProgram)
			{
				Search(*OtherProgram);
			}
			Search(Program);
			return Result;
		};
		std::unordered_set<std::string> Active;
		std::function<FTypeMemberBinding(const FOwner&, const std::vector<std::string>&, std::size_t)> Resolve;
		Resolve = [&](const FOwner& Current, const std::vector<std::string>& Values, const std::size_t Depth)
		{
			FTypeMemberBinding Result;
			if (Current.Name.empty() || Depth >= 64 || !Active.insert(Current.Linkage).second)
			{
				return Result;
			}
			struct FGuard
			{
				std::unordered_set<std::string>& Set;
				const std::string& Key;
				~FGuard()
				{
					Set.erase(Key);
				}
			} Guard{Active, Current.Linkage};
			CheckAnalysisCancellation();
			const auto Find = [&](const FProgram& Target, const auto& Declarations,
				const ETypeMemberDeclarationKind Kind)
			{
				const auto Visit = [&](const auto& Child)
				{
					if (Child.ModulePath == Current.Module && Child.Scope.ScopeId == Current.Id &&
						Child.Scope.OwnerTypeName == Current.Name && Child.Scope.SimpleName == Member)
					{
						Result.Name = Child.Name;
						Result.ModulePath = Child.ModulePath;
						Result.OwnerArguments = Values;
						Result.AccessChecks = {
							{true, Child.Scope.Access, Current.Linkage, Current.Parameters.size()}};
					}
				};
				if (Lookup && Lookup->Children)
				{
					for (const auto Position : Lookup->Children(Lookup->Context, Target, Kind,
						Current.Module, Current.Id, Current.Name, Member))
					{
						Visit(Declarations[Position]);
					}
				}
				else
				{
					for (const auto& Child : Declarations)
					{
						Visit(Child);
					}
				}
			};
			const auto Search = [&](const FProgram& P)
			{
				Find(P, P.Classes, ETypeMemberDeclarationKind::Class);
				Find(P, P.Structs, ETypeMemberDeclarationKind::Struct);
				Find(P, P.Interfaces, ETypeMemberDeclarationKind::Interface);
				Find(P, P.Enums, ETypeMemberDeclarationKind::Enum);
				Find(P, P.TypeAliases, ETypeMemberDeclarationKind::Alias);
			};
			if (OtherProgram)
			{
				Search(*OtherProgram);
			}
			Search(Program);
			if (!Result.Name.empty())
			{
				return Result;
			}
			for (std::size_t BaseIndex = 0; BaseIndex < Current.Bases.size(); ++BaseIndex)
			{
				std::vector<FTypePathSegment> Base;
				if (!ParseTypePath(Current.Bases[BaseIndex], Base))
				{
					continue;
				}
				std::string BaseName;
				std::vector<std::string> BaseValues;
				for (const auto& Part : Base)
				{
					if (!BaseName.empty())
					{
						BaseName += '.';
					}
					BaseName += Part.Name;
					for (const auto& Argument : Part.Arguments)
					{
						BaseValues.push_back(SubstituteScopedTypeParameters(Argument, Current.Parameters, Values));
					}
				}
				auto Found =
					Resolve(FindOwner(BaseName, Current.Module, BaseValues.size()), BaseValues, Depth + 1);
				if (Found.Name.empty())
				{
					continue;
				}
				if (Found.bAmbiguous)
				{
					return Found;
				}
				bool Promoted = false;
				for (const auto& Using : Current.Using)
				{
					if (Using.MemberName != Member)
					{
						continue;
					}
					std::string UsingBase = Using.BaseTypeName;
					std::vector<FTypePathSegment> Parts;
					if (ParseTypePath(UsingBase, Parts))
					{
						UsingBase.clear();
						for (const auto& Part : Parts)
						{
							if (!UsingBase.empty())
							{
								UsingBase += '.';
							}
							UsingBase += Part.Name;
						}
					}
					const auto& Declaring = Found.AccessChecks.front().DeclaringOwnerLinkage;
					if (!UsingBase.empty() && UsingBase != BaseName &&
						Declaring.substr(FindLinkageIdentityStart(Declaring)) != UsingBase)
					{
						continue;
					}
					std::string Context = Current.Linkage;
					for (std::size_t I = 0; I < Current.Parameters.size(); ++I)
					{
						const auto& Value = I < Values.size() ? Values[I] : Current.Parameters[I];
						Context += (I == 0 ? "#" : "") + std::to_string(Value.size()) + ":" + Value;
					}
					bool Allowed = true;
					for (const auto& Check : Found.AccessChecks)
					{
						Allowed = Allowed && CanAccessMemberFromContext(
												 Check, Context, OtherProgram ? *OtherProgram : Program, {}, true);
					}
					if (Allowed)
					{
						Found.AccessChecks = {{true, Using.Access, Current.Linkage, Current.Parameters.size()}};
						Promoted = true;
					}
				}
				const auto Inheritance =
					BaseIndex < Current.BaseAccess.size() ? Current.BaseAccess[BaseIndex] : EMemberAccess::Public;
				if (!Promoted && Inheritance != EMemberAccess::Public)
				{
					Found.AccessChecks.push_back({true, Inheritance, Current.Linkage, Current.Parameters.size()});
				}
				if (!Result.Name.empty() && (Result.Name != Found.Name || Result.ModulePath != Found.ModulePath ||
											 Result.OwnerArguments != Found.OwnerArguments))
				{
					Result.bAmbiguous = true;
					return Result;
				}
				Result = std::move(Found);
			}
			return Result;
		};
		return Resolve(FindOwner(Owner, ReferenceModule, Arguments.size()), Arguments, 0);
	}
} // namespace VeroScript
