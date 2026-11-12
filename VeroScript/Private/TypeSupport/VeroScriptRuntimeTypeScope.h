// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypeScope.h"

namespace VeroScript
{
	struct FProgram;
	struct FBytecodeModule;
	enum class ERuntimeTypeDeclarationKind : unsigned char
	{
		Class,
		Struct,
		Interface,
		Enum
	};
	enum class ERuntimeTypeLookupKind : unsigned char
	{
		Name,
		Scope,
		Children
	};
	struct FRuntimeTypeScopeLookupView
	{
		const void* Context = nullptr;
		const std::vector<std::size_t>& (*Candidates)(const void*, ERuntimeTypeDeclarationKind,
			ERuntimeTypeLookupKind, const std::string&) = nullptr;
	};
	bool ValidateRuntimeTypeScopes(const FBytecodeModule& Module, std::string& Error);
	void PopulateRuntimeTypeScope(const FProgram& Program, const FTypeDeclarationScope& Declaration,
								  const std::vector<std::string>& Arguments, FTypeDeclarationScope& Scope,
								  std::vector<FNestedTypeDeclaration>& NestedTypes,
		const FRuntimeTypeScopeLookupView* Lookup = nullptr);
} // namespace VeroScript
