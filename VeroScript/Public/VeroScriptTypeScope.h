// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptTypes.h"
#include "VeroScriptTemplate.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	/** Semantic ownership is independent of runtime layout and namespace spelling. */
	struct FTypeDeclarationScope
	{
		std::string Id;
		std::string ScopeId;
		std::string ParentScopeId;
		std::string SimpleName;
		std::string QualifiedName;
		std::string OwnerTypeName;
		std::string OwnerFunctionName;
		std::string AccessTypeName;
		std::string AccessModulePath;
		std::string ModulePath;
		std::string Kind;
		std::string DisplayName;
		std::vector<std::string> EnclosingTypeParameters;
		std::vector<std::string> FunctionTypeParameters;
		std::vector<std::string> ConcreteTypeArguments;
		std::vector<std::string> ForbiddenRuntimeNames;
		std::uint32_t OwnTypeParameterCount = 0;
		std::uint32_t OwnTemplateParameterCount = 0;
		FTemplateParameterList EnclosingTemplateParameters;
		EMemberAccess Access = EMemberAccess::Public;
		bool bLocal = false;
		bool bExported = false;
		bool bPublicPath = true;
	};

	inline const std::vector<std::string>& EnclosingTemplateParameterNames(const FTypeDeclarationScope& Scope)
	{
		return Scope.EnclosingTemplateParameters.IsEmpty() ? Scope.EnclosingTypeParameters
			: Scope.EnclosingTemplateParameters.GetNames();
	}

	inline std::uint32_t OwnTemplateParameterCount(const FTypeDeclarationScope& Scope)
	{
		return Scope.OwnTemplateParameterCount != 0 ? Scope.OwnTemplateParameterCount : Scope.OwnTypeParameterCount;
	}

	struct FNestedTypeDeclaration
	{
		FTypeDeclarationScope Scope;
		std::string DeclarationId;
	};

	/** A source spelling keeps its access route even when an alias binds an internal type. */
	struct FScopedTypeUse
	{
		std::string Name;
		std::string ContextType;
		std::string ContextFunction;
		FSourceLocation Location;
		std::string ContextModule;
		std::shared_ptr<const struct FDeferredSyntax> Context;
		std::string SourceName;
	};

	struct FTypePathSegment
	{
		std::string Name;
		std::vector<std::string> Arguments;
		bool bApplication = false;
	};

	/** Removes private owner-arity markers from a display spelling, preserving type identity elsewhere. */
	std::string StripTypeScopeMarkers(std::string Name);

	/** Splits only at top-level scope separators; callable arrows are not generic closes. */
	bool ParseTypePath(std::string_view Name, std::vector<FTypePathSegment>& Segments);
	std::string TypeScopeIdentity(std::string_view Parent, std::string_view Kind, std::string_view Name);
	std::string MakeLocalTypePrefix(std::string_view Identity, std::string_view Module);
	bool HasScopedTypeDeclarations(const struct FProgram& Program);
} // namespace VeroScript
