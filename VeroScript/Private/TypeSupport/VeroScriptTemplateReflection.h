// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

namespace VeroScript
{
	struct FBytecodeModule;

	// Internal query numbers are serialized in ordinary reflection builtin calls.
	enum class ETemplateReflectionQuery : std::int32_t
	{
		DeclarationCount = 200,
		DeclarationId,
		DeclarationName,
		DeclarationKind,
		DeclarationOwner,
		ParameterCount,
		OwnParameterCount,
		EnclosingParameterCount,
		ParameterId,
		ParameterName,
		ParameterKind,
		ParameterValueType,
		ParameterDefault,
		ParameterIsEnclosing,

		InstanceDeclarationId = 600,
		ArgumentCount,
		ArgumentEncoding,
		ArgumentName,
		ArgumentKind,
		ArgumentValueType,
		EnclosingArgumentCount,
		EnclosingArgumentEncoding,
		EnclosingArgumentName,
	};

	bool QueryOpenTemplateMetadata(const FBytecodeModule &Module, const std::string &ModulePath,
								   ETemplateReflectionQuery Query, std::size_t Index, std::size_t Detail,
								   FValue &Result, std::string &Error);
	bool QueryTemplateInstanceMetadata(const FBytecodeModule &Module, const FScriptObject &Descriptor,
									   ETemplateReflectionQuery Query, std::int32_t Index, FValue &Result,
									   std::string &Error);
} // namespace VeroScript
