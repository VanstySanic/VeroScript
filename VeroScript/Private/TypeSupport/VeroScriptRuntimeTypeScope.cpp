// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRuntimeTypeScope.h"
#include "VeroScriptAst.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptAnalysisControl.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>

namespace VeroScript
{
	bool ValidateRuntimeTypeScopes(const FBytecodeModule& Module, std::string& Error)
	{
		std::unordered_map<std::string, const FTypeDeclarationScope*> Declarations;
		std::size_t Bytes = 0;
		const auto Check = [&](const FTypeDeclarationScope& Scope)
		{
			if (Scope.Id.empty())
			{
				return Scope.OwnerTypeName.empty() && !Scope.bLocal;
			}
			if (Scope.SimpleName.empty() || Scope.QualifiedName.empty() || Scope.ScopeId.empty() ||
				Scope.ParentScopeId == Scope.Id || Scope.OwnTypeParameterCount > 65535
				|| Scope.OwnTemplateParameterCount > 65535 ||
				(!Scope.OwnerTypeName.empty() && Scope.Access != EMemberAccess::Public && Scope.bPublicPath) ||
				static_cast<unsigned>(Scope.Access) > static_cast<unsigned>(EMemberAccess::Private) ||
				(Scope.Kind != "class" && Scope.Kind != "struct" && Scope.Kind != "interface" &&
				 Scope.Kind != "enum"))
			{
				Error = "invalid type declaration scope";
				return false;
			}
			for (const auto* Value :
				 {&Scope.Id, &Scope.ScopeId, &Scope.ParentScopeId, &Scope.SimpleName, &Scope.QualifiedName,
				  &Scope.OwnerTypeName, &Scope.OwnerFunctionName, &Scope.AccessTypeName, &Scope.AccessModulePath,
				  &Scope.ModulePath, &Scope.Kind, &Scope.DisplayName})
			{
				if (Value->size() > 64u * 1024u * 1024u - Bytes || Value->find('\0') != std::string::npos)
				{
					Error = "type scope metadata exceeds safety limit";
					return false;
				}
				Bytes += Value->size();
			}
			for (const auto* Values : {&Scope.EnclosingTypeParameters, &Scope.FunctionTypeParameters,
									   &Scope.ConcreteTypeArguments, &Scope.ForbiddenRuntimeNames})
			{
				if (Values->size() > 65536)
				{
					Error = "type scope collection exceeds safety limit";
					return false;
				}
				for (const auto& Value : *Values)
				{
					if (Value.size() > 64u * 1024u * 1024u - Bytes || Value.find('\0') != std::string::npos)
					{
						Error = "type scope metadata exceeds safety limit";
						return false;
					}
					Bytes += Value.size();
				}
			}
			const auto Existing = Declarations.emplace(Scope.Id, &Scope);
			if (!Existing.second && (Existing.first->second->QualifiedName != Scope.QualifiedName ||
									 Existing.first->second->Kind != Scope.Kind ||
									 Existing.first->second->ParentScopeId != Scope.ParentScopeId ||
									 Existing.first->second->OwnTypeParameterCount != Scope.OwnTypeParameterCount))
			{
				Error = "conflicting type declaration identity";
				return false;
			}
			return true;
		};
		const auto Validate = [&](const auto& Types)
		{
			for (const auto& Type : Types)
			{
				if (!Check(Type.Scope))
				{
					return false;
				}
				if constexpr (!std::is_same_v<std::decay_t<decltype(Type)>, FBytecodeEnumDesc>)
				{
					std::unordered_set<std::string> Names;
					for (const auto& Child : Type.NestedTypes)
					{
						if (!Check(Child.Scope) || !Names.insert(Child.Scope.SimpleName).second ||
							Child.Scope.OwnerTypeName != Type.Scope.QualifiedName ||
							Child.Scope.ScopeId != Type.Scope.Id || Child.Scope.bLocal != Type.Scope.bLocal)
						{
							Error = "invalid nested type declaration catalog";
							return false;
						}
					}
				}
			}
			return true;
		};
		if (!Validate(Module.Types) || !Validate(Module.Interfaces) || !Validate(Module.Enums))
		{
			return false;
		}
		for (const auto& Declaration : Declarations)
		{
			std::unordered_set<std::string> Visited;
			const auto* Scope = Declaration.second;
			while (Scope)
			{
				if (!Visited.insert(Scope->Id).second || Visited.size() > 256)
				{
					Error = "cyclic type declaration scope";
					return false;
				}
				const auto Parent = Declarations.find(Scope->ParentScopeId);
				Scope = Parent == Declarations.end() ? nullptr : Parent->second;
			}
		}
		return true;
	}

