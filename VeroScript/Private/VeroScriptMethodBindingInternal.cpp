// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMethodBindingInternal.h"

#include <vector>

namespace VeroScript::MethodBindingDetail
{
	namespace
	{
		bool HasTypeBasePath(const FBytecodeModule& Module,
			const std::uint16_t From, const std::uint16_t To,
			std::unordered_set<std::uint16_t>& Visited, const bool bPublicOnly)
		{
			if (From >= Module.Types.size() || To >= Module.Types.size()) return false;
			if (From == To) return true;
			if (!Visited.insert(From).second) return false;
			// Valid bytecode may contain long inheritance chains. Keep traversal
			// storage on the heap so host stack sizes do not limit access checks.
			std::vector<std::uint16_t> Pending{From};
			for (std::size_t Position = 0; Position < Pending.size(); ++Position)
			{
				const FBytecodeTypeDesc& Type = Module.Types[Pending[Position]];
				for (std::size_t Index = 0; Index < Type.BaseTypeIndices.size(); ++Index)
				{
					if (bPublicOnly && (Index >= Type.BaseInheritanceAccess.size()
						|| Type.BaseInheritanceAccess[Index] != EMemberAccess::Public))
					{
						continue;
					}
					const std::uint16_t Base = Type.BaseTypeIndices[Index];
					if (Base == To) return true;
					if (Base < Module.Types.size() && Visited.insert(Base).second)
					{
						Pending.push_back(Base);
					}
				}
			}
			return false;
		}
	}

	bool HasPublicBasePath(const FBytecodeModule& Module,
		const std::uint16_t From, const std::uint16_t To,
		std::unordered_set<std::uint16_t>& Visited)
	{
		return HasTypeBasePath(Module, From, To, Visited, true);
	}

	bool HasBasePath(const FBytecodeModule& Module,
		const std::uint16_t From, const std::uint16_t To,
		std::unordered_set<std::uint16_t>& Visited)
	{
		return HasTypeBasePath(Module, From, To, Visited, false);
	}

	bool HasInterfaceBasePath(const FBytecodeModule& Module,
		const std::uint16_t From, const std::uint16_t To,
		std::unordered_set<std::uint16_t>& Visited)
	{
		if (From >= Module.Interfaces.size() || To >= Module.Interfaces.size()) return false;
		if (From == To) return true;
		if (!Visited.insert(From).second) return false;
		std::vector<std::uint16_t> Pending{From};
		for (std::size_t Position = 0; Position < Pending.size(); ++Position)
		{
			for (const std::uint16_t Base : Module.Interfaces[Pending[Position]].BaseInterfaceIndices)
			{
				if (Base == To) return true;
				if (Base < Module.Interfaces.size() && Visited.insert(Base).second)
				{
					Pending.push_back(Base);
				}
			}
		}
		return false;
	}
}
