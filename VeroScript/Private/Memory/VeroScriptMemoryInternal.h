// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptMemory.h"
#include "VeroScriptHostBinding.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace VeroScript
{
	struct FMemoryBudget
	{
		std::mutex Mutex;
		FMemoryLimits Limits;
		FMemoryStats Stats;
		std::uint64_t Reserved = 0;
		std::uint64_t Executions = 0;
		std::multiset<std::uint64_t> Sizes;
	};

	struct FMemoryDomain
	{
		std::thread::id Thread = std::this_thread::get_id();
		std::atomic<bool> Active{true};
	};

	struct FMemoryContextState
	{
		std::shared_ptr<FMemoryBudget> Budget;
		std::shared_ptr<FMemoryDomain> Domain;
	};

	struct FMemoryAccess
	{
		static const auto& State(const FMemoryContext& Context)
		{
			return Context.State;
		}
	};

	namespace Memory::Detail
	{
		enum class EKind : std::uint8_t
		{
			Buffer,
			View,
			ReadOnlyView
		};

		inline constexpr std::size_t ChunkSize = 65536;
		inline constexpr std::string_view Names[] = {
			"std.memory.buffer", "std.memory.view", "std.memory.readonly_view"};
		inline constexpr std::uint32_t TypeIdBase = 0xfffffff0u;
		struct FCharge
		{
			~FCharge();
			FMemoryResult Reserve(
				std::shared_ptr<FMemoryBudget> InBudget, std::uint64_t Bytes, bool bBorrowed);
			void Commit();

			std::shared_ptr<FMemoryBudget> Budget;
			std::uint64_t Size = 0;
			bool Borrowed = false;
			bool Committed = false;
			std::multiset<std::uint64_t>::iterator Entry;
		};

		struct FBlock
		{
			FCharge Charge;
			std::unique_ptr<std::uint8_t[]> Data;
		};

		struct FOwner
		{
			~FOwner();
			std::uint8_t* Data() const
			{
				return External ? ExternalData : Block ? Block->Data.get() : nullptr;
			}
			std::mutex Mutex;
			std::shared_ptr<FMemoryContextState> State;
			std::shared_ptr<FBlock> Block;
			FCharge ExternalCharge;
			std::uint8_t* ExternalData = nullptr;
			std::int64_t Size = 0;
			std::uint64_t Generation = 1;
			std::uint64_t Version = 0;
			std::uint64_t Readers = 0;
			bool Writer = false;
			bool Valid = true;
			bool External = false;
			bool ExternalReadOnly = false;
			bool Adopted = false;
			std::shared_ptr<void> ExternalOwner;
			FMemoryReleaseCallback Release = nullptr;
			void* ReleaseUserData = nullptr;
		};

		struct FDescriptor
		{
			std::shared_ptr<FOwner> Owner;
			std::int64_t Offset = 0;
			std::int64_t Length = 0;
			std::uint64_t Generation = 0;
			EKind Kind = EKind::View;
		};

		struct FRegion
		{
			std::shared_ptr<FDescriptor> Descriptor;
			std::int64_t Size = 0;
			std::int64_t Offset = 0;
			std::uint8_t* Data = nullptr;
		};

		struct FGuard
		{
			FGuard() = default;
			FGuard(const FGuard&) = delete;
			FGuard& operator=(const FGuard&) = delete;
			~FGuard();
			FMemoryResult Acquire(const FRegion& Region, bool bWrite);

			std::shared_ptr<FOwner> Owner;
			bool Write = false;
		};

		bool ContainsMemoryType(std::string_view Type) noexcept;

		// The compiler validates host class identity before encoding this operation.
		FMemoryResult ObjectIdentity(const FValue& Value, bool Address, bool HostClass, std::uint64_t& Out);

		inline FMemoryResult Fail(EMemoryStatus Status, const char* Id)
		{
			return {Status, Id};
		}

		FMemoryResult CheckContext(const FMemoryContext& Context);

		FMemoryResult Count(std::int64_t Value);

		FMemoryResult CheckWork(FMemoryWork Work, std::uint64_t Bytes);

		std::shared_ptr<FDescriptor> FindDescriptor(const FValue& Value);
		FMemoryResult MakeValue(std::shared_ptr<FDescriptor> Descriptor, FValue& OutValue);
		FMemoryResult Resolve(
			FMemoryContext& Context, const FValue& Value, FRegion& OutRegion, bool Write = false);
		FMemoryResult Bounds(const FRegion& Region, std::int64_t Offset, std::int64_t Count);
		// Byte member access keeps the same region, lease and work checks as Read.
		FMemoryResult ReadByte(FMemoryContext& Context, const FValue& Value,
			std::int64_t Offset, FValue& Output, FMemoryWork Work);
		FMemoryResult Allocate(
			FMemoryContext& Context, std::int64_t Capacity, std::shared_ptr<FBlock>& OutBlock);
		FMemoryResult CreateUninitializedBuffer(FMemoryContext& Context, std::int64_t Size, FValue& OutValue);
		bool CloneDescriptor(const FNativeCallContext& Context, FValue& OutValue, std::string& OutError);
		bool EqualDescriptor(const FNativeCallContext& Context, FValue& OutValue, std::string& OutError);
		bool HashDescriptor(const FNativeCallContext& Context, FValue& OutValue, std::string& OutError);
	} // namespace Memory::Detail
} // namespace VeroScript
