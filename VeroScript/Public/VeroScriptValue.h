// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace VeroScript
{
	struct FMemoryWork;
	namespace Concurrent
	{
		struct FTypedTaskState;
	}
	class VeroScriptVm;
	struct FSourceMetadataSnapshot;
	static_assert(CHAR_BIT == 8,
	    "VeroScript bytecode requires 8-bit bytes");
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
	    "VeroScript float requires IEEE-754 binary32");
	static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
	    "VeroScript double requires IEEE-754 binary64");

	/**
	 * Struct value semantics (deep copy) vs class handle semantics (shared).
	 */
	enum class EScriptObjectKind : std::uint8_t
	{
		Struct,
		Class,
	};

	enum class ERetainedThisCapture : std::uint8_t
	{
		Strong = 0,
		Weak,
	};

	/**
	 * Compact open-addressing table used only by JIT-proven non-escaping scalar
	 * maps. Tombstones keep erasure independent of adjacent occupied runs.
	 * General script maps keep the fully featured FValue-backed
	 * unordered_map representation below.
	 */
	// FMap and FSet embed these table specializations in variant storage below.
	// Their complete template definitions must precede those container structs.
	template <typename K, typename V>
	class TScalarFlatMap
	{
		static_assert(std::is_integral_v<K>, "scalar JIT map keys must be integral");
		static_assert(std::is_nothrow_default_constructible_v<V> && std::is_nothrow_copy_constructible_v<V> && std::is_nothrow_copy_assignable_v<V>,
		    "scalar JIT map values must support non-throwing storage operations");

		struct FSlot
		{
			K Key{};
			V Value{};
			std::uint8_t State = 0; // 0 = empty, 1 = occupied, 2 = erased.
		};

	public:
		using key_type = K;
		using mapped_type = V;

		std::size_t size() const noexcept
		{
			return Count;
		}

		void reserve(const std::size_t DesiredCount)
		{
			if (DesiredCount == 0)
			{
				return;
			}
			constexpr std::size_t MaxPowerOfTwo =
			    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
			if (DesiredCount > MaxPowerOfTwo / 2u)
			{
				throw std::length_error("scalar map reserve is too large");
			}
			std::size_t Capacity = 8;
			// A 50% maximum load keeps probes short on the hot JIT-only path.
			const std::size_t Required = DesiredCount * 2u;
			while (Capacity < Required)
			{
				Capacity *= 2u;
			}
			if (Capacity > Slots.size())
			{
				Rehash(Capacity);
			}
		}

		const V* Find(const K Key) const noexcept
		{
			if (Slots.empty())
			{
				return nullptr;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				const FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					return nullptr;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					return &Slot.Value;
				}
				Index = (Index + 1u) & Mask;
			}
			return nullptr;
		}

		V* FindMutable(const K Key) noexcept
		{
			return const_cast<V*>(
			    static_cast<const TScalarFlatMap*>(this)->Find(Key));
		}

		bool Erase(const K Key) noexcept
		{
			if (Slots.empty())
			{
				return false;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					return false;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					const V Empty{};
					Slot.Value = Empty;
					Slot.State = 2;
					--Count;
					++Tombstones;
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}

		/**
		 * Build a zero-based dense value view with one sequential table scan.
		 * The returned pointers remain valid until the next insertion, erasure or reserve.
		 * This is used by typed JIT loops that have already proven that they do
		 * not mutate the map's key set; sparse or differently keyed maps fail
		 * without changing either the map or OutValues.
		 */
		bool TryGetDenseZeroBasedValues(
		    const std::size_t DomainSize,
		    std::vector<V*>& OutValues)
		{
			if (Count != DomainSize)
			{
				return false;
			}
			std::vector<V*> Dense(DomainSize, nullptr);
			for (FSlot& Slot : Slots)
			{
				if (Slot.State != 1)
				{
					continue;
				}
				if constexpr (std::is_signed_v<K>)
				{
					if (Slot.Key < 0)
					{
						return false;
					}
				}
				const std::uint64_t WideIndex = static_cast<std::uint64_t>(
				    static_cast<std::make_unsigned_t<K>>(Slot.Key));
				if (WideIndex >= DomainSize || Dense[static_cast<std::size_t>(WideIndex)] != nullptr)
				{
					return false;
				}
				Dense[static_cast<std::size_t>(WideIndex)] = &Slot.Value;
			}
			OutValues.swap(Dense);
			return true;
		}

		template <typename FVisitor>
		void ForEach(FVisitor&& Visitor) const
		{
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1)
				{
					Visitor(Slot.Key, Slot.Value);
				}
			}
		}

		void InsertOrAssign(const K Key, const V Value)
		{
			if (Slots.empty())
			{
				Rehash(8u);
			}
			else if (Count + Tombstones + 1u > Slots.size() / 2u)
			{
				// At the threshold, updating an existing key must not allocate.
				if (AssignIfPresent(Key, Value))
				{
					return;
				}
				constexpr std::size_t MaxPowerOfTwo =
				    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
				if (Slots.size() >= MaxPowerOfTwo)
				{
					throw std::length_error("scalar map capacity exhausted");
				}
				Rehash(Tombstones > Count / 2u ? Slots.size() : Slots.size() * 2u);
			}
			InsertNoGrow(Key, Value);
		}

		/**
		 * Inserts a caller-proven number of scalar entries after one capacity check.
		 * The generator receives (offset, key, value); duplicate generated keys keep
		 * normal insert-or-assign behavior. This is intentionally a counted API so a
		 * caller cannot accidentally overfill the no-grow insertion path.
		 */
		template <typename FGenerator>
		void InsertOrAssignGenerated(
		    const std::size_t InsertionCount,
		    FGenerator&& Generator)
		{
			if (InsertionCount == 0)
			{
				return;
			}
			if (InsertionCount > std::numeric_limits<std::size_t>::max() - Count)
			{
				throw std::length_error("scalar map insertion batch is too large");
			}
			reserve(Count + InsertionCount);
			if (Tombstones > Slots.size() / 2u - Count - InsertionCount)
			{
				Rehash(Slots.size());
			}
			for (std::size_t Offset = 0; Offset < InsertionCount; ++Offset)
			{
				K Key{};
				V Value{};
				Generator(Offset, Key, Value);
				InsertNoGrow(Key, Value);
			}
		}

	private:
		static std::size_t Hash(const K Key, const std::size_t Capacity) noexcept
		{
			using UKey = std::make_unsigned_t<K>;
			const std::uint64_t Bits = static_cast<std::uint64_t>(
			    static_cast<UKey>(Key));
			// In-range integer keys have distinct home slots, preserving dense build
			// and lookup locality. Mix larger keys so fixed strides and high words
			// cannot collapse when the power-of-two capacity is masked.
			if (Bits < Capacity) return static_cast<std::size_t>(Bits);
			std::uint64_t Mixed = Bits;
			Mixed = (Mixed ^ (Mixed >> 30u)) * std::uint64_t{0xbf58476d1ce4e5b9};
			Mixed = (Mixed ^ (Mixed >> 27u)) * std::uint64_t{0x94d049bb133111eb};
			return static_cast<std::size_t>(Mixed ^ (Mixed >> 31u));
		}

		bool AssignIfPresent(const K Key, const V Value) noexcept
		{
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					return false;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					Slot.Value = Value;
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}

		void InsertNoGrow(const K Key, const V Value) noexcept
		{
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			std::size_t FirstErased = Slots.size();
			for (;;)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					FSlot& Destination = FirstErased == Slots.size() ? Slot : Slots[FirstErased];
					Destination.Key = Key;
					Destination.Value = Value;
					if (Destination.State == 2)
					{
						--Tombstones;
					}
					Destination.State = 1;
					++Count;
					return;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					Slot.Value = Value;
					return;
				}
				if (Slot.State == 2 && FirstErased == Slots.size())
				{
					FirstErased = Index;
				}
				Index = (Index + 1u) & Mask;
			}
		}

		void Rehash(const std::size_t Capacity)
		{
			// Allocate first so failure leaves the original table intact. Reinsertion
			// is non-throwing for the scalar key/value set after the swap.
			std::vector<FSlot> Previous(Capacity);
			Slots.swap(Previous);
			Count = 0;
			Tombstones = 0;
			for (const FSlot& Slot : Previous)
			{
				if (Slot.State == 1)
				{
					InsertNoGrow(Slot.Key, Slot.Value);
				}
			}
		}

		std::vector<FSlot> Slots;
		std::size_t Count = 0;
		std::size_t Tombstones = 0;
	};

	/**
	 * Native hash storage for JIT-proven non-escaping scalar maps whose keys are
	 * already native value types but are not integral (currently string/wstring).
	 * Keeping this adapter separate from TScalarFlatMap avoids imposing throwing
	 * string moves/copies on the compact integral table while presenting the same
	 * small interface to the typed JIT bridge.
	 */
	template <typename K, typename V>
	class TScalarNativeHashMap
	{
		struct FSlot
		{
			K Key{};
			V Value{};
			std::size_t Hash = 0;
			// 0 = empty, 1 = occupied, 2 = erased. Keeping tombstones makes
			// erase non-throwing even when K has a throwing move assignment.
			std::uint8_t State = 0;
		};

	public:
		using key_type = K;
		using mapped_type = V;

		std::size_t size() const noexcept
		{
			return Count;
		}
		void reserve(const std::size_t DesiredCount)
		{
			if (DesiredCount == 0u)
			{
				return;
			}
			const std::size_t Required = CheckedCapacity(DesiredCount);
			if (Required > Slots.size())
			{
				Rehash(Required);
			}
		}
		const V* Find(const K& Key) const
		{
			return FindHashed(Key, HashKey(Key));
		}
		const V* FindHashed(const K& Key, const std::size_t Hash) const noexcept
		{
			const FSlot* const Slot = FindSlot(Key, Hash);
			return Slot ? &Slot->Value : nullptr;
		}
		V* FindMutable(const K& Key)
		{
			return FindMutableHashed(Key, HashKey(Key));
		}
		V* FindMutableHashed(const K& Key, const std::size_t Hash) noexcept
		{
			return const_cast<V*>(
			    static_cast<const TScalarNativeHashMap*>(this)->FindHashed(Key, Hash));
		}
		bool Erase(const K& Key)
		{
			if (Slots.empty())
			{
				return false;
			}
			const std::size_t Hash = HashKey(Key);
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					return false;
				}
				if (Slot.State == 1u && Slot.Hash == Hash && Slot.Key == Key)
				{
					Slot.State = 2u;
					--Count;
					++Tombstones;
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}
		void InsertOrAssign(const K& Key, const V& Value)
		{
			InsertOrAssignHashed(Key, Value, HashKey(Key));
		}
		void InsertOrAssignHashed(
		    const K& Key,
		    const V& Value,
		    const std::size_t Hash)
		{
			if (V* const Existing = FindMutableHashed(Key, Hash))
			{
				*Existing = Value;
				return;
			}
			EnsureInsertCapacity();
			InsertNoGrow(Key, Value, Hash);
		}
		template <typename FVisitor>
		void ForEach(FVisitor&& Visitor) const
		{
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1u)
				{
					Visitor(Slot.Key, Slot.Value);
				}
			}
		}

	private:
		static std::size_t HashKey(const K& Key)
		{
			return std::hash<K>{}(Key);
		}

		static std::size_t CheckedCapacity(const std::size_t DesiredCount)
		{
			constexpr std::size_t MaxPowerOfTwo =
			    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
			if (DesiredCount > MaxPowerOfTwo / 2u)
			{
				throw std::length_error("native scalar map reserve is too large");
			}
			std::size_t Capacity = 8u;
			const std::size_t Required = DesiredCount * 2u;
			while (Capacity < Required)
			{
				Capacity *= 2u;
			}
			return Capacity;
		}

		const FSlot* FindSlot(const K& Key, const std::size_t Hash) const noexcept
		{
			if (Slots.empty())
			{
				return nullptr;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				const FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					return nullptr;
				}
				if (Slot.State == 1u && Slot.Hash == Hash && Slot.Key == Key)
				{
					return &Slot;
				}
				Index = (Index + 1u) & Mask;
			}
			return nullptr;
		}

		void EnsureInsertCapacity()
		{
			if (Slots.empty())
			{
				Rehash(8u);
				return;
			}
			if (Count + Tombstones + 1u <= Slots.size() / 2u)
			{
				return;
			}
			const std::size_t Capacity = Tombstones > Count / 2u
			    ? Slots.size()
			    : Slots.size() * 2u;
			if (Capacity < Slots.size())
			{
				throw std::length_error("native scalar map capacity exhausted");
			}
			Rehash(Capacity);
		}

		void InsertNoGrow(const K& Key, const V& Value, const std::size_t Hash)
		{
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			std::size_t Reusable = Slots.size();
			for (;;)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					FSlot& Destination = Reusable == Slots.size()
					    ? Slot
					    : Slots[Reusable];
					Destination.Key = Key;
					Destination.Value = Value;
					Destination.Hash = Hash;
					if (Destination.State == 2u)
					{
						--Tombstones;
					}
					Destination.State = 1u;
					++Count;
					return;
				}
				if (Slot.State == 2u && Reusable == Slots.size())
				{
					Reusable = Index;
				}
				Index = (Index + 1u) & Mask;
			}
		}

		void Rehash(const std::size_t Capacity)
		{
			TScalarNativeHashMap Replacement;
			Replacement.Slots.resize(Capacity);
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1u)
				{
					Replacement.InsertNoGrow(Slot.Key, Slot.Value, Slot.Hash);
				}
			}
			Slots.swap(Replacement.Slots);
			Count = Replacement.Count;
			Tombstones = 0u;
		}

		std::vector<FSlot> Slots;
		std::size_t Count = 0u;
		std::size_t Tombstones = 0u;
	};

	/**
	 * Native hash storage for JIT-proven non-escaping scalar sets whose element
	 * type is not integral (currently string/wstring). It deliberately mirrors
	 * TScalarFlatSet's small API so typed execution does not need an FValue bridge
	 * for each probe.
	 */
	template <typename K>
	class TScalarNativeHashSet
	{
		struct FSlot
		{
			K Key{};
			std::size_t Hash = 0;
			std::uint8_t State = 0;
		};

	public:
		using key_type = K;

		std::size_t size() const noexcept
		{
			return Count;
		}
		void reserve(const std::size_t DesiredCount)
		{
			if (DesiredCount == 0u)
			{
				return;
			}
			const std::size_t Required = CheckedCapacity(DesiredCount);
			if (Required > Slots.size())
			{
				Rehash(Required);
			}
		}
		bool Contains(const K& Key) const
		{
			return ContainsHashed(Key, HashKey(Key));
		}
		bool ContainsHashed(const K& Key, const std::size_t Hash) const noexcept
		{
			return FindSlot(Key, Hash) != nullptr;
		}
		bool Insert(const K& Key)
		{
			const std::size_t Hash = HashKey(Key);
			if (FindSlot(Key, Hash))
			{
				return false;
			}
			EnsureInsertCapacity();
			InsertNoGrow(Key, Hash);
			return true;
		}
		bool Erase(const K& Key)
		{
			if (Slots.empty())
			{
				return false;
			}
			const std::size_t Hash = HashKey(Key);
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					return false;
				}
				if (Slot.State == 1u && Slot.Hash == Hash && Slot.Key == Key)
				{
					Slot.State = 2u;
					--Count;
					++Tombstones;
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}
		template <typename FVisitor>
		void ForEach(FVisitor&& Visitor) const
		{
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1u)
				{
					Visitor(Slot.Key);
				}
			}
		}

	private:
		static std::size_t HashKey(const K& Key)
		{
			return std::hash<K>{}(Key);
		}

		static std::size_t CheckedCapacity(const std::size_t DesiredCount)
		{
			constexpr std::size_t MaxPowerOfTwo =
			    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
			if (DesiredCount > MaxPowerOfTwo / 2u)
			{
				throw std::length_error("native scalar set reserve is too large");
			}
			std::size_t Capacity = 8u;
			const std::size_t Required = DesiredCount * 2u;
			while (Capacity < Required)
			{
				Capacity *= 2u;
			}
			return Capacity;
		}

		const FSlot* FindSlot(const K& Key, const std::size_t Hash) const noexcept
		{
			if (Slots.empty())
			{
				return nullptr;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				const FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					return nullptr;
				}
				if (Slot.State == 1u && Slot.Hash == Hash && Slot.Key == Key)
				{
					return &Slot;
				}
				Index = (Index + 1u) & Mask;
			}
			return nullptr;
		}

		void EnsureInsertCapacity()
		{
			if (Slots.empty())
			{
				Rehash(8u);
				return;
			}
			if (Count + Tombstones + 1u <= Slots.size() / 2u)
			{
				return;
			}
			const std::size_t Capacity = Tombstones > Count / 2u
			    ? Slots.size()
			    : Slots.size() * 2u;
			if (Capacity < Slots.size())
			{
				throw std::length_error("native scalar set capacity exhausted");
			}
			Rehash(Capacity);
		}

		void InsertNoGrow(const K& Key, const std::size_t Hash)
		{
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash & Mask;
			std::size_t Reusable = Slots.size();
			for (;;)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0u)
				{
					FSlot& Destination = Reusable == Slots.size()
					    ? Slot
					    : Slots[Reusable];
					Destination.Key = Key;
					Destination.Hash = Hash;
					if (Destination.State == 2u)
					{
						--Tombstones;
					}
					Destination.State = 1u;
					++Count;
					return;
				}
				if (Slot.State == 2u && Reusable == Slots.size())
				{
					Reusable = Index;
				}
				Index = (Index + 1u) & Mask;
			}
		}

		void Rehash(const std::size_t Capacity)
		{
			TScalarNativeHashSet Replacement;
			Replacement.Slots.resize(Capacity);
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1u)
				{
					Replacement.InsertNoGrow(Slot.Key, Slot.Hash);
				}
			}
			Slots.swap(Replacement.Slots);
			Count = Replacement.Count;
			Tombstones = 0u;
		}

		std::vector<FSlot> Slots;
		std::size_t Count = 0u;
		std::size_t Tombstones = 0u;
	};

	/**
	 * Compact open-addressing set used only for JIT-proven non-escaping integral sets.
	 */
	template <typename K>
	class TScalarFlatSet
	{
		static_assert(std::is_integral_v<K>, "scalar JIT set elements must be integral");
		struct FSlot
		{
			K Key{};
			// 0 = empty, 1 = occupied, 2 = erased. Tombstones preserve probe chains.
			std::uint8_t State = 0;
		};

	public:
		using key_type = K;

		// Borrowed table metadata for a proven read-only region. Any mutation,
		// move or destruction of the set ends the borrow; never cache this view.
		class FReadOnlyProbe
		{
		public:
			std::size_t GetCapacity() const noexcept
			{
				return Capacity;
			}

			// Snapshot only a bounded non-negative key universe for a read-only loop.
			// Out-of-range and erased keys cannot affect queries in that universe.
			void BuildNonnegativeBitmap(std::uint64_t* Words, const std::size_t WordCount) const noexcept
			{
				std::fill_n(Words, WordCount, std::uint64_t{0});
				for (std::size_t Index = 0; Index < Capacity; ++Index)
				{
					const auto& Slot = Data[Index];
					const auto Bits = static_cast<std::uint64_t>(static_cast<std::make_unsigned_t<K>>(Slot.Key));
					if (Slot.State == 1 && Bits / 64u < WordCount)
					{
						Words[static_cast<std::size_t>(Bits / 64u)] |= std::uint64_t{1} << (Bits % 64u);
					}
				}
			}

			bool Contains(const K Key) const noexcept
			{
				if (Capacity == 0u)
				{
					return false;
				}
				const std::size_t Mask = Capacity - 1u;
				std::size_t Index = Hash(Key, Capacity) & Mask;
				for (std::size_t Probe = 0; Probe < Capacity; ++Probe)
				{
					const FSlot& Slot = Data[Index];
					if (Slot.State == 0)
					{
						return false;
					}
					if (Slot.State == 1 && Slot.Key == Key)
					{
						return true;
					}
					Index = (Index + 1u) & Mask;
				}
				return false;
			}

		private:
			friend class TScalarFlatSet;
			FReadOnlyProbe(const FSlot* InData, const std::size_t InCapacity) noexcept
			    : Data(InData), Capacity(InCapacity)
			{
			}
			const FSlot* Data;
			std::size_t Capacity;
		};

		FReadOnlyProbe BorrowReadOnlyProbe() const noexcept
		{
			return FReadOnlyProbe(Slots.data(), Slots.size());
		}

		std::size_t size() const noexcept
		{
			return Count;
		}
		void reserve(const std::size_t DesiredCount)
		{
			if (DesiredCount == 0)
			{
				return;
			}
			constexpr std::size_t MaxPowerOfTwo =
			    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
			if (DesiredCount > MaxPowerOfTwo / 2u)
			{
				throw std::length_error("scalar set reserve is too large");
			}
			std::size_t Capacity = 8;
			const std::size_t Required = DesiredCount * 2u;
			while (Capacity < Required)
			{
				Capacity *= 2u;
			}
			if (Capacity > Slots.size())
			{
				Rehash(Capacity);
			}
		}

		bool Contains(const K Key) const noexcept
		{
			if (Slots.empty())
			{
				return false;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				const FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					return false;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}

		bool Insert(const K Key)
		{
			if (Slots.empty())
			{
				Rehash(8u);
			}
			else if (Count + Tombstones + 1u > Slots.size() / 2u)
			{
				if (Contains(Key))
				{
					return false;
				}
				constexpr std::size_t MaxPowerOfTwo =
				    std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
				if (Slots.size() >= MaxPowerOfTwo)
				{
					throw std::length_error("scalar set capacity exhausted");
				}
				Rehash(Tombstones > Count / 2u ? Slots.size() : Slots.size() * 2u);
			}
			return InsertNoGrow(Key);
		}

		// Counted counterpart of Insert() for JIT-proven scalar batches.
		template <typename FGenerator>
		void InsertGenerated(
		    const std::size_t InsertionCount,
		    FGenerator&& Generator)
		{
			if (InsertionCount == 0)
			{
				return;
			}
			if (InsertionCount > std::numeric_limits<std::size_t>::max() - Count)
			{
				throw std::length_error("scalar set insertion batch is too large");
			}
			reserve(Count + InsertionCount);
			// reserve() accounts for live keys only. Repeated generated batches can
			// otherwise fill every empty slot with tombstones, so InsertNoGrow never
			// reaches its terminating empty slot. Keep the same occupied-load bound
			// as Insert(), clearing tombstones once before generating the batch.
			if (Tombstones > Slots.size() / 2u - Count - InsertionCount)
			{
				Rehash(Slots.size());
			}
			for (std::size_t Offset = 0; Offset < InsertionCount; ++Offset)
			{
				K Key{};
				Generator(Offset, Key);
				(void)InsertNoGrow(Key);
			}
		}

		bool Erase(const K Key) noexcept
		{
			if (Slots.empty())
			{
				return false;
			}
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			for (std::size_t Probe = 0; Probe < Slots.size(); ++Probe)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					return false;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					Slot.State = 2;
					--Count;
					++Tombstones;
					return true;
				}
				Index = (Index + 1u) & Mask;
			}
			return false;
		}

		template <typename FVisitor>
		void ForEach(FVisitor&& Visitor) const
		{
			for (const FSlot& Slot : Slots)
			{
				if (Slot.State == 1)
				{
					Visitor(Slot.Key);
				}
			}
		}

	private:
		static std::size_t Hash(const K Key, const std::size_t Capacity) noexcept
		{
			using UKey = std::make_unsigned_t<K>;
			const std::uint64_t Bits = static_cast<std::uint64_t>(
			    static_cast<UKey>(Key));
			// Dense keys remain collision-free; larger keys need all bits mixed.
			if (Bits < Capacity) return static_cast<std::size_t>(Bits);
			std::uint64_t Mixed = Bits;
			Mixed = (Mixed ^ (Mixed >> 30u)) * std::uint64_t{0xbf58476d1ce4e5b9};
			Mixed = (Mixed ^ (Mixed >> 27u)) * std::uint64_t{0x94d049bb133111eb};
			return static_cast<std::size_t>(Mixed ^ (Mixed >> 31u));
		}

		bool InsertNoGrow(const K Key) noexcept
		{
			const std::size_t Mask = Slots.size() - 1u;
			std::size_t Index = Hash(Key, Slots.size()) & Mask;
			std::size_t FirstErased = Slots.size();
			for (;;)
			{
				FSlot& Slot = Slots[Index];
				if (Slot.State == 0)
				{
					const std::size_t Target = FirstErased == Slots.size() ? Index : FirstErased;
					FSlot& Destination = Slots[Target];
					if (Destination.State == 2)
					{
						--Tombstones;
					}
					Destination.Key = Key;
					Destination.State = 1;
					++Count;
					return true;
				}
				if (Slot.State == 1 && Slot.Key == Key)
				{
					return false;
				}
				if (Slot.State == 2 && FirstErased == Slots.size())
				{
					FirstErased = Index;
				}
				Index = (Index + 1u) & Mask;
			}
		}

		void Rehash(const std::size_t Capacity)
		{
			std::vector<FSlot> Previous(Capacity);
			Slots.swap(Previous);
			Count = 0;
			Tombstones = 0;
			for (const FSlot& Slot : Previous)
			{
				if (Slot.State == 1)
				{
					(void)InsertNoGrow(Slot.Key);
				}
			}
		}

		std::vector<FSlot> Slots;
		std::size_t Count = 0;
		std::size_t Tombstones = 0;
	};

	struct FScriptObject;
	struct FContainerMutationOrigin
	{
		std::weak_ptr<FScriptObject> Object;
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		// Reference cells use the same origin record. This bit distinguishes a
		// direct field alias from an alias to an element of a field-owned container.
		bool bContainerElement = false;
	};

	struct FNativeArray
	{
		friend class VeroScriptVm;

		struct FStorage;

		FNativeArray();
		~FNativeArray();
		FNativeArray(const FNativeArray& Other);
		FNativeArray& operator=(const FNativeArray& Other);
		FNativeArray(FNativeArray&& Other) noexcept;
		FNativeArray& operator=(FNativeArray&& Other) noexcept;

		FStorage& Storage() noexcept;
		const FStorage& Storage() const noexcept;
		FContainerMutationOrigin& Origin() noexcept;
		const FContainerMutationOrigin& Origin() const noexcept;
		std::string& TypeName() noexcept;
		const std::string& TypeName() const noexcept;
		std::string& ElementTypeName() noexcept;
		const std::string& ElementTypeName() const noexcept;
		std::uint32_t& Length() noexcept;
		std::uint32_t Length() const noexcept;

	private:
		// Native arrays retain value semantics through a deep-copying unique
		// payload. The complete lightweight wrapper is declared before FValue so
		// std::variant traits are portable across MSVC, Clang and GCC.
		std::unique_ptr<FStorage> Data;
		// Only a move can preserve a prepared interception result. Copies cross
		// a new value boundary; this weak stamp never extends the payload lifetime.
		std::weak_ptr<const void> PreparedReturnOwner;
	};
	struct FDynamicArray;
	struct FMap;
	struct FSet;
	struct FQueue;
	struct FStack;
	struct FOrderedMap;
	struct FDeque;
	struct FChannel;
	struct FFunctionValue;
	struct FHostObject;
	struct FScriptVariantStorage;
	struct FNativeCallContext;

	// Handles break the recursive dependency between FValue and its containers.
	using FObjectHandle = std::shared_ptr<FScriptObject>;
	using FDynamicArrayHandle = std::shared_ptr<FDynamicArray>;
	using FMapHandle = std::shared_ptr<FMap>;
	using FSetHandle = std::shared_ptr<FSet>;
	using FQueueHandle = std::shared_ptr<FQueue>;
	using FStackHandle = std::shared_ptr<FStack>;
	using FOrderedMapHandle = std::shared_ptr<FOrderedMap>;
	using FDequeHandle = std::shared_ptr<FDeque>;
	using FChannelHandle = std::shared_ptr<FChannel>;
	using FFunctionHandle = std::shared_ptr<FFunctionValue>;
	using FHostObjectHandle = std::shared_ptr<FHostObject>;
	using FScriptVariantHandle = std::shared_ptr<FScriptVariantStorage>;

	/**
	 * Opaque session-owned identity for a callable retained beyond the native call
	 * that produced it. The id is meaningful only to the VeroScriptSession that
	 * created it; generation is carried for cheap stale-handle rejection.
	 */
	struct FRetainedCallableHandle
	{
		friend bool operator==(const FRetainedCallableHandle& Left,
		    const FRetainedCallableHandle& Right)
		{
			return Left.Id == Right.Id && Left.ProgramGeneration == Right.ProgramGeneration;
		}

		bool IsValid() const
		{
			return Id != 0 && ProgramGeneration != 0;
		}

		explicit operator bool() const
		{
			return IsValid();
		}

		std::uint64_t Id = 0;
		std::uint64_t ProgramGeneration = 0;
	};

	/**
	 * Hash/equality adapters for host keys. Class handles use identity by default;
	 * host value types may stamp deterministic @hash/@equals callbacks.
	 */
	struct FHostObjectKeyHash
	{
		std::size_t operator()(const FHostObjectHandle& Value) const noexcept;
	};

	struct FHostObjectKeyEqual
	{
		bool operator()(
		    const FHostObjectHandle& Left,
		    const FHostObjectHandle& Right) const noexcept;
	};

	// Value comparison does not require the separate unordered-key hash protocol.
	struct FHostObjectValueEqual
	{
		bool operator()(const FHostObjectHandle& Left, const FHostObjectHandle& Right) const noexcept;
	};

	/**

	 * Parameter alias to a local slot in a call frame (v1.24).

	 */
	struct FLocalRef
	{
		std::uint32_t FrameIndex = 0;
		std::uint16_t Slot = 0;
	};

	/**

	 * Parameter alias to a global slot (v1.24).

	 */
	struct FGlobalRef
	{
		std::uint16_t Slot = 0;
	};

	/**

	 * Parameter alias to a field on a this/object handle (v1.24).

	 */
	struct FThisFieldRef
	{
		FObjectHandle Object;
		std::uint16_t FieldIndex = 0;
	};

	/**
	 * Alias to Elements[Index] inside an array/deque local (or IndexRef/ref thereto).
	 * Emit GetIndexRef; used by `for (ref T x : xs)`, `ref xs[i]`, nested `ref a[i][j]`.
	 */
	struct FContainerElementReference;

	struct FIndexRef
	{
		std::uint32_t FrameIndex = 0;
		std::uint16_t ArraySlot = 0;
		std::int64_t Index = 0;
		// Empty only for a compiler-owned assignment path; user borrows are bound.
		std::shared_ptr<FContainerElementReference> Target;
	};

	struct FEnumValue
	{
		bool operator==(const FEnumValue& Other) const
		{
			return TypeName == Other.TypeName && Value == Other.Value;
		}

		std::string TypeName;
		std::int64_t Value = 0;
	};

	struct FEnumValueHash
	{
		std::size_t operator()(const FEnumValue& Value) const noexcept
		{
			const std::size_t TypeHash = std::hash<std::string>{}(Value.TypeName);
			const std::size_t ValueHash = std::hash<std::int64_t>{}(Value.Value);
			return TypeHash ^ (ValueHash + static_cast<std::size_t>(0x9e3779b9u) + (TypeHash << 6u) + (TypeHash >> 2u));
		}
	};

	// FMapEntryKey needs complete scalar alternatives before FMapEntryRef can use it.
	// Map / ordered_map key payload for FMapEntryRef (avoids nesting full FValue).
	using FMapEntryKey =
	    std::variant<
	        std::int32_t,
	        std::uint32_t,
	        std::int64_t,
	        std::uint64_t,
	        std::string,
	        std::u16string,
	        FEnumValue,
	        FHostObjectHandle>;

	/**
	 * Alias to a map/ordered_map value for a specific key.
	 * Emit GetMapEntryRef; used by `ref m[k]`, nested `ref m[k][i]`, `for (ref V v : m)`.
	 */
	struct FContainerElementReference
	{
		using FContainer = std::variant<std::weak_ptr<FDynamicArray>, std::weak_ptr<const void>,
		    std::weak_ptr<FMap>, std::weak_ptr<FOrderedMap>, std::weak_ptr<FDeque>>;

		FContainer Container;
		FNativeArray::FStorage* NativeStorage = nullptr;
		std::weak_ptr<FDynamicArray> Cell;
		std::optional<FMapEntryKey> Key;
		FContainerMutationOrigin Origin;
		bool bBound = false;
	};

	struct FMapEntryRef
	{
		void SetKey(FMapEntryKey InKey)
		{
			Target = std::make_shared<FContainerElementReference>();
			Target->Key = std::move(InKey);
		}

		std::uint32_t FrameIndex = 0;
		std::uint16_t MapSlot = 0;

		// String/enum/host keys make FMapEntryKey too large to embed in every
		// FValue alternative. Map references are comparatively rare and copied as
		// aliases, so one immutable shared key reduces the universal value footprint.
		std::shared_ptr<FContainerElementReference> Target;
	};

	// FValue requires complete value alternatives above; containers below store FValue.
	using FValue = std::variant<
	    std::int32_t,
	    std::int64_t,
	    double,
	    std::string,
	    bool,
	    FObjectHandle,
	    FNativeArray,
	    FDynamicArrayHandle,
	    std::uint32_t,
	    std::uint64_t,
	    std::u16string,
	    FMapHandle,
	    FSetHandle,
	    FQueueHandle,
	    FStackHandle,
	    FOrderedMapHandle,
	    FDequeHandle,
	    FLocalRef,
	    FGlobalRef,
	    FThisFieldRef,
	    FIndexRef,
	    FMapEntryRef,
	    FChannelHandle,
	    FFunctionHandle,
	    FEnumValue,
	    FHostObjectHandle,
	    float,
	    std::int8_t,
	    std::uint8_t,
	    std::int16_t,
	    std::uint16_t,
	    FScriptVariantHandle>;

	// FValue crosses every operand/local boundary. Large value alternatives must
	// stay indirect so a container feature cannot inflate all scalar execution.
	// The bound leaves ample room for supported standard-library ABI differences.
	static_assert(sizeof(FValue) <= 64,
	    "FValue hot-path footprint regressed; store large alternatives indirectly");

	/**
	 * Dynamic array value (array<T>) with reference semantics via shared handle.
	 */
	struct FDynamicArray
	{
		using FScalarElements = std::variant<
		    std::monostate,
		    std::vector<std::int8_t>,
		    std::vector<std::uint8_t>,
		    std::vector<std::int16_t>,
		    std::vector<std::uint16_t>,
		    std::vector<std::int32_t>,
		    std::vector<std::uint32_t>,
		    std::vector<std::int64_t>,
		    std::vector<std::uint64_t>,
		    std::vector<float>,
		    std::vector<double>,
		    std::vector<bool>,
		    std::vector<std::string>,
		    std::vector<std::u16string>>;

		bool HasScalarElements() const noexcept
		{
			return ScalarElements.index() != 0;
		}

		std::size_t ScalarSize() const noexcept
		{
			return std::visit(
			    [](const auto& Values) -> std::size_t
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (std::is_same_v<TValues, std::monostate>)
				{
					return 0;
				}
				else
				{
					return Values.size();
				}
			},
			    ScalarElements);
		}

		std::size_t ScalarCapacity() const noexcept
		{
			return std::visit(
			    [](const auto& Values) -> std::size_t
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (std::is_same_v<TValues, std::monostate>)
				{
					return 0;
				}
				else
				{
					return Values.capacity();
				}
			},
			    ScalarElements);
		}

		void ReserveScalar(const std::size_t Capacity)
		{
			std::visit(
			    [Capacity](auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					Values.reserve(Capacity);
				}
			},
			    ScalarElements);
		}

		void ClearScalar() noexcept
		{
			std::visit(
			    [](auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					Values.clear();
				}
			},
			    ScalarElements);
		}

		bool EnableScalarStorage()
		{
			if (HasScalarElements())
			{
				return true;
			}
			if (!Elements.empty())
			{
				return false;
			}
			const std::string& Type = ElementTypeName;
			if (Type == "char" || Type == "int8")
			{
				ScalarElements.emplace<std::vector<std::int8_t>>();
			}
			else if (Type == "unsigned char" || Type == "uint8")
			{
				ScalarElements.emplace<std::vector<std::uint8_t>>();
			}
			else if (Type == "short" || Type == "int16")
			{
				ScalarElements.emplace<std::vector<std::int16_t>>();
			}
			else if (Type == "unsigned short" || Type == "uint16")
			{
				ScalarElements.emplace<std::vector<std::uint16_t>>();
			}
			else if (Type == "int" || Type == "int32")
			{
				ScalarElements.emplace<std::vector<std::int32_t>>();
			}
			else if (Type == "unsigned int" || Type == "uint" || Type == "unsigned" || Type == "uint32")
			{
				ScalarElements.emplace<std::vector<std::uint32_t>>();
			}
			else if (Type == "int64" || Type == "long")
			{
				ScalarElements.emplace<std::vector<std::int64_t>>();
			}
			else if (Type == "uint64" || Type == "unsigned long")
			{
				ScalarElements.emplace<std::vector<std::uint64_t>>();
			}
			else if (Type == "float")
			{
				ScalarElements.emplace<std::vector<float>>();
			}
			else if (Type == "double")
			{
				ScalarElements.emplace<std::vector<double>>();
			}
			else if (Type == "bool")
			{
				ScalarElements.emplace<std::vector<bool>>();
			}
			else if (Type == "string")
			{
				ScalarElements.emplace<std::vector<std::string>>();
			}
			else if (Type == "wstring")
			{
				ScalarElements.emplace<std::vector<std::u16string>>();
			}
			else
			{
				return false;
			}
			return true;
		}

		bool TryGetScalar(const std::size_t Index, FValue& OutValue) const
		{
			return std::visit(
			    [&](const auto& Values) -> bool
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					if (Index >= Values.size()) return false;
					using TValue = typename TValues::value_type;
					OutValue = static_cast<TValue>(Values[Index]);
					return true;
				}
				return false;
			},
			    ScalarElements);
		}

		bool TrySetScalar(const std::size_t Index, const FValue& Value)
		{
			return std::visit(
			    [&](auto& Values) -> bool
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					if (Index >= Values.size()) return false;
					using TValue = typename TValues::value_type;
					if (const auto* Source = std::get_if<TValue>(&Value))
					{
						Values[Index] = *Source;
						return true;
					}
				}
				return false;
			},
			    ScalarElements);
		}

		bool TryPushScalar(const FValue& Value)
		{
			bool bStored = false;
			std::visit(
			    [&](auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					using TValue = typename TValues::value_type;
					if (const auto* Source = std::get_if<TValue>(&Value))
					{
						Values.push_back(*Source);
						bStored = true;
					}
				}
			},
			    ScalarElements);
			return bStored;
		}

		bool EraseScalarAt(const std::size_t Index)
		{
			if (Index >= ScalarSize())
			{
				return false;
			}
			bool bErased = false;
			std::visit(
			    [&](auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					Values.erase(Values.begin() + static_cast<std::ptrdiff_t>(Index));
					bErased = true;
				}
			},
			    ScalarElements);
			return bErased;
		}

		bool TryRemoveFirstScalar(const FValue& Value, bool& bOutRemoved)
		{
			bOutRemoved = false;
			bool bTypeMatched = false;
			std::visit(
			    [&](auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					using TValue = typename TValues::value_type;
					if (const auto* Source = std::get_if<TValue>(&Value))
					{
						bTypeMatched = true;
						const auto It = std::find(Values.begin(), Values.end(), *Source);
						if (It != Values.end())
						{
							Values.erase(It);
							bOutRemoved = true;
						}
					}
				}
			},
			    ScalarElements);
			return bTypeMatched;
		}

		std::size_t ElementCount() const noexcept
		{
			return HasScalarElements() ? ScalarSize() : Elements.size();
		}

		const std::string* BorrowString(const std::size_t Index) const noexcept
		{
			if (const auto* Values = std::get_if<std::vector<std::string>>(
			        &ScalarElements))
			{
				return Index < Values->size() ? &(*Values)[Index] : nullptr;
			}
			if (Index >= Elements.size())
			{
				return nullptr;
			}
			return std::get_if<std::string>(&Elements[Index]);
		}

		const std::u16string* BorrowWString(const std::size_t Index) const noexcept
		{
			if (const auto* Values = std::get_if<std::vector<std::u16string>>(
			        &ScalarElements))
			{
				return Index < Values->size() ? &(*Values)[Index] : nullptr;
			}
			if (Index >= Elements.size())
			{
				return nullptr;
			}
			return std::get_if<std::u16string>(&Elements[Index]);
		}

		void MaterializeBoxedElements()
		{
			if (!HasScalarElements())
			{
				return;
			}
			Elements.clear();
			Elements.reserve(ScalarSize());
			std::visit(
			    [&](const auto& Values)
			{
				using TValues = std::decay_t<decltype(Values)>;
				if constexpr (!std::is_same_v<TValues, std::monostate>)
				{
					using TValue = typename TValues::value_type;
					for (const auto& Value : Values)
					{
						Elements.emplace_back(static_cast<TValue>(Value));
					}
				}
			},
			    ScalarElements);
			ScalarElements.emplace<std::monostate>();
		}

		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;

		// Canonical compact storage for primitive array<T>. Address-taking and
		// mutable host ABI boundaries materialize Elements only when stable FValue
		// slots are required.
		FScalarElements ScalarElements;
		std::vector<FValue> Elements;
	};

	/**
	 * Fixed-length native array value (T[N], T[N][M]..., or string[]) on the VM stack.
	 * Multi-dim types are nested: "int[2][3]" has Length=2 and each Element is int[3].
	 */
	struct FNativeArray::FStorage
	{
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;
		std::uint32_t Length = 0;
		std::vector<FValue> Elements;
		std::shared_ptr<const void> ReferenceLifetime;
	};

	inline FNativeArray::FNativeArray()
	    : Data(std::make_unique<FStorage>())
	{
	}

	inline FNativeArray::~FNativeArray() = default;

	inline FNativeArray::FNativeArray(const FNativeArray& Other)
	    : Data(Other.Data
	              ? std::make_unique<FStorage>(*Other.Data)
	              : std::make_unique<FStorage>())
	{
		Data->ReferenceLifetime.reset();
	}

	inline FNativeArray& FNativeArray::operator=(const FNativeArray& Other)
	{
		if (this != &Other)
		{
			auto Copy = Other.Data
			    ? std::make_unique<FStorage>(*Other.Data)
			    : std::make_unique<FStorage>();
			Copy->ReferenceLifetime.reset();
			Data = std::move(Copy);
			PreparedReturnOwner.reset();
		}
		return *this;
	}

	inline FNativeArray::FNativeArray(FNativeArray&& Other) noexcept = default;
	inline FNativeArray& FNativeArray::operator=(FNativeArray&& Other) noexcept = default;

	inline FNativeArray::FStorage& FNativeArray::Storage() noexcept
	{
		return *Data;
	}

	inline const FNativeArray::FStorage& FNativeArray::Storage() const noexcept
	{
		return *Data;
	}

	inline FContainerMutationOrigin& FNativeArray::Origin() noexcept
	{
		return Data->MutationOrigin;
	}

	inline const FContainerMutationOrigin& FNativeArray::Origin() const noexcept
	{
		return Data->MutationOrigin;
	}

	inline std::string& FNativeArray::TypeName() noexcept
	{
		return Data->TypeName;
	}

	inline const std::string& FNativeArray::TypeName() const noexcept
	{
		return Data->TypeName;
	}

	inline std::string& FNativeArray::ElementTypeName() noexcept
	{
		return Data->ElementTypeName;
	}

	inline const std::string& FNativeArray::ElementTypeName() const noexcept
	{
		return Data->ElementTypeName;
	}

	inline std::uint32_t& FNativeArray::Length() noexcept
	{
		return Data->Length;
	}

	inline std::uint32_t FNativeArray::Length() const noexcept
	{
		return Data->Length;
	}

	// VM-private heap cell used to give local aliases and reference captures a
	// lifetime independent of the call frame. It deliberately reuses the dynamic
	// array handle alternative so the public FValue ABI and bytecode value tags do
	// not change; script code can never construct this reserved type name.
	inline constexpr std::string_view ClosureReferenceCellTypeName =
	    "__veroscript.closure_reference_cell";
	inline constexpr std::string_view ClosureReadonlyCellTypeName =
	    "__veroscript.closure_readonly_cell";

	inline bool IsReadonlyClosureCell(const FValue& Value)
	{
		const auto* const Handle = std::get_if<FDynamicArrayHandle>(&Value);
		return Handle && *Handle && (*Handle)->TypeName == ClosureReadonlyCellTypeName && (*Handle)->Elements.size() == 1;
	}

	inline bool IsClosureReferenceCell(const FValue& Value)
	{
		const auto* const Handle = std::get_if<FDynamicArrayHandle>(&Value);
		return Handle && *Handle && ((*Handle)->TypeName == ClosureReferenceCellTypeName || (*Handle)->TypeName == ClosureReadonlyCellTypeName) && (*Handle)->Elements.size() == 1;
	}

	// VM closure cells store a script-visible value behind a private representation. Persistent
	// object/global/container storage may contain one after a reference capture, so
	// host-facing validation, serialization, copying and comparison must not expose
	// the private representation.  A small bound also makes malformed host-provided
	// cyclic cells fail safely instead of looping forever.
	inline FValue* UnwrapClosureReferenceCell(FValue* Value)
	{
		for (std::size_t Depth = 0; Value && Depth < 64; ++Depth)
		{
			if (!IsClosureReferenceCell(*Value))
			{
				return Value;
			}
			FValue* const Next = &std::get<FDynamicArrayHandle>(*Value)->Elements[0];
			if (Next == Value)
			{
				return nullptr;
			}
			Value = Next;
		}
		return nullptr;
	}

	inline const FValue* UnwrapClosureReferenceCell(const FValue* Value)
	{
		for (std::size_t Depth = 0; Value && Depth < 64; ++Depth)
		{
			if (!IsClosureReferenceCell(*Value))
			{
				return Value;
			}
			const FValue* const Next =
			    &std::get<FDynamicArrayHandle>(*Value)->Elements[0];
			if (Next == Value)
			{
				return nullptr;
			}
			Value = Next;
		}
		return nullptr;
	}

	/**
	 * Map value (map<K,V>) with reference semantics via shared handle.
	 * Unordered dictionary (hash); keys() iteration order is unspecified.
	 * Use ordered_map for insertion-ordered dictionaries.
	 */
	struct FMap
	{
		template <typename K, typename V>
		using TScalarEntries = TScalarFlatMap<K, V>;
		using FIntI32Entries = TScalarEntries<std::int32_t, std::int32_t>;
		using FUIntI32Entries = TScalarEntries<std::uint32_t, std::int32_t>;
		using FIntU32Entries = TScalarEntries<std::int32_t, std::uint32_t>;
		using FUIntU32Entries = TScalarEntries<std::uint32_t, std::uint32_t>;
		using FIntF32Entries = TScalarEntries<std::int32_t, float>;
		using FIntF64Entries = TScalarEntries<std::int32_t, double>;
		using FUIntF32Entries = TScalarEntries<std::uint32_t, float>;
		using FUIntF64Entries = TScalarEntries<std::uint32_t, double>;
		using FIntBoolEntries = TScalarEntries<std::int32_t, bool>;
		using FUIntBoolEntries = TScalarEntries<std::uint32_t, bool>;
		using FPrimitiveScalar = std::variant<
		    std::int8_t,
		    std::uint8_t,
		    std::int16_t,
		    std::uint16_t,
		    std::int32_t,
		    std::uint32_t,
		    std::int64_t,
		    std::uint64_t,
		    float,
		    double,
		    bool>;
		using FIntExtendedEntries = TScalarEntries<std::int32_t, FPrimitiveScalar>;
		using FUIntExtendedEntries = TScalarEntries<std::uint32_t, FPrimitiveScalar>;
		using FInt64ExtendedEntries = TScalarEntries<std::int64_t, FPrimitiveScalar>;
		using FUInt64ExtendedEntries = TScalarEntries<std::uint64_t, FPrimitiveScalar>;
		using FStringI32Entries = TScalarNativeHashMap<std::string, std::int32_t>;
		using FWStringI32Entries = TScalarNativeHashMap<std::u16string, std::int32_t>;
		using FStringExtendedEntries = TScalarNativeHashMap<std::string, FPrimitiveScalar>;
		using FWStringExtendedEntries = TScalarNativeHashMap<std::u16string, FPrimitiveScalar>;
		using FScalarEntries = std::variant<
		    std::monostate,
		    FIntI32Entries,
		    FIntU32Entries,
		    FIntF32Entries,
		    FIntF64Entries,
		    FIntBoolEntries,
		    FUIntI32Entries,
		    FUIntU32Entries,
		    FUIntF32Entries,
		    FUIntF64Entries,
		    FUIntBoolEntries,
		    FIntExtendedEntries,
		    FUIntExtendedEntries,
		    FInt64ExtendedEntries,
		    FUInt64ExtendedEntries,
		    FStringI32Entries,
		    FWStringI32Entries,
		    FStringExtendedEntries,
		    FWStringExtendedEntries>;

		static bool TryPackPrimitiveScalar(
		    const FValue& Value,
		    FPrimitiveScalar& OutValue)
		{
			bool bPacked = false;
			std::visit(
			    [&](const auto& Candidate)
			{
				using TValue = std::decay_t<decltype(Candidate)>;
				if constexpr (std::is_constructible_v<FPrimitiveScalar, TValue>)
				{
					OutValue = Candidate;
					bPacked = true;
				}
			},
			    Value);
			return bPacked;
		}

		static FValue UnpackPrimitiveScalar(const FPrimitiveScalar& Value)
		{
			return std::visit(
			    [](const auto Scalar) -> FValue
			{
				return Scalar;
			},
			    Value);
		}

		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string KeyTypeName;
		std::string ValueTypeName;

		// Retain an explicit reserve request until a non-escaping map is upgraded
		// to typed scalar storage. Without this hint, reserve() only sized the
		// initially boxed table and the later JIT table rehashed from scratch.
		std::size_t ReservedCapacityHint = 0;

		// Used only for JIT-proven, non-escaping local primitive maps. Generic maps keep
		// monostate and use the boxed FValue stores below.
		FScalarEntries ScalarEntries;
		std::unordered_map<std::int32_t, FValue> IntEntries;
		std::unordered_map<std::uint32_t, FValue> UIntEntries;
		std::unordered_map<std::int64_t, FValue> Int64Entries;
		std::unordered_map<std::uint64_t, FValue> UInt64Entries;
		std::unordered_map<std::string, FValue> StrEntries;
		std::unordered_map<std::u16string, FValue> WStrEntries;
		std::unordered_map<FEnumValue, FValue, FEnumValueHash> EnumEntries;
		std::unordered_map<
		    FHostObjectHandle,
		    FValue,
		    FHostObjectKeyHash,
		    FHostObjectKeyEqual>
		    HostEntries;
	};

	/**
	 * Set value (set<T>) with reference semantics via shared handle.
	 * Unordered unique elements (hash).
	 */
	struct FSet
	{
		using FIntScalarElements = TScalarFlatSet<std::int32_t>;
		using FUIntScalarElements = TScalarFlatSet<std::uint32_t>;
		using FInt64ScalarElements = TScalarFlatSet<std::int64_t>;
		using FUInt64ScalarElements = TScalarFlatSet<std::uint64_t>;
		using FStringScalarElements = TScalarNativeHashSet<std::string>;
		using FWStringScalarElements = TScalarNativeHashSet<std::u16string>;
		using FScalarElements = std::variant<
		    std::monostate,
		    FIntScalarElements,
		    FUIntScalarElements,
		    FInt64ScalarElements,
		    FUInt64ScalarElements,
		    FStringScalarElements,
		    FWStringScalarElements>;
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;
		std::size_t ReservedCapacityHint = 0;
		// Enabled only after bytecode escape analysis proves the set remains local.
		FScalarElements ScalarElements;
		std::unordered_set<std::int32_t> IntElements;
		std::unordered_set<std::uint32_t> UIntElements;
		std::unordered_set<std::int64_t> Int64Elements;
		std::unordered_set<std::uint64_t> UInt64Elements;
		std::unordered_set<std::string> StrElements;
		std::unordered_set<std::u16string> WStrElements;
		std::unordered_set<FEnumValue, FEnumValueHash> EnumElements;
		std::unordered_set<
		    FHostObjectHandle,
		    FHostObjectKeyHash,
		    FHostObjectKeyEqual>
		    HostElements;
	};

	/**
	 * Queue value (queue<T>) with reference semantics via shared handle.
	 * FIFO only: push back / pop front / front. Use deque for double-ended access.
	 */
	struct FQueue
	{
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;
		std::deque<FValue> Elements;
	};

	/**
	 * Stack value (stack<T>) with reference semantics via shared handle.
	 */
	struct FStack
	{
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;
		std::vector<FValue> Elements;
	};

	/**
	 * Ordered map (ordered_map<K,V>) with reference semantics via shared handle.
	 * Insertion-ordered dictionary; keys() follows first-insert order.
	 * Updating an existing key keeps its position.
	 */
	struct FOrderedMap
	{
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string KeyTypeName;
		std::string ValueTypeName;
		std::vector<std::int32_t> IntKeyOrder;
		std::unordered_map<std::int32_t, FValue> IntEntries;
		std::vector<std::uint32_t> UIntKeyOrder;
		std::unordered_map<std::uint32_t, FValue> UIntEntries;
		std::vector<std::int64_t> Int64KeyOrder;
		std::unordered_map<std::int64_t, FValue> Int64Entries;
		std::vector<std::uint64_t> UInt64KeyOrder;
		std::unordered_map<std::uint64_t, FValue> UInt64Entries;
		std::vector<std::string> StrKeyOrder;
		std::unordered_map<std::string, FValue> StrEntries;
		std::vector<std::u16string> WStrKeyOrder;
		std::unordered_map<std::u16string, FValue> WStrEntries;
		std::vector<FEnumValue> EnumKeyOrder;
		std::unordered_map<FEnumValue, FValue, FEnumValueHash> EnumEntries;
		std::vector<FHostObjectHandle> HostKeyOrder;
		std::unordered_map<
		    FHostObjectHandle,
		    FValue,
		    FHostObjectKeyHash,
		    FHostObjectKeyEqual>
		    HostEntries;
	};

	/**
	 * Double-ended queue (deque<T>) with reference semantics via shared handle.
	 * Supports integer subscript (read/write) in addition to end APIs.
	 */
	struct FDeque
	{
		FContainerMutationOrigin MutationOrigin;
		std::string TypeName;
		std::string ElementTypeName;
		std::deque<FValue> Elements;
	};

	/** Immutable identity carried only by reflection/source descriptor objects. */
	struct FScriptObjectDescriptorData
	{
		std::string ReflectionOwnerType;
		std::string ReflectionDeclaringType;
		std::string ReflectionMemberName;
		std::string ReflectionSignature;
		bool bReflectionMethod = false;
		bool bReflectionStatic = false;
		std::shared_ptr<const FSourceMetadataSnapshot> SourceMetadataSnapshot;
		std::string SourceMetadataNodeId;
		std::vector<std::uint32_t> SourceMetadataPath;
		std::uint8_t SourceMetadataCategory = 0;
	};

	/** Cold state needed by host-derived objects and incomplete construction. */
	struct FScriptObjectSupportData
	{
		FHostObjectHandle HostBase;
		std::vector<std::uint16_t> CompletedCopyBases;
	};

	/**
	 * Runtime struct/class instance: kind, type name, and field value slots.
	 */
	struct FScriptObject : std::enable_shared_from_this<FScriptObject>
	{
		friend class VeroScriptVm;

		// Immutable names are shared by equal runtime types. The returned reference
		// remains valid until this object's type identity is replaced.
		const std::string& GetTypeName() const noexcept
		{
			return TypeNameIdentity ? *TypeNameIdentity : EmptyTypeName;
		}
		void SetTypeName(const std::string& Name);
		// Empty ordinary objects do not allocate descriptor storage on query.
		const FScriptObjectDescriptorData& GetDescriptorData() const noexcept;
		// Borrowed until the next host-binding/construction-state mutation.
		const FHostObjectHandle& GetHostBase() const noexcept
		{
			return SupportData ? SupportData->HostBase : EmptyHostBase;
		}
		void SetHostBase(FHostObjectHandle Value);
		const std::vector<std::uint16_t>& GetCompletedCopyBases() const noexcept;
		std::vector<std::uint16_t>& EditCompletedCopyBases();
		void ClearCompletedCopyBases();

		FContainerMutationOrigin MutationOrigin;
		EScriptObjectKind Kind = EScriptObjectKind::Class;
		// Keep the compact state together instead of padding three separate words.
		std::uint16_t RuntimeTypeIndex = 0xFFFF;
		// Reentrancy / once-only guard while destructor runs.
		bool bDestroying = false;
		bool bDestroyed = false;
		// Failed construction releases completed subobjects without the enclosing dtor.
		bool bConstructionIncomplete = false;
		/**
		 * VM-owned native type tag. It is deliberately module- and
		 * generation-qualified: bytecode type indices may change between modules or
		 * after hot reload, while TypeName remains the stable semantic identity.
		 * A null module or zero generation means unresolved.
		 *
		 * Script objects are VM-isolated mutable values; hosts must not execute the
		 * same object concurrently from multiple sessions.  A generation mismatch
		 * always falls back to the authoritative TypeName lookup.
		 */
		// Initialized before the object handle is published. These fields are an
		// immutable fast-path hint afterwards; cross-VM and hot-reload lookups use
		// VM-local generation-qualified caches rather than racing to rewrite them.
		std::uint64_t RuntimeTypeGeneration = 0;
		// Logical Session/VM owner; null marks manually constructed external objects.
		std::shared_ptr<const void> ProgramOwner;
		// Published descriptors are immutable; clones share their identity, not fields.
		std::shared_ptr<const FScriptObjectDescriptorData> DescriptorData;
		const void* RuntimeTypeModule = nullptr;
		std::vector<FValue> Fields;
		// Monotonic change counter used by external serializers / delta replication.
		std::uint64_t MutationVersion = 0;

	private:
		std::shared_ptr<const std::string> TypeNameIdentity;
		inline static const std::string EmptyTypeName;
		FScriptObjectSupportData& EditSupportData();
		// Ordinary objects keep an empty handle. Copies detach this cold record
		// before changing construction state or replacing the host base handle.
		std::shared_ptr<FScriptObjectSupportData> SupportData;
		inline static const FHostObjectHandle EmptyHostBase;
	};

	/**
	 * Cross-VM value channel: channel<T>. Shared by handle; T is a scalar or
	 * another channel handle. Nested channels permit safe capability passing
	 * between isolated VMs without sharing ordinary mutable script objects.
	 */
	struct FChannel
	{
		friend class VeroScriptVm;
		enum class ERole : std::uint8_t
		{
			Ordinary,
			Mutex,
			Semaphore,
			Synchronized
		};
		ERole GetRole() const noexcept
		{
			return Role;
		}
		static FChannelHandle MakeMutex();
		static FChannelHandle MakeSemaphore(std::int64_t Initial, std::int64_t Maximum, std::string& Error);
		static FChannelHandle MakeSynchronized(const std::string& Type, const std::string& Element,
		    FValue Initial, std::string& Error);
		bool AcquireSynchronization(bool Mutex, const std::shared_ptr<const void>& Executor, std::int64_t Timeout,
		    bool& Acquired, std::string& Error, const FMemoryWork& Work);
		bool ReleaseSynchronization(bool Mutex, const std::shared_ptr<const void>& Executor, std::string& Error);
		void AbandonMutex(const std::shared_ptr<const void>& Executor) noexcept;
		std::shared_ptr<const FValue> SnapshotSynchronized(std::string& Error) const;
		bool PublishSynchronized(const std::shared_ptr<const FValue>& Expected,
		    std::shared_ptr<const FValue> Desired, bool Compare, bool& Published, std::string& Error);
		bool Send(FValue Value, std::string& OutError);
		bool TrySend(FValue Value);
		bool SendFor(
		    FValue Value,
		    std::int64_t TimeoutMs,
		    bool& OutTimedOut,
		    std::string& OutError);
		bool Recv(FValue& OutValue, std::string& OutError);
		bool TryRecv(FValue& OutValue);
		bool RecvFor(
		    FValue& OutValue,
		    std::int64_t TimeoutMs,
		    bool& OutTimedOut,
		    std::string& OutError);
		bool ConfigureCapacity(std::size_t InCapacity, std::string& OutError);
		// Bind immutable runtime type metadata before publication, or validate it.
		bool TryBindTypeNames(
		    const std::string& InTypeName,
		    const std::string& InElementTypeName);
		// Read a consistent metadata snapshot while the channel may be shared.
		void GetTypeNames(
		    std::string& OutTypeName,
		    std::string& OutElementTypeName) const;
		/**
		 * Return the immutable published type name without allocating. Empty means
		 * metadata has not been bound yet. The view remains valid for this channel's
		 * lifetime because successful binding is write-once.
		 */
		std::string_view GetTypeNameView() const;
		// Stamp the owning VM field that receives same-thread mutation barriers.
		void SetMutationOrigin(FContainerMutationOrigin InOrigin);
		// Reject cross-thread access to the owning VM's object graph.
		bool TryGetMutationOriginForCurrentThread(
		    FContainerMutationOrigin& OutOrigin) const;
		std::size_t Size() const;
		std::size_t Capacity() const;
		bool IsClosed() const;
		void Close();

	private:
		ERole Role = ERole::Ordinary;
		std::shared_ptr<Concurrent::FTypedTaskState> TypedTask;
		bool ConsumeTaskResult(FValue& Output, std::uint64_t MaxTextBytes, bool& LimitExceeded);
		std::uint64_t PermitCount = 0;
		std::uint64_t MaximumPermits = 0;
		bool bMutexLocked = false;
		std::weak_ptr<const void> MutexOwner;
		std::shared_ptr<const FValue> SynchronizedValue;
		std::string TypeName;
		std::string ElementTypeName;
		FContainerMutationOrigin MutationOrigin;
		mutable std::mutex Mu;
		// Distinct predicates need distinct wait queues: notify_one on a shared
		// queue can wake a blocked sender while every receiver remains asleep.
		std::condition_variable CanSend;
		std::condition_variable CanReceive;
		std::deque<FValue> Queue;
		// Zero keeps the historical unbounded behavior.
		std::size_t MaxCapacity = 0;
		bool bCapacityConfigured = false;
		bool bUsed = false;
		bool bClosed = false;
		std::atomic<bool> bTypeMetadataBound{false};
		std::thread::id MutationOriginThread;
		std::atomic<bool> bHasMutationOrigin{false};
	};

	/**
	 * Opaque host-language object (In binding). Payload lifetime is owned by the handle
	 * deleter when Destroy is set; otherwise the host owns Payload.
	 */
	// FHostObject stores this callback alias and needs its definition.
	using FHostValueCallback = bool (*)(
	    const FNativeCallContext& Context,
	    FValue& OutReturn,
	    std::string& OutError);

	struct FHostObject
	{
		// Core-created payload owners and all protocol/deleter code outlive this value.
		std::shared_ptr<void> ModuleOwner;
		std::shared_ptr<const void> ProviderIdentity;
		std::shared_ptr<void> PayloadOwner;
		std::shared_ptr<void> IsTypeOwner;
		std::uint32_t TypeId = 0;
		std::string TypeName;
		// Non-zero for a concrete instance of a registered generic host template.
		std::uint32_t GenericTemplateId = 0;
		std::vector<std::string> GenericTypeArguments;
		void* Payload = nullptr;
		/**
		 * Native callback for value-copy (`@clone`). Stamped on host-struct
		 * returns when a clone native is registered, and on script Struct HostBase.
		 * Null -> share this handle on copy (Class HostBase always shares; Struct HostBase
		 * is rejected at construct without a stamped clone).
		 */
		FHostValueCallback CloneCallback = nullptr;
		void* CloneUserData = nullptr;
		/**
		 * Optional value-key protocol callbacks. Equals receives (left,right) and
		 * returns bool; Hash receives (value) and returns uint64/int64/int.
		 * Both must be deterministic for the lifetime of a key stored in Map/Set.
		 */
		FHostValueCallback EqualsCallback = nullptr;
		void* EqualsUserData = nullptr;
		FHostValueCallback HashCallback = nullptr;
		void* HashUserData = nullptr;
		/**
		 * Optional host-defined runtime subtype test. It is used whenever a host
		 * handle is consumed through a different static host class (for example
		 * UObject <- AActor after Cast<UObject>). Exact TypeName matches never call it.
		 */
		bool (*IsTypeCallback)(
		    void* Payload,
		    void* UserData,
		    std::string_view ExpectedTypeName) = nullptr;
		void* IsTypeUserData = nullptr;
		/**
		 * Back-reference to the script instance that owns this host payload (Class/Struct
		 * HostBase). Weak to avoid cycles with FScriptObject::HostBase. Cleared when the
		 * script object is destroyed; used by GetScriptOwner for host->script reverse lookup.
		 */
		std::weak_ptr<FScriptObject> ScriptOwner;
		// Protocol stamps can escape the binding registry that produced them.
		std::shared_ptr<void> CloneOwner, EqualsOwner, HashOwner;
	};

	/**
	 * First-class function / closure. Source accepts `fn` and `function`; runtime
	 * signatures use canonical `fn(...)->T`. Shared by handle; captures by value.
	 * CapturedThis is in-memory only: method-context MakeClosure binds Frame.ThisObject -
	 * struct receivers are cloned (value snapshot); class receivers share the handle.
	 */
	struct FFunctionValue
	{
		std::uint16_t FunctionIndex = 0;
		// Session/VM generation that owns FunctionIndex. Hot reload remaps it
		// transactionally; handles from unrelated module generations are rejected.
		std::uint64_t ProgramGeneration = 1;
		// In-memory identity of the owning linked Session/VM, never serialized.
		std::shared_ptr<const void> ProgramOwner;
		// Stable linkage identity used to reject stale/reused indices after reload.
		std::string FunctionName;
		std::string TypeName;
		std::vector<FValue> Captures;
		FObjectHandle CapturedThis;
	};
}
