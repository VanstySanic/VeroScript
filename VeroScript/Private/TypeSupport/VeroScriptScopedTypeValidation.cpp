// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemberAccess.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptTypeMemberBinding.h"
#include "Parser/VeroScriptParserBindings.h"

#include <unordered_map>
#include <algorithm>
#include <type_traits>

namespace VeroScript
{
	bool ValidateScopedTypeDeclarations(
	    const FProgram& Program, const ModuleVisibility::FImportIndex* ImportIndex, FScriptError& Error,
	    const std::function<bool(const FMemberAccessInfo&, const FScopedTypeUse&)>& FunctionAccess,
	    const std::vector<FScopedTypeUse>* Uses,
	    const std::function<bool(std::string_view)>& CheckScope)
	{
		struct FEntry
		{
			const FTypeDeclarationScope* Scope;
			std::string Linkage;
			std::size_t Arity, RequiredArity;
			bool Exported;
			std::string AliasTarget;
			const FDeferredSyntax* Context;
			const FSourceLocation* Location;
		};
		std::unordered_map<std::string, std::vector<FEntry>> Index;
		const auto Add = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				if (CheckScope && !CheckScope(Declaration.Scope.ScopeId))
				{
					continue;
				}
				std::size_t Arity = EnclosingTemplateParameterNames(Declaration.Scope).size();
				if constexpr (!std::is_same_v<std::decay_t<decltype(Declaration)>, FTypeAliasDecl> &&
				    !std::is_same_v<std::decay_t<decltype(Declaration)>, FEnumDecl>)
				{
					Arity = TemplateParameterNames(Declaration).size();
				}
				std::size_t Required = Arity;
				if constexpr (!std::is_same_v<std::decay_t<decltype(Declaration)>, FTypeAliasDecl> &&
				    !std::is_same_v<std::decay_t<decltype(Declaration)>, FEnumDecl>)
				{
					while (Required > 0 && Required <= TemplateParameterDefaults(Declaration).size() &&
					    !TemplateParameterDefaults(Declaration)[Required - 1].empty())
					{
						--Required;
					}
				}
				std::string Module = Declaration.ModulePath;
				std::replace(Module.begin(), Module.end(), '\\', '/');
				const std::string Linkage = Declaration.bExported || Module.empty()
				    ? Declaration.Name
				    : Module + '\x1F' + Declaration.Name;
				std::string AliasTarget;
				if constexpr (std::is_same_v<std::decay_t<decltype(Declaration)>, FTypeAliasDecl>)
				{
					AliasTarget = Declaration.TypeName;
				}
				Index[Declaration.Name].push_back({&Declaration.Scope, Linkage, Arity, Required,
				    Declaration.bExported, std::move(AliasTarget),
				    Declaration.DeclarationContext.get(), &Declaration.Location});
			}
		};
		Add(Program.Classes);
		Add(Program.Structs);
		Add(Program.Interfaces);
		Add(Program.Enums);
		Add(Program.TypeAliases);
		std::unordered_map<std::string, const FEntry*> ById;
		for (const auto& Group : Index)
		{
			for (const auto& Entry : Group.second)
			{
				ById.emplace(Entry.Scope->Id, &Entry);
			}
		}
		std::unordered_map<std::string, std::string> MemberNames;
		for (const auto& Name : Index)
		{
			for (const auto& Entry : Name.second)
			{
				if (Entry.Scope->OwnerTypeName.empty())
				{
					continue;
				}
				const auto Key =
				    Entry.Scope->ModulePath + '\x1F' + Entry.Scope->ScopeId + '\x1F' + Entry.Scope->SimpleName;
				if (!MemberNames.emplace(Key, Entry.Scope->Id).second)
				{
					Error.Message = "duplicate nested type or alias: " + StripTypeScopeMarkers(Entry.Scope->QualifiedName);
					Error.Location = *Entry.Location;
					return false;
				}
			}
		}
		for (const auto& Enum : Program.Enums)
		{
			if (CheckScope && !CheckScope(Enum.Scope.ScopeId))
			{
				continue;
			}
			if (Enum.bScoped || Enum.Scope.OwnerTypeName.empty())
			{
				continue;
			}
			for (const auto& Member : Enum.Members)
			{
				const auto Key = Enum.ModulePath + '\x1F' + Enum.Scope.ScopeId + '\x1F' + Member.Name;
				if (!MemberNames.emplace(Key, Enum.Scope.Id).second)
				{
					Error.Message = "duplicate member in type scope: " + Member.Name;
					Error.Location = Member.Location;
					return false;
				}
			}
		}

		const auto CheckMembers = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				if (CheckScope && !CheckScope(Declaration.Scope.ScopeId))
				{
					continue;
				}
				const auto Conflict = [&](const auto& Member)
				{
					if (MemberNames.count(Declaration.ModulePath + '\x1F' + Declaration.Scope.Id + '\x1F' +
					        Member.Name) != 0)
					{
						Error.Message = "type name conflicts with another member: " + Member.Name;
						Error.Location = Member.Location;
						return true;
					}
					return false;
				};
				for (const auto& Field : Declaration.Fields)
				{
					if (Conflict(Field))
					{
						return false;
					}
				}
				for (const auto& Method : Declaration.Methods)
				{
					if (Conflict(Method))
					{
						return false;
					}
				}
			}
			return true;
		};
		if (!CheckMembers(Program.Classes) || !CheckMembers(Program.Structs) || !CheckMembers(Program.Interfaces))
		{
			return false;
		}
		const auto Find = [&](const std::string& Name, const std::string_view Module,
		                      const std::size_t Arity = static_cast<std::size_t>(-1)) -> const FEntry*
		{
			const auto Found = Index.find(Name);
			if (Found == Index.end())
			{
				return nullptr;
			}
			const FEntry* Result = nullptr;
			for (const auto& Entry : Found->second)
			{
				if (Arity != static_cast<std::size_t>(-1) && (Arity < Entry.RequiredArity || Arity > Entry.Arity))
				{
					continue;
				}
				if (!ModuleVisibility::CanAccessSymbol(Program, Entry.Scope->ModulePath, Entry.Exported,
				        std::string(Module), ImportIndex))
				{
					continue;
				}
				if (Result && Result->Arity == Arity && Entry.Arity != Arity)
				{
					continue;
				}
				if (!Result || (Entry.Arity == Arity && Result->Arity != Arity) ||
				    (Entry.Scope->ModulePath == Module && Result->Scope->ModulePath != Module))
				{
					Result = &Entry;
				}
			}
			return Result;
		};
		const auto Context = [](const FEntry* Entry)
		{
			if (!Entry)
			{
				return std::string{};
			}
			std::string Result = Entry->Linkage;
			for (std::size_t I = 0; I < Entry->Arity; ++I)
			{
				Result += (I == 0 ? "#" : "") + std::string("1:T");
			}
			return Result;
		};
		for (const auto& Use : Uses ? *Uses : Program.ScopedTypeUses)
		{
			CheckAnalysisCancellation();
			std::vector<FTypePathSegment> Segments;
			if (!ParseTypePath(Use.Name, Segments))
			{
				continue;
			}
			std::size_t AliasPrefix = 0;
			std::vector<FTypePathSegment> SourcePath;
			if (Use.Context && Use.Context->Bindings && ParseTypePath(Use.SourceName, SourcePath) &&
			    Use.SourceName.rfind("::", 0) != 0 && !SourcePath.empty())
			{
				const auto& Bindings = *Use.Context->Bindings;
				bool Found = false;
				for (auto I = Bindings.Aliases.rbegin(); I != Bindings.Aliases.rend() && !Found; ++I)
				{
					for (auto Head = *I; Head != EmptyBindingHead && Head < Bindings.RecordCount;)
					{
						const auto& Binding = Bindings.Arena->Get(Head);
						if (Binding.Name == SourcePath.front().Name)
						{
							Found = true;
							std::vector<FTypePathSegment> Target;
							if (ParseTypePath(Binding.Type, Target) && Target.size() <= Segments.size() &&
							    std::equal(Target.begin(), Target.end(), Segments.begin(),
							        [](const auto& A, const auto& B)
							{
								return A.Name == B.Name;
							}))
							{
								// The alias declaration already checked its source access route.
								AliasPrefix = Target.size();
							}
							break;
						}
						if (Binding.Previous != EmptyBindingHead && Binding.Previous >= Head)
						{
							break;
						}
						Head = Binding.Previous;
					}
				}
			}
			std::unordered_map<std::string, const FEntry*> LexicalTypes;
			const FEntry* Caller = nullptr;
			if (Use.Context && Use.Context->TypeScope)
			{
				const auto Current = ById.find(Use.Context->TypeScope->Id);
				Caller = Current == ById.end() ? nullptr : Current->second;
			}
			for (const FEntry* Current = Caller; Current && LexicalTypes.size() < 64;)
			{
				if (!LexicalTypes.emplace(Current->Scope->QualifiedName, Current).second)
				{
					break;
				}
				const auto ParentId = Current->Context && Current->Context->TypeScope
				    ? Current->Context->TypeScope->Id
				    : Current->Scope->ScopeId;
				const auto Parent = ById.find(ParentId);
				Current = Parent == ById.end() ? nullptr : Parent->second;
			}
			if (!Caller)
			{
				Caller = Find(Use.ContextType, Use.ContextModule.empty() ? Use.Location.FileLabel : std::string_view(Use.ContextModule));
			}
			const std::string Module = Use.Context ? Use.Context->ModulePath : std::string(Use.Location.FileLabel);
			std::vector<std::string> Arguments;
			std::string Path;
			std::size_t SegmentIndex = 0;
			for (const auto& Segment : Segments)
			{
				++SegmentIndex;
				const auto ParentPath = Path;
				if (!Path.empty())
				{
					Path += '.';
				}
				Path += Segment.Name;
				const auto Lexical = LexicalTypes.find(Path);
				if (Arguments.empty() && Segment.Arguments.empty() && Lexical != LexicalTypes.end())
				{
					Arguments.resize(Lexical->second->Arity, "T");
				}
				const auto ExpectedArity = Arguments.size() + Segment.Arguments.size();
				const auto* Declaration = Find(Path, Module, ExpectedArity);
				bool BoundMember = false;
				if (!Declaration && !ParentPath.empty())
				{
					const auto Binding =
					    FindTypeMember(Program, nullptr, ParentPath, Arguments, Segment.Name, Module);
					if (Binding.bAmbiguous)
					{
						Error.Message = "ambiguous inherited type: " + Path;
						Error.Location = Use.Location;
						return false;
					}
					for (const auto& Check : Binding.AccessChecks)
					{
						if (!CanAccessMemberFromContext(Check, Context(Caller), Program, {}, true, ImportIndex) &&
						    !(FunctionAccess && FunctionAccess(Check, Use)))
						{
							Error.Message = "cannot access inherited type: " + Path;
							Error.Location = Use.Location;
							return false;
						}
					}
					if (!Binding.Name.empty())
					{
						BoundMember = true;
						Path = Binding.Name;
						Arguments = Binding.OwnerArguments;
					}
					Declaration = Find(Path, Module, Arguments.size() + Segment.Arguments.size());
				}
				Arguments.insert(Arguments.end(), Segment.Arguments.begin(), Segment.Arguments.end());
				if (!Declaration)
				{
					continue;
				}
				if (SegmentIndex > AliasPrefix && !BoundMember && !Declaration->Scope->OwnerTypeName.empty())
				{
					const auto OwnerEntry = ById.find(Declaration->Scope->ScopeId);
					const auto* Owner = OwnerEntry == ById.end() ? nullptr : OwnerEntry->second;
					if (!Owner)
					{
						continue;
					}
					FMemberAccessInfo Access;
					Access.bFound = true;
					Access.EffectiveAccess = Declaration->Scope->Access;
					Access.DeclaringOwnerLinkage = Owner->Linkage;
					Access.DeclaringArity = Owner->Arity;
					if (!CanAccessMemberFromContext(Access, Context(Caller), Program, {}, true, ImportIndex) &&
					    !(FunctionAccess && FunctionAccess(Access, Use)))
					{
						Error.Message = "cannot access nested type '" + Path + "'";
						Error.Location = Use.Location;
						return false;
					}
				}
				if (!Declaration->AliasTarget.empty())
				{
					std::vector<FTypePathSegment> Target;
					if (ParseTypePath(SubstituteScopedTypeParameters(Declaration->AliasTarget,
					                      EnclosingTemplateParameterNames(*Declaration->Scope),
					                      Arguments),
					        Target))
					{
						Path.clear();
						Arguments.clear();
						for (const auto& Part : Target)
						{
							if (!Path.empty())
							{
								Path += '.';
							}
							Path += Part.Name;
							Arguments.insert(Arguments.end(), Part.Arguments.begin(), Part.Arguments.end());
						}
					}
				}
			}
		}
		return true;
	}
} // namespace VeroScript
