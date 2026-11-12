// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptParser.h"
#include "VeroScriptModuleVisibility.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"

namespace VeroScript
{
	class IHostBindingCatalog;

	struct VeroScriptParser::FParsedTypeLookup
	{
		const FProgram* Owner = nullptr;
		FTypeNameIndex Structs;
		FTypeNameIndex Classes;
		FTypeNameIndex Interfaces;
		FTypeNameIndex Enums;
		FTypeNameIndex Aliases;
		FTypeNameIndex AliasPrefixes;
		// Only actual nested declarations populate these maps. Flat programs do
		// not pay for a child entry for every top-level declaration.
		FTypeNameIndex Children[5];
		std::size_t StructCount = 0;
		std::size_t ClassCount = 0;
		std::size_t InterfaceCount = 0;
		std::size_t EnumCount = 0;
		std::size_t AliasCount = 0;

		bool Matches(const FProgram& Program) const;
		void Append(const FProgram& Program);
		const std::vector<std::size_t>& Names(ETypeMemberDeclarationKind Kind, const std::string& Name) const;
		const std::vector<std::size_t>& Nested(ETypeMemberDeclarationKind Kind,
			const std::string& Module, const std::string& Scope, const std::string& OwnerName,
			const std::string& Member) const;
	};

	struct VeroScriptParser::FForwardTypeLookup
	{
		std::shared_ptr<const FParsedTypeLookup> Declarations;
		std::string Module;
		std::unordered_set<std::string> Names;
		std::unordered_set<std::string> Interfaces;
	};

	// One deferred-resolution stage owns this cache after imports are resolved.
	// A new stage takes a fresh snapshot of the import graph.
	class FParserLookupContext
	{
	public:
		FParserLookupContext(const FProgram& Program, const IHostBindingCatalog* Bindings);
		void Attach(VeroScriptParser& Parser);

	private:
		const FProgram& Program;
		const ModuleVisibility::FImportIndex ImportIndex;
		std::shared_ptr<const VeroScriptParser::FParsedTypeLookup> Declarations;
		std::unordered_map<std::string, std::shared_ptr<const VeroScriptParser::FForwardTypeLookup>> Forward;
		std::unordered_set<std::string> HostNames;
	};
}
