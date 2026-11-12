// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitNative.h"

#include <array>
#include <vector>

namespace VeroScript::AsmJit
{
	struct FJitCodeCache::FImpl
	{
		FImpl()
			: Arena(std::make_unique<JitNative::FArena>())
		{
		}

		void StoreEntry(std::uint16_t Index, std::unique_ptr<FJitFunctionEntry> Entry) noexcept;
		void ClearEntry(std::uint16_t Index) noexcept;

		// Keep dense function-index lookup without committing full artifacts for
		// every unvisited function. Published artifacts keep a stable address.
		std::vector<std::unique_ptr<FJitFunctionEntry>> Entries;
		std::vector<std::uint8_t> Published;
		std::size_t CachedEntryCount = 0;
		// Entries are immutable after insertion, including unpublished candidates.
		std::size_t EntryBytes = 0;
		std::size_t TextScratchBytes = 0;
		std::array<std::size_t, static_cast<std::size_t>(EJitEntryKind::VmTrampoline) + 1u> PublishedByKind{};
		std::size_t PublishedInlineSites = 0;
		std::unique_ptr<JitNative::FArena> Arena;
		std::size_t UnsupportedEntries = 0;
		std::size_t CompiledEntries = 0;
		std::size_t NativeEntries = 0;
		std::size_t CompileAttempts = 0;
		std::size_t CacheHits = 0;
		std::size_t NativeLeafEntries = 0;
		std::size_t NativeCfgEntries = 0;
		std::size_t BridgeTrampolineEntries = 0;
		std::size_t LinearTrampolineEntries = 0;
		std::size_t VmTrampolineEntries = 0;
		FJitPolicy Policy;
		std::vector<std::uint32_t> CallHeat;
		// Stable for one prepared module generation. Native callers load through
		// this table so a callee compiled after its caller becomes directly callable
		// without patching executable pages or embedding a stale code address.
		std::vector<void*> DirectI32Targets;
		std::vector<void*> DirectF64Targets;
		std::size_t HotnessDeferrals = 0;
		std::size_t HotFunctions = 0;
		std::size_t CompileBudgetSkips = 0;
		std::size_t CacheCapacitySkips = 0;
		std::size_t EagerCompileSkips = 0;
		std::size_t InlinedCallSites = 0;
	};
}
