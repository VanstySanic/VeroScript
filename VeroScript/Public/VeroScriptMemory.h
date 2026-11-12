// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace VeroScript
{
	struct FNativeFunction;
	struct FBytecodeModule;
	struct FMemoryContextState;
	struct FMemoryAccess;

	class IHostBindingCatalog;

	enum class EMemoryStatus : std::uint32_t
	{
		Ok,
		InvalidArgument,
		OutOfMemory,
		WrongThread,
		Unsupported,
		TypeMismatch,
		PermissionDenied,
		Expired,
		ReadOnly,
		Busy,
		LimitExceeded,
		Interrupted
	};

	enum class EMemoryByteOrder : std::uint32_t
	{
		Little = 0,
		Big = 1
	};

	struct FMemoryLimits
	{
		std::uint64_t MaxBufferBytes = 0;
		std::uint64_t MaxRetainedBytes = 0;
	};

	struct FMemoryStats
	{
		std::uint64_t OwnedBytes = 0;
		std::uint64_t BorrowedBytes = 0;
		std::uint64_t PeakOwnedBytes = 0;
	};

	struct FMemoryInspection
	{
		std::optional<std::uint64_t> StorageAddress;
		std::optional<std::uint64_t> ObjectId;
		std::optional<std::uint64_t> ObjectAddress;
		std::optional<std::uint64_t> DataAddress;
		std::optional<std::int64_t> ByteLength;
	};

	struct FMemoryResult
	{
		explicit operator bool() const noexcept
		{
			return Status == EMemoryStatus::Ok;
		}
		EMemoryStatus Status = EMemoryStatus::Ok;
		const char* MessageId = nullptr;
	};

	struct FMemoryLeaseToken
	{
		std::uint64_t Runtime = 0;
		std::uint64_t Serial = 0;
	};

	struct FMemoryLease
	{
		const std::uint8_t* Data = nullptr;
		std::uint8_t* WritableData = nullptr;
		std::uint64_t Size = 0;
		FMemoryLeaseToken Token;
	};

	// A callback is invoked before each bounded chunk, without internal locks.
	struct FMemoryWork
	{
		bool (*Check)(void*, std::uint64_t WorkUnits) = nullptr;
		void* UserData = nullptr;
	};

	using FMemoryReleaseCallback = void (*)(void* UserData);

	class FMemoryContext final
	{
		friend struct FMemoryAccess;

	public:
		explicit FMemoryContext(FMemoryLimits Limits = {});
		~FMemoryContext();
		FMemoryContext(const FMemoryContext&) = delete;
		FMemoryContext& operator=(const FMemoryContext&) = delete;
		std::shared_ptr<FMemoryContext> CreateWorker() const;
		FMemoryResult SetLimits(FMemoryLimits Limits);
		FMemoryLimits GetLimits() const;
		FMemoryStats GetStats() const;
		void Close() noexcept;
		void BeginExecution();
		void EndExecution() noexcept;

	private:
		explicit FMemoryContext(std::shared_ptr<FMemoryContextState> InState);
		std::shared_ptr<FMemoryContextState> State;
	};

	namespace Memory
	{
		bool NativeAddressesAvailable() noexcept;
		bool IsTypeName(std::string_view Name) noexcept;
		bool IsRegion(const FValue& Value) noexcept;
		FMemoryResult ObjectId(
			const FValue& Value, std::uint64_t& OutId, const IHostBindingCatalog* HostBindings = nullptr);
		FMemoryResult ObjectAddress(const FValue& Value, std::uint64_t& OutAddress,
			const IHostBindingCatalog* HostBindings = nullptr);
		FMemoryResult CreateBuffer(
			FMemoryContext& Context, std::int64_t Size, FValue& OutValue, FMemoryWork Work = {});
		FMemoryResult CreateEmptyView(bool ReadOnly, FValue& OutValue);
		FMemoryResult Size(FMemoryContext& Context, const FValue& Region, std::int64_t& OutSize);
		FMemoryResult Capacity(FMemoryContext& Context, const FValue& Buffer, std::int64_t& OutCapacity);
		FMemoryResult DataAddress(FMemoryContext& Context, const FValue& Region, std::uint64_t& OutAddress);
		FMemoryResult Resize(FMemoryContext& Context, const FValue& Buffer, std::int64_t Size,
			bool ReserveOnly = false, FMemoryWork Work = {});
		FMemoryResult Slice(FMemoryContext& Context, const FValue& Region, std::int64_t Offset,
			std::int64_t Count, bool ReadOnly, FValue& OutValue);
		FMemoryResult Copy(FMemoryContext& Context, const FValue& Destination, std::int64_t DestinationOffset,
			const FValue& Source, std::int64_t SourceOffset, std::int64_t Count, FMemoryWork Work = {});
		FMemoryResult Fill(FMemoryContext& Context, const FValue& Destination, std::int64_t Offset,
			std::int64_t Count, std::uint8_t Value, FMemoryWork Work = {});
		FMemoryResult Equal(FMemoryContext& Context, const FValue& Left, std::int64_t LeftOffset,
			const FValue& Right, std::int64_t RightOffset, std::int64_t Count, bool& OutEqual,
			FMemoryWork Work = {});
		FMemoryResult Clone(
			FMemoryContext& Context, const FValue& Source, FValue& OutValue, FMemoryWork Work = {});
		FMemoryResult FromArray(FMemoryContext& Context, const FDynamicArrayHandle& Source, FValue& OutValue,
			FMemoryWork Work = {});
		FMemoryResult ToArray(FMemoryContext& Context, const FValue& Source, FDynamicArrayHandle& OutValue,
			FMemoryWork Work = {});
		FMemoryResult Read(FMemoryContext& Context, const FValue& Source, std::int64_t Offset,
			std::string_view Type, EMemoryByteOrder Order, FValue& OutValue, FMemoryWork Work = {});
		FMemoryResult Write(FMemoryContext& Context, const FValue& Destination, std::int64_t Offset,
			std::string_view Type, EMemoryByteOrder Order, const FValue& Value, FMemoryWork Work = {});
		// Registration adopts Owner/release responsibility only after success.
		FMemoryResult WrapExternalRegion(FMemoryContext& Context, void* Data, std::uint64_t Size,
			bool ReadOnly, const std::shared_ptr<void>& Owner, FMemoryReleaseCallback Release, void* UserData,
			FValue& OutValue);
		FMemoryResult InvalidateExternalRegion(FMemoryContext& Context, const FValue& Region);
		FMemoryResult AcquireLease(
			FMemoryContext& Context, const FValue& Region, bool Write, FMemoryLease& OutLease);
		// Thread safe; independent of VM/Session lifetime. Synchronize native work first.
		FMemoryResult ReleaseLease(FMemoryLeaseToken Token);
		FMemoryResult MutationVersion(
			FMemoryContext& Context, const FValue& Region, std::uint64_t& OutVersion);
		const IHostBindingCatalog& GetBindingCatalog();
		// Metadata and unused imports do not count as runtime use.
		bool UsesRuntimeMemory(
			const FBytecodeModule& Module, const std::vector<FNativeFunction>* Natives = nullptr);
		std::shared_ptr<const IHostBindingCatalog> MergeBindingCatalog(
			const IHostBindingCatalog* Catalog, const std::vector<FNativeFunction>* Natives);
		const std::vector<FNativeFunction>* MergeNativeFunctions(
			const std::vector<FNativeFunction>* Natives, std::vector<FNativeFunction>& Storage);
	} // namespace Memory
} // namespace VeroScript
