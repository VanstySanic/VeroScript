// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace VeroScript::AsmJit::JitScratch
{
	inline constexpr std::size_t MaxPooledBuffers = 8;
	inline constexpr std::size_t MaxRetainedSlots = 4096;

	struct FStorage
	{
		FStorage()
		{
			I32.reserve(MaxPooledBuffers);
			U32.reserve(MaxPooledBuffers);
			F64.reserve(MaxPooledBuffers);
			Mixed.reserve(MaxPooledBuffers);
		}

		std::size_t RetainedBytes() const noexcept
		{
			const auto Size = [](const auto& Pool)
			{
				std::size_t Bytes = Pool.capacity() * sizeof(typename std::decay_t<decltype(Pool)>::value_type);
				for (const auto &Buffer : Pool)
				{
					Bytes += Buffer.capacity() * sizeof(typename std::decay_t<decltype(Buffer)>::value_type);
				}
				return Bytes;
			};
			return Size(I32) + Size(U32) + Size(F64) + Size(Mixed) + LinearStateBytes;
		}

		std::vector<std::vector<std::int32_t>> I32;
		std::vector<std::vector<std::uint32_t>> U32;
		std::vector<std::vector<double>> F64;
		std::vector<std::vector<std::uint64_t>> Mixed;
		std::array<std::shared_ptr<void>, 32> LinearStates{};
		std::size_t LinearStateCount = 0;
		std::size_t LinearStateBytes = 0;

	};

	// The thread-local pointer only routes synchronous leases. Retained storage is
	// owned by one backend instance, and reentrant instances restore the outer route.
	inline thread_local FStorage* ActiveStorage = nullptr;

	class FStorageScope final
	{
	public:
		explicit FStorageScope(FStorage& Storage) noexcept : Previous(ActiveStorage)
		{
			ActiveStorage = &Storage;
		}
		~FStorageScope()
		{
			ActiveStorage = Previous;
		}
		FStorageScope(const FStorageScope&) = delete;
		FStorageScope& operator=(const FStorageScope&) = delete;

	private:
		FStorage* Previous;
	};

	template <typename T>
	std::vector<std::vector<T>>* Buffers() noexcept
	{
		if (!ActiveStorage) return nullptr;
		if constexpr (std::is_same_v<T, std::int32_t>)
		{
			return &ActiveStorage->I32;
		}
		else if constexpr (std::is_same_v<T, std::uint32_t>)
		{
			return &ActiveStorage->U32;
		}
		else if constexpr (std::is_same_v<T, double>)
		{
			return &ActiveStorage->F64;
		}
		else if constexpr (std::is_same_v<T, std::uint64_t>)
		{
			return &ActiveStorage->Mixed;
		}
		else
		{
			return nullptr;
		}
	}

	template <typename T>
	class TRawScratchLease final
	{
		static_assert(std::is_trivially_copyable_v<T>
			&& std::is_trivially_default_constructible_v<T>,
			"native JIT scratch is only valid for raw scalar slots");

	public:
		explicit TRawScratchLease(const std::size_t RequiredSlots) : Pool(Buffers<T>())
		{
			if (RequiredSlots == 0)
			{
				return;
			}
			if (Pool && !Pool->empty())
			{
				// Prefer the smallest retained buffer that already fits. With at most
				// eight entries this avoids both a heap allocation and excess retention.
				std::size_t Best = Pool->size();
				for (std::size_t Index = 0; Index < Pool->size(); ++Index)
				{
					if ((*Pool)[Index].size() >= RequiredSlots
						&& (Best == Pool->size()
							|| (*Pool)[Index].size() < (*Pool)[Best].size()))
					{
						Best = Index;
					}
				}
				if (Best == Pool->size())
				{
					Best = Pool->size() - 1;
				}
				Storage = std::move((*Pool)[Best]);
				(*Pool)[Best] = std::move(Pool->back());
				Pool->pop_back();
			}
			if (Storage.size() < RequiredSlots)
			{
				Storage.resize(RequiredSlots);
			}
		}

		~TRawScratchLease()
		{
			if (Storage.empty() || Storage.capacity() > MaxRetainedSlots)
			{
				return;
			}
			if (Pool && Pool->size() < MaxPooledBuffers)
			{
				Pool->push_back(std::move(Storage));
			}
		}

		TRawScratchLease(const TRawScratchLease&) = delete;
		TRawScratchLease& operator=(const TRawScratchLease&) = delete;

		T* data() noexcept
		{
			return Storage.data();
		}

	private:
		std::vector<std::vector<T>>* Pool;
		std::vector<T> Storage;
	};
}
