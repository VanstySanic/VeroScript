// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMemberAccess.h"

namespace VeroScript
{
	std::string CallableScopeName(const std::string& Owner, const std::string& Name,
								  const std::vector<FFunctionParam>& Parameters, bool Const = false);
	std::string SubstituteScopedTypeParameters(std::string_view Text, const std::vector<std::string>& Parameters,
											   const std::vector<std::string>& Arguments);
	enum class ETypeMemberDeclarationKind : unsigned char
	{
		Class,
		Struct,
		Interface,
		Enum,
		Alias
	};

	struct FTypeMemberBinding
	{
		std::string Name, ModulePath;
		std::vector<std::string> OwnerArguments;
		std::vector<FMemberAccessInfo> AccessChecks;
		bool bAmbiguous = false;
	};

	// Borrowed declarations whose bodies are still being parsed.
	struct FTypeMemberOwnerDeclarations
	{
		const FStructDecl* Struct = nullptr;
		const FClassDecl* Class = nullptr;
		const FInterfaceDecl* Interface = nullptr;
	};
	std::string MakeTypeMemberLookupKey(const std::string& Module, const std::string& Scope,
		const std::string& Owner, const std::string& Member);
	struct FTypeMemberLookupView
	{
		const void* Context = nullptr;
		const std::vector<std::size_t>& (*Owners)(const void*, const FProgram&,
			ETypeMemberDeclarationKind, const std::string&) = nullptr;
		const std::vector<std::size_t>& (*Children)(const void*, const FProgram&,
			ETypeMemberDeclarationKind, const std::string&, const std::string&,
			const std::string&, const std::string&) = nullptr;
	};

	FTypeMemberBinding FindTypeMember(const FProgram& Program, const FProgram* OtherProgram,
									  const std::string& Owner, const std::vector<std::string>& Arguments,
									  const std::string& Member, const std::string& ReferenceModule,
									  const FTypeMemberOwnerDeclarations* OpenDeclarations = nullptr,
									  const FTypeMemberLookupView* Lookup = nullptr);
} // namespace VeroScript
