// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptJitCacheInternal.h"
#include "VeroScriptJitLinear.h"
#include "VeroScriptJitText.h"
#include "VeroScriptJitNative.h"
#include <algorithm>
#include <limits>

namespace VeroScript::AsmJit
{
	namespace
	{
		// Saturate rather than wrap a malformed/exceptionally large capacity estimate.
		struct FPayloadSize
		{
			void Add(std::size_t Count, std::size_t Width = 1)
			{
				const auto Max = std::numeric_limits<std::size_t>::max();
				if (Width != 0 && Count > (Max - Bytes) / Width)
				{
					Bytes = Max;
				}
				else
				{
					Bytes += Count * Width;
				}
			}

			template<class T>
			void Vector(const std::vector<T>& Values)
			{
				Add(Values.capacity(), sizeof(T));
			}

			// Counting SSO capacity again is intentionally conservative and portable.
			void String(const std::string& Value)
			{
				Add(Value.capacity());
				Add(1);
			}

			void Strings(const std::vector<std::string>& Values)
			{
				Vector(Values);
				for (const auto& Value : Values)
				{
					String(Value);
				}
			}

			void Function(const FBytecodeFunction& F)
			{
				Add(sizeof(F));
				String(F.Name);
				String(F.ModulePath);
				// FSourceLocation::FileLabel is a borrowed module-owned string_view.
				String(F.ClosureTypeName);
				Vector(F.Code);
				Strings(F.LocalNames);
				Vector(F.DebugLines);
				Vector(F.ExceptionRegions);
				for (const auto& Region : F.ExceptionRegions)
				{
					Vector(Region.Catches);
					for (const auto& Catch : Region.Catches)
					{
						String(Catch.TypeName);
					}
				}
				Vector(F.Captures);
				for (const auto& Capture : F.Captures)
				{
					String(Capture.Name);
					String(Capture.TypeName);
				}
				Vector(F.ParamFlags);
				Vector(F.DeclMeta);
				for (const auto& Meta : F.DeclMeta)
				{
					String(Meta.first);
					String(Meta.second);
				}
			}

			std::size_t Bytes = 0;
		};

		std::size_t EntryPayloadBytes(const FJitFunctionEntry& Entry) noexcept
		{
			FPayloadSize Size;
			Size.Add(sizeof(Entry));
			Size.Vector(Entry.RuntimeDebugLines);
			Size.Vector(Entry.ResolvedThisFields);
			Size.Vector(Entry.ScalarSetCallKinds);
			Size.Vector(Entry.InvariantLocalCompareSites);
			Size.Vector(Entry.ScalarNativeCallGuards);
			if (Entry.LinearFunction)
			{
				Size.Function(*Entry.LinearFunction);
			}
			if (!Entry.TypedLoopPlans)
			{
				return Size.Bytes;
			}
			// Shared immutable owners can be counted more than once, never undercounted.
			const auto& Plans = *Entry.TypedLoopPlans;
			Size.Add(sizeof(Plans));
			Size.Vector(Plans.Plans);
			Size.Vector(Plans.PlanAtOffset);
			for (const auto& Plan : Plans.Plans)
			{
				Size.Vector(Plan.Instructions);
				Size.Vector(Plan.TextConstants);
				if (Plan.TextBuffers)
				{
					Size.Add(Plan.TextBuffers->RetainedBytes());
				}
				Size.Vector(Plan.GenericPureMethods);
				Size.Vector(Plan.FieldMethods);
				for (const auto& Method : Plan.FieldMethods)
				{
					Size.Vector(Method.Instructions);
					Size.Vector(Method.FieldNameIndices);
				}
				for (const auto& Method : Plan.GenericPureMethods)
				{
					Size.Vector(Method.Instructions);
					Size.Vector(Method.FieldNameIndices);
				}
				Size.Vector(Plan.StateArrayVirtual.Methods);
				for (const auto& Method : Plan.StateArrayVirtual.Methods)
				{
					Size.Vector(Method.Instructions);
				}
			}
			return Size.Bytes;
		}
		std::size_t EntryTextScratchBytes(const FJitFunctionEntry& Entry) noexcept
		{
			FPayloadSize Size;
			if (Entry.TypedLoopPlans)
			{
				for (const auto &Plan : Entry.TypedLoopPlans->Plans)
				{
					if (Plan.TextBuffers)
					{
						Size.Add(Plan.TextBuffers->RetainedBytes());
					}
				}
			}
			return Size.Bytes;
		}
	}

	std::size_t FJitCodeCache::RetainedTextScratchBytes() const noexcept
	{
		return Impl->TextScratchBytes;
	}

	std::size_t FJitCodeCache::RetainedPayloadBytes() const
	{
		FPayloadSize Size;
		Size.Add(sizeof(*this));
		Size.Add(sizeof(FImpl));
		Size.Vector(Impl->Entries);
		Size.Vector(Impl->Published);
		Size.Vector(Impl->CallHeat);
		Size.Vector(Impl->DirectI32Targets);
		Size.Vector(Impl->DirectF64Targets);
		if (Impl->Arena)
		{
			Size.Add(sizeof(JitNative::FArena));
			Size.Add(Impl->Arena->ReservedBytes());
		}
		Size.Add(Impl->EntryBytes);
		return Size.Bytes;
	}

	void FJitCodeCache::FImpl::StoreEntry(std::uint16_t Index, std::unique_ptr<FJitFunctionEntry> Entry) noexcept
	{
		FPayloadSize Size{EntryBytes};
		Size.Add(EntryPayloadBytes(*Entry));
		FPayloadSize TextSize{TextScratchBytes};
		TextSize.Add(EntryTextScratchBytes(*Entry));
		TextScratchBytes = TextSize.Bytes;
		Entries[Index] = std::move(Entry);
		EntryBytes = Size.Bytes;
	}

	void FJitCodeCache::FImpl::ClearEntry(std::uint16_t Index) noexcept
	{
		auto& Slot = Entries[Index];
		if (!Slot) return;
		if (TextScratchBytes != std::numeric_limits<std::size_t>::max())
		{
			TextScratchBytes -= EntryTextScratchBytes(*Slot);
		}
		else
		{
			FPayloadSize TextSize;
			for (const auto &Entry : Entries)
			{
				if (Entry && Entry.get() != Slot.get())
				{
					TextSize.Add(EntryTextScratchBytes(*Entry));
				}
			}
			TextScratchBytes = TextSize.Bytes;
		}
		if (EntryBytes != std::numeric_limits<std::size_t>::max())
		{
			EntryBytes -= EntryPayloadBytes(*Slot);
			Slot.reset();
			return;
		}
		// Saturation can be recovered after discarding an over-budget candidate.
		Slot.reset();
		FPayloadSize Size;
		for (const auto& Remaining : Entries)
		{
			if (Remaining)
			{
				Size.Add(EntryPayloadBytes(*Remaining));
			}
		}
		EntryBytes = Size.Bytes;
	}

	std::size_t FJitCodeCache::MinimumPayloadBytes(const std::size_t FunctionCount)
	{
		FPayloadSize Size;
		Size.Add(sizeof(FJitCodeCache));
		Size.Add(sizeof(FImpl));
		Size.Add(sizeof(JitNative::FArena));
		Size.Add(std::min(FunctionCount, std::size_t{65536}), sizeof(std::unique_ptr<FJitFunctionEntry>) +
																  sizeof(std::uint8_t) + sizeof(std::uint32_t) +
																  2 * sizeof(void*));
		return Size.Bytes;
	}
}
