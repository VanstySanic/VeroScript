// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptMethodKey.h"

namespace VeroScript::Runtime
{
	// Ordinary enum payloads retain declaration spelling for the native ABI.
	// Enums enclosed by templates retain their complete closed instance identity.
	inline std::string EnumValueTypeName(const FBytecodeModule &Module, const std::string &TypeName)
	{
		const auto Start = FindLinkageIdentityStart(TypeName);
		if (Start != 0)
		{
			for (const auto &Declaration : Module.Enums)
			{
				if (Declaration.Name == TypeName && Declaration.TemplateInstance.EnclosingArguments.empty())
				{
					return TypeName.substr(Start);
				}
			}
		}
		return TypeName;
	}
} // namespace VeroScript::Runtime
