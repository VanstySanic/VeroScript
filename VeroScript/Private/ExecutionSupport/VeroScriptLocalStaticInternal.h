// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VeroScript
{
	enum class ELocalStaticInitialization : std::uint8_t
	{
		Uninitialized,
		Initializing,
		Initialized,
	};

	struct FLocalStaticGuard
	{
		const void* Context = nullptr;
		std::size_t FrameIndex = 0;
		std::uint16_t GlobalSlot = 0;
		std::uint32_t BeginOffset = 0;
		std::uint32_t EndOffset = 0;
		std::size_t StackDepth = 0;
	};

	struct FLocalStaticState
	{
		std::vector<ELocalStaticInitialization> States;
		std::vector<std::uint16_t> Completed;
		std::vector<FLocalStaticGuard> Guards;
		bool bDestroying = false;
	};
}
