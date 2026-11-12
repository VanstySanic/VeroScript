// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptModuleMeta.h"

#include <array>
#include <memory>

namespace VeroScript::ModuleMeta
{
	// Retain only the structural candidates. Conversion ranks and viability can
	// vary with each actual value, even when its runtime alternative is unchanged.
	class FFreeFunctionLookupCache
	{
	public:
		EMethodResolveStatus Resolve(const std::shared_ptr<const FBytecodeModule>& Owner,
		    std::uint64_t Generation,
		    std::string_view Name, std::uint8_t ArgumentCount, std::uint16_t& OutFunctionIndex,
		    const FValue* Arguments);

	private:
		struct FEntry
		{
			std::string Name;
			std::vector<std::uint16_t> Candidates;
			std::uint8_t ArgumentCount = 0;
		};

		std::array<FEntry, 4> Entries;
		std::weak_ptr<const FBytecodeModule> CachedOwner;
		const FBytecodeModule* CachedModule = nullptr;
		std::uint64_t CachedGeneration = 0;
		std::size_t NextEntry = 0;
	};
}
