// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript
{
	inline constexpr std::size_t EmptyBindingHead = std::numeric_limits<std::size_t>::max();
	struct FParserBindingRecord
	{
		std::string Name, Type;
		std::size_t Previous = EmptyBindingHead;
	};

	/** Records are appended by an unpublished parser; captured prefixes never change. */
	struct FParserBindingArena
	{
		const FParserBindingRecord& Get(std::size_t Index) const;
		std::shared_ptr<const FParserBindingArena> Base;
		std::vector<FParserBindingRecord> Records;
		std::size_t BaseCount = 0, Depth = 0;
	};
	struct FParserBindingsSnapshot
	{
		std::shared_ptr<const FParserBindingArena> Arena;
		std::vector<std::size_t> Aliases, Runtime;
		std::vector<std::size_t> AliasOrder, RuntimeOrder;
		std::size_t NextOrder = 0;
		std::size_t RecordCount = 0;
	};
	class FParserBindingStore
	{
	  public:
		explicit FParserBindingStore(const FParserBindingsSnapshot* Source = nullptr);
		void Add(std::vector<std::size_t>& Heads, const std::string& Name, const std::string& Type = {});
		std::shared_ptr<const FParserBindingsSnapshot> Capture() const;
		bool IsRuntimeName(const std::string& Name,
						   const std::vector<std::unordered_map<std::string, std::string>>& TypeScopes,
						   const std::vector<std::unordered_set<std::string>>& ValueScopes,
						   std::size_t TypeScopeOrder = 0) const;

		std::shared_ptr<FParserBindingArena> Arena;
		std::vector<std::size_t> Aliases, Runtime;
		std::vector<std::size_t> AliasOrder, RuntimeOrder;
		std::size_t NextOrder = 0;
	};
} // namespace VeroScript
