// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParserLookup.h"

#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAst.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptModuleVisibility.h"

#include <stdexcept>

namespace VeroScript
{
	bool VeroScriptParser::FParsedTypeLookup::Matches(const FProgram& Program) const
	{
		return Owner == &Program && StructCount == Program.Structs.size() && ClassCount == Program.Classes.size() && InterfaceCount == Program.Interfaces.size() && EnumCount == Program.Enums.size() && AliasCount == Program.TypeAliases.size();
	}

	void VeroScriptParser::FParsedTypeLookup::Append(const FProgram& Program)
	{
		if (Owner != &Program || StructCount > Program.Structs.size() || ClassCount > Program.Classes.size() || InterfaceCount > Program.Interfaces.size() || EnumCount > Program.Enums.size() || AliasCount > Program.TypeAliases.size())
		{
			*this = {};
			Owner = &Program;
		}
		const auto Add = [&](const auto& Declarations, auto& Index, std::size_t& Count,
		                     const ETypeMemberDeclarationKind Kind)
		{
			while (Count < Declarations.size())
			{
				if ((Count & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const auto& D = Declarations[Count];
				Index[D.Name].push_back(Count);
				if (!D.Scope.OwnerTypeName.empty())
				{
					Children[static_cast<unsigned>(Kind)][MakeTypeMemberLookupKey(D.ModulePath,
					                                          D.Scope.ScopeId, D.Scope.OwnerTypeName, D.Scope.SimpleName)]
					    .push_back(Count);
				}
				++Count;
			}
		};
		Add(Program.Structs, Structs, StructCount, ETypeMemberDeclarationKind::Struct);
		Add(Program.Classes, Classes, ClassCount, ETypeMemberDeclarationKind::Class);
		Add(Program.Interfaces, Interfaces, InterfaceCount, ETypeMemberDeclarationKind::Interface);
		Add(Program.Enums, Enums, EnumCount, ETypeMemberDeclarationKind::Enum);
		const auto PreviousAliases = AliasCount;
		Add(Program.TypeAliases, Aliases, AliasCount, ETypeMemberDeclarationKind::Alias);
		for (auto Position = PreviousAliases; Position < AliasCount; ++Position)
		{
			const auto& Alias = Program.TypeAliases[Position];
			AliasPrefixes[Alias.Scope.DisplayName.empty() ? Alias.Name : Alias.Scope.DisplayName].push_back(Position);
		}
	}

	const std::vector<std::size_t>& VeroScriptParser::FParsedTypeLookup::Names(
	    const ETypeMemberDeclarationKind Kind, const std::string& Name) const
	{
		const FTypeNameIndex* Index = nullptr;
		switch (Kind)
		{
		case ETypeMemberDeclarationKind::Class:
			{
				Index = &Classes;
				break;
			}
		case ETypeMemberDeclarationKind::Struct:
			{
				Index = &Structs;
				break;
			}
		case ETypeMemberDeclarationKind::Interface:
			{
				Index = &Interfaces;
				break;
			}
		case ETypeMemberDeclarationKind::Enum:
			{
				Index = &Enums;
				break;
			}
		case ETypeMemberDeclarationKind::Alias:
			{
				Index = &Aliases;
				break;
			}

		default:
			{
				break;
			}
		}
		static const std::vector<std::size_t> Empty;
		if (!Index) return Empty;
		const auto Found = Index->find(Name);
		return Found == Index->end() ? Empty : Found->second;
	}

	const std::vector<std::size_t>& VeroScriptParser::FParsedTypeLookup::Nested(
	    const ETypeMemberDeclarationKind Kind, const std::string& Module, const std::string& Scope,
	    const std::string& OwnerName, const std::string& Member) const
	{
		static const std::vector<std::size_t> Empty;
		const auto& Index = Children[static_cast<unsigned>(Kind)];
		if (Index.empty()) return Empty;
		const auto Found = Index.find(MakeTypeMemberLookupKey(Module, Scope, OwnerName, Member));
		return Found == Index.end() ? Empty : Found->second;
	}

	const VeroScriptParser::FParsedTypeLookup& VeroScriptParser::GetParsedTypeLookup(const FProgram& Target) const
	{
		if (&Target == Program.get())
		{
			if (!ParsedTypeLookup)
			{
				ParsedTypeLookup = std::make_shared<FParsedTypeLookup>();
			}
			ParsedTypeLookup->Append(Target);
			return *ParsedTypeLookup;
		}
		if (!ExternalTypeLookup || !ExternalTypeLookup->Matches(Target))
		{
			auto Built = std::make_shared<FParsedTypeLookup>();
			Built->Append(Target);
			ExternalTypeLookup = std::move(Built);
		}
		return *ExternalTypeLookup;
	}

	const VeroScriptParser::FForwardTypeLookup& VeroScriptParser::GetExternalForwardTypeLookup(
	    const ModuleVisibility::FImportIndex* ImportIndex) const
	{
		if (!ExternalForwardTypeLookup || ExternalForwardTypeLookup->Module != FileLabel || !ExternalForwardTypeLookup->Declarations->Matches(*ExternalTypeProgram))
		{
			(void)GetParsedTypeLookup(*ExternalTypeProgram);
			auto Built = std::make_shared<FForwardTypeLookup>();
			Built->Declarations = ExternalTypeLookup;
			Built->Module = std::string(FileLabel);
			const auto Add = [&](const auto& Declarations)
			{
				for (std::size_t Position = 0; Position < Declarations.size(); ++Position)
				{
					if ((Position & 255u) == 0u)
					{
						CheckAnalysisCancellation();
					}
					const auto& D = Declarations[Position];
				if (ModuleVisibility::CanAccessSymbol(
				        *ExternalTypeProgram, D.ModulePath, D.bExported, Built->Module, ImportIndex))
					{
						Built->Names.insert(D.Name);
						if (!D.Scope.DisplayName.empty())
						{
							Built->Names.insert(D.Scope.DisplayName);
						}
					}
				}
			};
			Add(ExternalTypeProgram->Structs);
			Add(ExternalTypeProgram->Classes);
			Add(ExternalTypeProgram->Interfaces);
			Add(ExternalTypeProgram->Enums);
			Add(ExternalTypeProgram->TypeAliases);
			for (const auto& D : ExternalTypeProgram->Interfaces)
			{
				if (Built->Names.count(D.Name))
				{
					Built->Interfaces.insert(D.Name);
				}
			}
			ExternalForwardTypeLookup = std::move(Built);
		}
		return *ExternalForwardTypeLookup;
	}

	FTypeMemberLookupView VeroScriptParser::GetTypeMemberLookupView() const
	{
		return {this,
		    [](const void* Context, const FProgram& Target, const ETypeMemberDeclarationKind Kind,
		        const std::string& Name) -> const std::vector<std::size_t>&
		{
			return static_cast<const VeroScriptParser*>(Context)->GetParsedTypeLookup(Target).Names(Kind, Name);
		},
		    [](const void* Context, const FProgram& Target, const ETypeMemberDeclarationKind Kind,
		        const std::string& Module, const std::string& Scope, const std::string& Owner,
		        const std::string& Member) -> const std::vector<std::size_t>&
		{
			return static_cast<const VeroScriptParser*>(Context)->GetParsedTypeLookup(Target).Nested(
			    Kind, Module, Scope, Owner, Member);
		}};
	}

	bool VeroScriptParser::IsForwardTypeName(const std::string& Name) const
	{
		return ForwardTypeNames.count(Name) != 0 || (bFragmentForwardLookup && ExternalTypeProgram && GetExternalForwardTypeLookup().Names.count(Name) != 0);
	}

	bool VeroScriptParser::IsForwardInterfaceName(const std::string& Name) const
	{
		return ForwardInterfaceNames.count(Name) != 0 || (bFragmentForwardLookup && ExternalTypeProgram && GetExternalForwardTypeLookup().Interfaces.count(Name) != 0);
	}

	FParserLookupContext::FParserLookupContext(const FProgram& InProgram, const IHostBindingCatalog* Bindings)
	    : Program(InProgram), ImportIndex(InProgram.Imports)
	{
		if (Bindings)
		{
			for (const auto& Type : Bindings->GetTypes())
			{
				HostNames.insert(Type.Name);
			}
			for (const auto& Type : Bindings->GetTypeAliases())
			{
				HostNames.insert(Type.Name);
			}
			for (const auto& Type : Bindings->GetGenericTypeTemplates())
			{
				HostNames.insert(Type.Name);
			}
		}
	}

	void FParserLookupContext::Attach(VeroScriptParser& Parser)
	{
		if (Parser.ExternalTypeProgram != &Program)
		{
			throw std::logic_error("parser lookup program mismatch");
		}
		if (!Declarations || !Declarations->Matches(Program))
		{
			auto Built = std::make_shared<VeroScriptParser::FParsedTypeLookup>();
			Built->Append(Program);
			Declarations = std::move(Built);
			Forward.clear();
		}
		Parser.ExternalTypeLookup = Declarations;
		auto& Visible = Forward[std::string(Parser.FileLabel)];
		Parser.ExternalForwardTypeLookup = Visible;
		(void)Parser.GetExternalForwardTypeLookup(&ImportIndex);
		Visible = Parser.ExternalForwardTypeLookup;
		Parser.SetHostTypeNames(HostNames);
	}
}
