// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkInternal.h"
#include <algorithm>

namespace VeroScript::Network::Detail
{
	FByteLease::FByteLease(std::shared_ptr<FContext> Value) : Context(std::move(Value))
	{
	}
	FByteLease::~FByteLease()
	{
		Context->RetainedBytes.fetch_sub(Bytes, std::memory_order_relaxed);
	}
	bool FByteLease::TryResize(std::uint64_t Value) noexcept
	{
		if (Value <= Bytes)
		{
			Context->RetainedBytes.fetch_sub(Bytes - Value, std::memory_order_relaxed);
			Bytes = Value;
			return true;
		}
		const auto Growth = Value - Bytes;
		auto Current = Context->RetainedBytes.load(std::memory_order_relaxed);
		for (;;)
		{
			if (Growth > Context->Options.MaxRetainedBytes - std::min(Current, Context->Options.MaxRetainedBytes))
			{
				return false;
			}
			if (Context->RetainedBytes.compare_exchange_weak(Current, Current + Growth, std::memory_order_relaxed))
			{
				break;
			}
		}
		Bytes = Value;
		const auto PeakValue = Current + Growth;
		auto Peak = Context->PeakRetainedBytes.load(std::memory_order_relaxed);
		while (Peak < PeakValue &&
			   !Context->PeakRetainedBytes.compare_exchange_weak(Peak, PeakValue, std::memory_order_relaxed))
		{
		}
		return true;
	}
	std::shared_ptr<FByteLease> ReserveBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, FError& Error)
	{
		Error = {};
		if (Context->Closed)
		{
			Error.Code = EError::Closed;
			return {};
		}
		return ReserveSnapshotBytes(Context, Bytes, Error);
	}
	std::shared_ptr<FByteLease> ReserveSnapshotBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, FError& Error)
	{
		Error = {};
		try
		{
			auto Lease = Context->CreateByteLease(Context);
			if (Lease->TryResize(Bytes))
			{
				return Lease;
			}
			Error.Code = EError::Limit;
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = EError::Allocation;
		}
		return {};
	}
	std::shared_ptr<FByteLease> ReserveBytes(
		const std::shared_ptr<FContext>& Context, std::uint64_t Bytes, const std::shared_ptr<FOperation>& Operation)
	{
		FError Error;
		auto Lease = ReserveBytes(Context, Bytes, Error);
		if (!Lease)
		{
			Context->Finish(Operation, EOperationState::Failed, std::move(Error));
		}
		return Lease;
	}
} // namespace VeroScript::Network::Detail
namespace VeroScript::Network
{
	FMemoryStatistics FContext::MemoryUsage() const noexcept
	{
		const auto Current = Impl->RetainedBytes.load(std::memory_order_relaxed);
		return {Current,
			std::max(Current, Impl->PeakRetainedBytes.load(std::memory_order_relaxed)),
			Impl->Options.MaxRetainedBytes};
	}
} // namespace VeroScript::Network
