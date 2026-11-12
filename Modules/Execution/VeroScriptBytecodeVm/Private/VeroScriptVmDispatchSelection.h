// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExecutionServices.h"
#include <array>

namespace VeroScript::VmDispatchSelection
{
	inline constexpr std::uint8_t TextFeature = 1u;
	inline constexpr std::uint8_t IdentityInputFeature = 2u;
	inline constexpr std::uint8_t IndexedFeature = 4u;

	// A completed answer uses only reachable direct callees and fixed scratch
	// storage. A declined bounded analysis leaves the whole-module path available.
	bool TryDescribeReachableFeatures(const FBytecodeModule& Module,
	    const FBytecodeFunction& Root, std::uint8_t& OutFeatures) noexcept;

	// Owned by one bound backend instance. Unknown shapes retain the ordinary
	// dispatcher; classification can only enable an optional optimization.
	class FCache
	{
	public:
		void Reset() noexcept;
		std::uint8_t GetFeatures(const FBytecodeModule& Module, const FVmExecutionView& State) noexcept;
		std::uint8_t GetFunctionFeatures(const FBytecodeModule& Module, const FBytecodeFunction& Function) noexcept;
		std::size_t GetRetainedBytes() const noexcept;

	private:
		struct FEntry
		{
			const FBytecodeFunction* Function;
			std::uint8_t Features;
		};
		void Build(const FBytecodeModule& Module);
		std::vector<FEntry> Entries;
		std::array<FEntry, 8> InlineEntries{};
		std::size_t InlineEntryCount = 0u;
		std::size_t NextInlineEntry = 0u;
		const FBytecodeFunction* LastFunction = nullptr;
		std::uint8_t LastFeatures = 0u;
		bool bReady = false;
	};
}
