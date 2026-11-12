// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitCacheInternal.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace VeroScript::AsmJit
{
	FJitCodeCache::FJitCodeCache()
		: Impl(std::make_unique<FImpl>())
	{
	}

	FJitCodeCache::~FJitCodeCache() = default;

	void FJitCodeCache::InvalidateAll()
	{
		FImpl& Cache = *Impl;
		Cache.Entries.clear();
		Cache.Published.clear();
		Cache.CachedEntryCount = 0;
		Cache.EntryBytes = 0;
		Cache.TextScratchBytes = 0;
		Cache.PublishedByKind = {};
		Cache.PublishedInlineSites = 0;
		Cache.CallHeat.clear();
		Cache.DirectI32Targets.clear();
		Cache.DirectF64Targets.clear();
		Cache.UnsupportedEntries = 0;
		Cache.CompiledEntries = 0;
		Cache.NativeEntries = 0;
		Cache.CompileAttempts = 0;
		Cache.CacheHits = 0;
		Cache.NativeLeafEntries = 0;
		Cache.NativeCfgEntries = 0;
		Cache.BridgeTrampolineEntries = 0;
		Cache.LinearTrampolineEntries = 0;
		Cache.VmTrampolineEntries = 0;
		Cache.HotnessDeferrals = 0;
		Cache.HotFunctions = 0;
		Cache.CompileBudgetSkips = 0;
		Cache.CacheCapacitySkips = 0;
		Cache.EagerCompileSkips = 0;
		Cache.InlinedCallSites = 0;
		if (Cache.Arena)
		{
			Cache.Arena->Reset();
		}
	}

	void FJitCodeCache::SetPolicy(FJitPolicy InPolicy)
	{
		if (InPolicy.MixedHotCallThreshold == 0)
		{
			InPolicy.MixedHotCallThreshold = 1;
		}
		Impl->Policy = InPolicy;
		InvalidateAll();
	}

	const FJitPolicy& FJitCodeCache::GetPolicy() const
	{
		return Impl->Policy;
	}

	bool FJitCodeCache::ShouldCompileOnCall(const std::uint16_t FunctionIndex)
	{
		FImpl& Cache = *Impl;
		if (FunctionIndex >= Cache.CallHeat.size())
		{
			Cache.CallHeat.resize(static_cast<std::size_t>(FunctionIndex) + 1u, 0u);
		}
		std::uint32_t& Heat = Cache.CallHeat[FunctionIndex];
		if (Heat < Cache.Policy.MixedHotCallThreshold)
		{
			++Heat;
		}
		if (Heat < Cache.Policy.MixedHotCallThreshold)
		{
			++Cache.HotnessDeferrals;
			return false;
		}
		if (Heat == Cache.Policy.MixedHotCallThreshold)
		{
			++Cache.HotFunctions;
			// Saturate above the threshold so HotFunctions is counted once.
			if (Heat != std::numeric_limits<std::uint32_t>::max())
			{
				++Heat;
			}
		}
		return true;
	}

	const FJitFunctionEntry* FJitCodeCache::GetCandidate(const std::uint16_t FunctionIndex) const noexcept
	{
		return FunctionIndex < Impl->Entries.size() ? Impl->Entries[FunctionIndex].get() : nullptr;
	}

	bool FJitCodeCache::PublishCandidate(const std::uint16_t FunctionIndex) noexcept
	{
		auto& Cache = *Impl;
		const auto* Entry = GetCandidate(FunctionIndex);
		if (!Entry || Entry->Status != EJitCompileStatus::Ok || !Entry->Entry || FunctionIndex >= Cache.Published.size())
		{
			return false;
		}
		if (FunctionIndex < Cache.DirectI32Targets.size())
		{
			Cache.DirectI32Targets[FunctionIndex] =
				Entry->Kind == EJitEntryKind::NativeCfg
					&& Entry->NativeCode
					// Direct callers allocate the callee frame from the immutable
					// module declaration. An inlined kernel can require additional
					// locals/operand slots, so publishing it through this layout-free
					// table would let the kernel overrun the caller's scratch frame.
					// Such entries remain callable through the ordinary checked bridge.
					&& Entry->InlinedCallSites == 0u
					&& (Entry->ScalarAbi == EJitScalarAbi::I32
						|| Entry->ScalarAbi == EJitScalarAbi::U32)
				? Entry->NativeCode
				: nullptr;
		}
		if (FunctionIndex < Cache.DirectF64Targets.size())
		{
			Cache.DirectF64Targets[FunctionIndex] =
				Entry->Kind == EJitEntryKind::NativeCfg
					&& Entry->NativeCode
					&& Entry->InlinedCallSites == 0u
					&& Entry->ScalarAbi == EJitScalarAbi::F64
				? Entry->NativeCode
				: nullptr;
		}
		if (Cache.Published[FunctionIndex] == 0)
		{
			++Cache.PublishedByKind[static_cast<std::size_t>(Entry->Kind)];
			Cache.PublishedInlineSites += Entry->InlinedCallSites;
			Cache.Published[FunctionIndex] = 1;
		}
		return true;
	}

	void FJitCodeCache::DiscardCandidate(const std::uint16_t FunctionIndex) noexcept
	{
		if (FunctionIndex < Impl->Entries.size() &&
		    (FunctionIndex >= Impl->Published.size() || Impl->Published[FunctionIndex] == 0))
		{
			Impl->ClearEntry(FunctionIndex);
		}
	}

	bool FJitCodeCache::HasEntry(const std::uint16_t FunctionIndex) const
	{
		return TryGet(FunctionIndex) != nullptr;
	}

	const FJitFunctionEntry* FJitCodeCache::TryGet(
		const std::uint16_t FunctionIndex) const
	{
		if (FunctionIndex >= Impl->Published.size() || Impl->Published[FunctionIndex] == 0)
		{
			return nullptr;
		}
		const auto* Entry = GetCandidate(FunctionIndex);
		return Entry && Entry->Status == EJitCompileStatus::Ok && Entry->Entry ? Entry : nullptr;
	}

	void FJitCodeCache::PrepareModule(
		const FBytecodeModule& Module,
		const EExecutionBackend Backend,
		const std::size_t PreferredFunctionIndex)
	{
		// Function indices are module-local. Preparing any backend must discard
		// entries from the previous module, including Mixed-mode hot compilations.
		InvalidateAll();
		const std::size_t FunctionCount = Module.Functions.size();
		const std::size_t SlotCount =
			std::min(FunctionCount, static_cast<std::size_t>(0xFFFFu) + 1u);
		FImpl& Cache = *Impl;
		Cache.Entries.resize(SlotCount);
		Cache.Published.assign(SlotCount, 0);
		Cache.CallHeat.resize(SlotCount, 0u);
		Cache.DirectI32Targets.assign(SlotCount, nullptr);
		Cache.DirectF64Targets.assign(SlotCount, nullptr);
		if (Backend != EExecutionBackend::Jit)
		{
			// Mixed mode still benefits from dense lookup once a hot function is
			// compiled on demand. Allocate the module-local tables once, off the hot path.
			return;
		}
		std::size_t Limit = SlotCount;
		if (Cache.Policy.EagerCompileBudget != 0
			&& Limit > Cache.Policy.EagerCompileBudget)
		{
			Cache.EagerCompileSkips += Limit - Cache.Policy.EagerCompileBudget;
			Limit = Cache.Policy.EagerCompileBudget;
		}
		std::size_t Considered = 0;
		if (Considered < Limit && PreferredFunctionIndex < SlotCount)
		{
			(void)EnsureCompiled(
				Module,
				static_cast<std::uint16_t>(PreferredFunctionIndex));
			++Considered;
		}
		for (std::size_t Index = 0; Considered < Limit && Index < SlotCount; ++Index)
		{
			if (Index == PreferredFunctionIndex)
			{
				continue;
			}
			(void)EnsureCompiled(Module, static_cast<std::uint16_t>(Index));
			++Considered;
		}
	}

	FJitStats FJitCodeCache::GetStats() const
	{
		const FImpl& Cache = *Impl;
		FJitStats Stats;
		Stats.CompileAttempts = Cache.CompileAttempts;
		Stats.CacheHits = Cache.CacheHits;
		Stats.CompiledEntries = Cache.CompiledEntries;
		Stats.NativeLeafEntries = Cache.NativeLeafEntries;
		Stats.NativeCfgEntries = Cache.NativeCfgEntries;
		Stats.BridgeTrampolineEntries = Cache.BridgeTrampolineEntries;
		Stats.LinearTrampolineEntries = Cache.LinearTrampolineEntries;
		Stats.VmTrampolineEntries = Cache.VmTrampolineEntries;
		Stats.UnsupportedEntries = Cache.UnsupportedEntries;
		Stats.HotnessDeferrals = Cache.HotnessDeferrals;
		Stats.HotFunctions = Cache.HotFunctions;
		Stats.CompileBudgetSkips = Cache.CompileBudgetSkips;
		Stats.CacheCapacitySkips = Cache.CacheCapacitySkips;
		Stats.EagerCompileSkips = Cache.EagerCompileSkips;
		Stats.InlinedCallSites = Cache.InlinedCallSites;
		return Stats;
	}

	void FJitCodeCache::AccumulatePublishedStatistics(FJitStats& Stats) const noexcept
	{
		const auto& Published = Impl->PublishedByKind;
		Stats.NativeLeafEntries += Published[static_cast<std::size_t>(EJitEntryKind::NativeLeaf)];
		Stats.NativeCfgEntries += Published[static_cast<std::size_t>(EJitEntryKind::NativeCfg)];
		Stats.BridgeTrampolineEntries += Published[static_cast<std::size_t>(EJitEntryKind::BridgeTrampoline)];
		Stats.LinearTrampolineEntries += Published[static_cast<std::size_t>(EJitEntryKind::LinearTrampoline)];
		Stats.VmTrampolineEntries += Published[static_cast<std::size_t>(EJitEntryKind::VmTrampoline)];
		Stats.InlinedCallSites += Impl->PublishedInlineSites;
	}

	std::size_t FJitCodeCache::UnsupportedCount() const
	{
		return Impl->UnsupportedEntries;
	}

	std::size_t FJitCodeCache::CompiledCount() const
	{
		return Impl->CompiledEntries;
	}

	std::size_t FJitCodeCache::CompiledNativeCount() const
	{
		return Impl->NativeEntries;
	}

	JitNative::FArena& FJitCodeCache::NativeArena()
	{
		return *Impl->Arena;
	}

	void* const* FJitCodeCache::GetDirectI32Targets() const
	{
		return Impl->DirectI32Targets.empty()
			? nullptr
			: Impl->DirectI32Targets.data();
	}

	std::size_t FJitCodeCache::GetDirectI32TargetCount() const
	{
		return Impl->DirectI32Targets.size();
	}

	void* const* FJitCodeCache::GetDirectF64Targets() const
	{
		return Impl->DirectF64Targets.empty()
			? nullptr
			: Impl->DirectF64Targets.data();
	}

	std::size_t FJitCodeCache::GetDirectF64TargetCount() const
	{
		return Impl->DirectF64Targets.size();
	}
}
