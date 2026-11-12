// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAnalysisControl.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"

#include <algorithm>

namespace VeroScript
{
	bool VeroScriptCompiler::CheckScopedTypeRequirements(
	    const std::string_view ScopeId, const std::string_view FunctionOwner,
	    const std::unordered_map<std::string, std::string>& Bindings)
	{
		if (!ActiveProgram)
		{
			return true;
		}
		thread_local unsigned Depth = 0;
		if (Depth >= 64)
		{
			ReportError("recursive scoped generic requirement", CurrentEmitLocation);
			return false;
		}
		struct FDepthGuard
		{
			unsigned& Value;
			~FDepthGuard()
			{
				--Value;
			}
		} Guard{Depth};
		++Depth;
		std::vector<std::string> Names, Values;
		for (const auto& Binding : Bindings)
		{
			Names.push_back(Binding.first);
			Values.push_back(Binding.second);
		}
		std::vector<const FScopedTypeUse*> Uses;
		bool LexicalScope = false;
		if (!ScopeId.empty() || !FunctionOwner.empty())
		{
			const auto& Index = ScopeId.empty() ? ScopedUsesByFunction : ScopedUsesByType;
			const auto Found = Index.find(std::string(ScopeId.empty() ? FunctionOwner : ScopeId));
			if (Found == Index.end())
			{
				if (ScopeId.empty()) return true;
				LexicalScope = true;
				for (const auto& Use : ActiveProgram->ScopedTypeUses)
				{
					if (Use.Context && (Use.Context->ScopeId == ScopeId || Use.Context->ScopeId.rfind(std::string(ScopeId) + '/', 0) == 0))
					{
						Uses.push_back(&Use);
					}
				}
			}
			else
			{
				Uses = Found->second;
			}
		}
		else
		{
			Uses.reserve(ActiveProgram->ScopedTypeUses.size());
			for (const auto& Use : ActiveProgram->ScopedTypeUses)
			{
				Uses.push_back(&Use);
			}
		}
		for (const auto* SourceUse : Uses)
		{
			const auto& Use = *SourceUse;
			if (ConstantBranchState && !ConstantBranchState->Includes(Use))
			{
				continue;
			}
			CheckAnalysisCancellation();
			if (!ScopeId.empty() && !LexicalScope &&
			    (!Use.Context || !Use.Context->TypeScope || Use.Context->TypeScope->Id != ScopeId))
			{
				continue;
			}
			if (!FunctionOwner.empty() && Use.ContextFunction != FunctionOwner)
			{
				continue;
			}
			std::vector<FTypePathSegment> Path;
			if (!ParseTypePath(SubstituteScopedTypeParameters(Use.Name, Names, Values), Path))
			{
				continue;
			}
			std::string Name;
			std::vector<std::string> Arguments;
			for (const auto& Part : Path)
			{
				const auto Parent = Name;
				if (!Name.empty())
				{
					Name += '.';
				}
				Name += Part.Name;
				if (!Parent.empty())
				{
					const auto Bound = FindTypeMember(*ActiveProgram, nullptr, Parent, Arguments, Part.Name,
					    Use.Context ? Use.Context->ModulePath
					                : std::string(Use.Location.FileLabel));
					if (!Bound.Name.empty())
					{
						Name = Bound.Name;
						Arguments = Bound.OwnerArguments;
					}
				}
				Arguments.insert(Arguments.end(), Part.Arguments.begin(), Part.Arguments.end());
				if (!Part.bApplication && Arguments.empty())
				{
					continue;
				}
				const auto Exact = [&](const auto& Declarations)
				{
					return std::any_of(Declarations.begin(), Declarations.end(),
					    [&](const auto& Declaration)
					{
						return Declaration.Name == Name &&
						    TemplateParameterNames(Declaration).size() == Arguments.size() &&
						    CanAccessSymbol(Declaration.ModulePath, Declaration.bExported,
						        Use.Context
						            ? Use.Context->ModulePath
						            : std::string(Use.Location.FileLabel));
					});
				};
				const bool HasExact = Exact(ActiveProgram->Classes) || Exact(ActiveProgram->Structs) ||
				    Exact(ActiveProgram->Interfaces);
				const auto Check = [&](const auto& Declarations)
				{
					for (const auto& Declaration : Declarations)
					{
						if (Declaration.Name != Name || TemplateParameterNames(Declaration).size() < Arguments.size() ||
						    (HasExact && TemplateParameterNames(Declaration).size() != Arguments.size()) ||
						    !CanAccessSymbol(Declaration.ModulePath, Declaration.bExported,
						        Use.Context ? Use.Context->ModulePath
						                    : std::string(Use.Location.FileLabel)))
						{
							continue;
						}
						if (TemplateParameterNames(Declaration).size() != Arguments.size() &&
						    (TemplateParameterDefaults(Declaration).size() != TemplateParameterNames(Declaration).size() ||
						        std::any_of(TemplateParameterDefaults(Declaration).begin() + Arguments.size(),
						            TemplateParameterDefaults(Declaration).end(),
						            [](const auto& Default)
						{
							return Default.empty();
						})))
						{
							continue;
						}
						if (Declaration.Bounds.empty())
						{
							return true;
						}
						const auto Unbound =
						    Use.Context ? Use.Context->TypeParameters : std::vector<std::string>{};
						const std::vector<std::string> Markers(Unbound.size(), "$unbound");
						if (std::any_of(Arguments.begin(), Arguments.end(),
						        [&](const auto& Argument)
						{
							return SubstituteScopedTypeParameters(Argument, Unbound, Markers)
							           .find("$unbound") != std::string::npos;
						}))
						{
							return true;
						}
						struct FContextGuard
						{
							std::string& Module;
							std::string& Namespace;
							std::string PreviousModule, PreviousNamespace;
							~FContextGuard()
							{
								Module = std::move(PreviousModule);
								Namespace = std::move(PreviousNamespace);
							}
						} Context{CurrentModulePath, CurrentNamespacePrefix, CurrentModulePath,
						    CurrentNamespacePrefix};
						CurrentModulePath =
						    Use.Context ? Use.Context->ModulePath : std::string(Use.Location.FileLabel);
						CurrentNamespacePrefix = Use.Context ? Use.Context->Namespace : std::string{};
						auto Concrete = Arguments;
						for (auto& Argument : Concrete)
						{
							Argument = CanonicalTypeNameForCoercion(
							    QualifyTypeName(NormalizeTypeAlias(Argument), CurrentModulePath));
						}
						if (!CompleteGenericTypeArguments(TemplateParameterNames(Declaration), TemplateParameterDefaults(Declaration),
						        Declaration.ModulePath, Concrete, Use.Location,
						        Declaration.Name, &Declaration.TemplateParameters))
						{
							return false;
						}
						return CheckTypeParamBounds(TemplateParameterNames(Declaration), Declaration.Bounds, Concrete,
						    Declaration.ModulePath, Use.Location, "type");
					}
					return true;
				};
				if (!Check(ActiveProgram->Classes) || !Check(ActiveProgram->Structs) ||
				    !Check(ActiveProgram->Interfaces))
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace VeroScript
