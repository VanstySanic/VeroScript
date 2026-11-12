// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include <unordered_set>

namespace VeroScript::MethodBindingDetail
{
	bool HasPublicBasePath(
		const FBytecodeModule& Module, const std::uint16_t From,
		const std::uint16_t To, std::unordered_set<std::uint16_t>& Visited);

	bool HasBasePath(
		const FBytecodeModule& Module, const std::uint16_t From,
		const std::uint16_t To, std::unordered_set<std::uint16_t>& Visited);

	bool HasInterfaceBasePath(
		const FBytecodeModule& Module,
		const std::uint16_t From,
		const std::uint16_t To,
		std::unordered_set<std::uint16_t>& Visited);

	inline bool UsingNamesDirectBase(
			const FBytecodeModule& Module, const FBytecodeTypeDesc& Type,
			const FBytecodeTypeDesc::FUsingMember& Using,
			const std::size_t BasePosition)
		{
			if (BasePosition >= Type.BaseTypeIndices.size()
				|| Type.BaseTypeIndices[BasePosition] >= Module.Types.size())
			{
				return false;
			}
			const std::string& Name =
				Module.Types[Type.BaseTypeIndices[BasePosition]].Name;
			std::string Qualified = Using.BaseTypeName;
			for (std::size_t Separator = Qualified.find("::");
				Separator != std::string::npos;
				Separator = Qualified.find("::", Separator + 1))
			{
				Qualified.replace(Separator, 2, ".");
			}
			if (Qualified == Name
				|| Qualified == Name.substr(FindLinkageIdentityStart(Name)))
			{
				return true;
			}
			if (Qualified != ModuleMeta::TypeSimpleName(Name))
			{
				return false;
			}
			std::size_t Matches = 0;
			for (const std::uint16_t Base : Type.BaseTypeIndices)
			{
				Matches += Base < Module.Types.size()
					&& ModuleMeta::TypeSimpleName(Module.Types[Base].Name)
						== Qualified;
			}
			return Matches == 1;
		}

}
