// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitLinear.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace VeroScript::AsmJit::JitLinear
{
	// The immutable CFG already gives every entry's operand depth. Pack only
	// those actual slots, rather than allocating a vector at every merge and
	// again for every instruction transfer. No zero-depth pointer arithmetic.
	class FPackedKindStates final
	{
	public:
		using K = ETypedLoopLocalKind;

		FPackedKindStates(const std::vector<FTypedLoopInstruction>& Instructions,
			const std::uint16_t MaximumDepth)
		{
			const auto Count = Instructions.size();
			if (Count >= Offsets.max_size() || Count > Initialized.max_size()) return;
			Offsets.resize(Count + 1u, 0u);
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const auto Depth = Instructions[Index].EntryStackDepth;
				if (Depth > MaximumDepth || Depth > Storage.max_size() - Offsets[Index]) return;
				Offsets[Index + 1u] = Offsets[Index] + Depth;
			}
			Storage.resize(Offsets.back());
			Initialized.resize(Count, 0u);
			bValid = true;
		}

		bool IsValid() const noexcept {return bValid;}
		std::size_t StorageSlots() const noexcept {return Storage.size();}

		bool Merge(const std::size_t Target, const std::vector<K>& Stack,
			bool& OutAdded)
		{
			OutAdded = false;
			if (!bValid || Target >= Initialized.size() ||
				Stack.size() != Offsets[Target + 1u] - Offsets[Target])
			{
				return false;
			}
			if (Initialized[Target])
			{
				return Stack.empty() || std::equal(Stack.begin(), Stack.end(),
					Storage.begin() + Offsets[Target]);
			}
			if (!Stack.empty())
			{
				std::copy(Stack.begin(), Stack.end(),
					Storage.begin() + Offsets[Target]);
			}
			Initialized[Target] = 1u;
			OutAdded = true;
			return true;
		}

		bool Load(const std::size_t Entry, std::vector<K>& OutStack) const
		{
			if (!bValid || Entry >= Initialized.size() || !Initialized[Entry]) return false;
			const auto Begin = Offsets[Entry], End = Offsets[Entry + 1u];
			if (Begin == End)
			{
				OutStack.clear();
			}
			else
			{
				OutStack.assign(Storage.begin() + Begin, Storage.begin() + End);
			}
			return true;
		}

	private:
		std::vector<std::size_t> Offsets;
		std::vector<K> Storage;
		std::vector<std::uint8_t> Initialized;
		bool bValid = false;
	};
}

