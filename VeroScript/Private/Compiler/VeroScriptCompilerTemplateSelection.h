// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"

#include <string_view>
#include <type_traits>

namespace VeroScript::CompilerDetail
{
	struct FTemplateTypeSelection
	{
		const FStructDecl *Struct = nullptr;
		const FClassDecl *Class = nullptr;
		const FInterfaceDecl *Interface = nullptr;
		const FEnumDecl *Enum = nullptr;
		bool Local = false;
		bool Exact = false;
		bool Ambiguous = false;
		bool Found = false;
	};

	// Select one visible declaration before inspecting argument kinds or defaults.
	// Declaration order and class/struct/interface grouping cannot change the result.
	template <typename TVisible>
	FTemplateTypeSelection SelectTemplateTypeDeclaration(const FProgram &Program, const std::string_view Name,
														 const std::string_view Module, const std::size_t Arity,
														 const TVisible &Visible,
		const std::vector<std::size_t>* Structs = nullptr,
		const std::vector<std::size_t>* Classes = nullptr,
		const std::vector<std::size_t>* Interfaces = nullptr,
		const std::vector<std::size_t>* Enums = nullptr)
	{
		FTemplateTypeSelection Selected;
		const auto Scan = [&](const auto &Declarations, const std::vector<std::size_t>* Indices)
		{
			for (std::size_t Position = 0; Position < (Indices ? Indices->size() : Declarations.size()); ++Position)
			{
				const auto& Declaration = Declarations[Indices ? (*Indices)[Position] : Position];
				CheckAnalysisCancellation();
				if (Declaration.Name != Name || !Visible(Declaration.ModulePath, Declaration.bExported))
				{
					continue;
				}
				using TDeclaration = std::decay_t<decltype(Declaration)>;
				std::size_t Count = 0, Required = 0;
				FTemplateTypeSelection Candidate;
				if constexpr (std::is_same_v<TDeclaration, FEnumDecl>)
				{
					Count = Required = EnclosingTemplateParameterNames(Declaration.Scope).size();
					Candidate.Enum = &Declaration;
				}
				else
				{
					Count = Required = TemplateParameterNames(Declaration).size();
					const auto &Defaults = TemplateParameterDefaults(Declaration);
					if (Defaults.size() == Count)
					{
						while (Required != 0 && !Defaults[Required - 1].empty())
						{
							--Required;
						}
					}
					if constexpr (std::is_same_v<TDeclaration, FStructDecl>)
					{
						Candidate.Struct = &Declaration;
					}
					else if constexpr (std::is_same_v<TDeclaration, FClassDecl>)
					{
						Candidate.Class = &Declaration;
					}
					else
					{
						Candidate.Interface = &Declaration;
					}
				}
				if (Arity < Required || Arity > Count)
				{
					continue;
				}
				Candidate.Found = true;
				Candidate.Local = Declaration.ModulePath == Module;
				Candidate.Exact = Count == Arity;
				const auto Rank = [](const FTemplateTypeSelection &Value)
				{
					return (Value.Local ? 2 : 0) + (Value.Exact ? 1 : 0);
				};
				if (!Selected.Found || Rank(Candidate) > Rank(Selected))
				{
					Selected = Candidate;
				}
				else if (Rank(Candidate) == Rank(Selected))
				{
					Selected.Ambiguous = true;
				}
			}
		};
		Scan(Program.Structs, Structs);
		Scan(Program.Classes, Classes);
		Scan(Program.Interfaces, Interfaces);
		Scan(Program.Enums, Enums);
		return Selected;
	}
} // namespace VeroScript::CompilerDetail