	void PopulateRuntimeTypeScope(const FProgram& Program, const FTypeDeclarationScope& Declaration,
								  const std::vector<std::string>& Arguments, FTypeDeclarationScope& Scope,
								  std::vector<FNestedTypeDeclaration>& NestedTypes,
		const FRuntimeTypeScopeLookupView* Lookup)
	{
		CheckAnalysisCancellation();
		Scope = Declaration;
		if (Declaration.Id.empty())
		{
			return;
		}
		const auto ForCandidates = [&](const auto& Declarations, const ERuntimeTypeDeclarationKind Kind,
			const ERuntimeTypeLookupKind Query, const std::string& Key, const auto& Visit)
		{
			if (Lookup && Lookup->Candidates)
			{
				for (const auto Position : Lookup->Candidates(Lookup->Context, Kind, Query, Key))
				{
					CheckAnalysisCancellation();
					if (Visit(Declarations[Position])) return;
				}
			}
			else
			{
				for (const auto& Candidate : Declarations)
				{
					CheckAnalysisCancellation();
					if (Visit(Candidate)) return;
				}
			}
		};
		Scope.bPublicPath = Declaration.OwnerTypeName.empty() || Declaration.Access == EMemberAccess::Public;
		std::string ParentId = Declaration.ScopeId;
		for (std::size_t Depth = 0; Depth < 64 && !ParentId.empty(); ++Depth)
		{
			CheckAnalysisCancellation();
			const FTypeDeclarationScope* Parent = nullptr;
			const auto FindParent = [&](const auto& Declarations, const ERuntimeTypeDeclarationKind Kind)
			{
				ForCandidates(Declarations, Kind, ERuntimeTypeLookupKind::Scope, ParentId, [&](const auto& Candidate)
				{
					if (Candidate.Scope.Id == ParentId)
					{
						Parent = &Candidate.Scope;
					}
					return false;
				});
			};
			FindParent(Program.Classes, ERuntimeTypeDeclarationKind::Class);
			FindParent(Program.Structs, ERuntimeTypeDeclarationKind::Struct);
			FindParent(Program.Interfaces, ERuntimeTypeDeclarationKind::Interface);
			if (!Parent)
			{
				break;
			}
			if (!Parent->OwnerTypeName.empty() && Parent->Access != EMemberAccess::Public)
			{
				Scope.bPublicPath = false;
			}
			ParentId = Parent->ScopeId;
		}

		Scope.ConcreteTypeArguments = Arguments;
		std::function<std::string(const FTypeDeclarationScope&, std::size_t)> Display;
		Display = [&](const FTypeDeclarationScope& Source, const std::size_t Depth) -> std::string
		{
			if (Depth >= 64)
			{
				return Source.SimpleName;
			}
			std::string Result;
			const auto Find = [&](const auto& Declarations, const ERuntimeTypeDeclarationKind Kind)
			{
				ForCandidates(Declarations, Kind, ERuntimeTypeLookupKind::Name, Source.OwnerTypeName, [&](const auto& Candidate)
				{
					if (Candidate.Name == Source.OwnerTypeName && Candidate.ModulePath == Source.ModulePath &&
						Candidate.Scope.Id == Source.ScopeId)
					{
						Result = Display(Candidate.Scope, Depth + 1) + "::" + Source.SimpleName;
						return true;
					}
					return false;
				});
			};
			if (!Source.OwnerTypeName.empty())
			{
				Find(Program.Classes, ERuntimeTypeDeclarationKind::Class);
				Find(Program.Structs, ERuntimeTypeDeclarationKind::Struct);
				Find(Program.Interfaces, ERuntimeTypeDeclarationKind::Interface);
			}
			if (Result.empty())
			{
				Result = Source.bLocal
							 ? Source.OwnerFunctionName + "::" + Source.SimpleName + " [" + Source.ScopeId + "]"
							 : Source.QualifiedName;
			}
			const auto Start = EnclosingTemplateParameterNames(Source).size();
			if (OwnTemplateParameterCount(Source) != 0 && Start < Arguments.size())
			{
				Result += '<';
				for (std::size_t I = 0; I < OwnTemplateParameterCount(Source) && Start + I < Arguments.size(); ++I)
				{
					if (I != 0)
					{
						Result += ',';
					}
					Result += FormatTemplateArgumentName(Arguments[Start + I]);
				}
				Result += '>';
			}
			return Result;
		};
		Scope.DisplayName = Display(Declaration, 0);
		const auto Owner = [&](const auto& Declarations, const ERuntimeTypeDeclarationKind Kind)
		{
			ForCandidates(Declarations, Kind, ERuntimeTypeLookupKind::Name, Declaration.OwnerTypeName, [&](const auto& Candidate)
			{
				if (Candidate.Name != Declaration.OwnerTypeName ||
					Candidate.ModulePath != Declaration.ModulePath || Candidate.Scope.Id != Declaration.ScopeId)
				{
					return false;
				}
				std::string Module = Candidate.ModulePath;
				std::replace(Module.begin(), Module.end(), '\\', '/');
				Scope.OwnerTypeName =
					Candidate.bExported || Module.empty() ? Candidate.Name : Module + '\x1F' + Candidate.Name;
				for (std::size_t Index = 0; Index < TemplateParameterNames(Candidate).size() && Index < Arguments.size();
					 ++Index)
				{
					Scope.OwnerTypeName +=
						(Index == 0 ? "#" : "") + std::to_string(Arguments[Index].size()) + ":" + Arguments[Index];
				}
				return false;
			});
		};
		if (!Declaration.OwnerTypeName.empty())
		{
			Owner(Program.Classes, ERuntimeTypeDeclarationKind::Class);
			Owner(Program.Structs, ERuntimeTypeDeclarationKind::Struct);
			Owner(Program.Interfaces, ERuntimeTypeDeclarationKind::Interface);
		}
		if (!Scope.OwnerFunctionName.empty())
		{
			std::string Module = Scope.ModulePath;
			std::replace(Module.begin(), Module.end(), '\\', '/');
			Scope.OwnerFunctionName = Module + '\x1F' + Scope.OwnerFunctionName;
			bool First = true;
			for (const auto& Parameter : EnclosingTemplateParameterNames(Scope))
			{
				const auto It = std::find(EnclosingTemplateParameterNames(Scope).begin(),
										  EnclosingTemplateParameterNames(Scope).end(), Parameter);
				const auto Index = static_cast<std::size_t>(It - EnclosingTemplateParameterNames(Scope).begin());
				if (Index < Arguments.size())
				{
					Scope.OwnerFunctionName +=
						(First ? "#" : "") + std::to_string(Arguments[Index].size()) + ":" + Arguments[Index];
					First = false;
				}
			}
		}
		struct FChild
		{
			FNestedTypeDeclaration Value;
			FSourceLocation Location;
		};
		std::vector<FChild> Children;
		const auto Collect = [&](const auto& Declarations, const ERuntimeTypeDeclarationKind Kind)
		{
			ForCandidates(Declarations, Kind, ERuntimeTypeLookupKind::Children, Declaration.Id, [&](const auto& Child)
			{
				if (Child.Scope.ScopeId == Declaration.Id &&
					Child.Scope.OwnerTypeName == Declaration.QualifiedName &&
					Child.ModulePath == Declaration.ModulePath)
				{
					auto ChildScope = Child.Scope;
					ChildScope.bPublicPath = Scope.bPublicPath && ChildScope.Access == EMemberAccess::Public;
					Children.push_back({{std::move(ChildScope), Child.DeclMeta.StableId}, Child.Location});
				}
				return false;
			});
		};
		Collect(Program.Classes, ERuntimeTypeDeclarationKind::Class);
		Collect(Program.Structs, ERuntimeTypeDeclarationKind::Struct);
		Collect(Program.Interfaces, ERuntimeTypeDeclarationKind::Interface);
		Collect(Program.Enums, ERuntimeTypeDeclarationKind::Enum);
		std::stable_sort(Children.begin(), Children.end(),
						 [](const auto& A, const auto& B)
						 {
							 if (A.Location.FileLabel != B.Location.FileLabel)
							 {
								 return A.Location.FileLabel < B.Location.FileLabel;
							 }
							 if (A.Location.Line != B.Location.Line)
							 {
								 return A.Location.Line < B.Location.Line;
							 }
							 return A.Location.Column < B.Location.Column;
						 });
		for (auto& Child : Children)
		{
			NestedTypes.push_back(std::move(Child.Value));
		}
	}
} // namespace VeroScript
