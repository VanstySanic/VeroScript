// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConcurrent.h"
#include "VeroScriptConcurrentInternal.h"
#include "VeroScriptMemory.h"
#include "VeroScriptNative.h"

#include <chrono>
#include <algorithm>
#include <limits>

namespace VeroScript
{
	namespace
	{
		auto Deadline(std::int64_t Milliseconds)
		{
			using Clock = std::chrono::steady_clock;
			const auto Now = Clock::now();
			if (Milliseconds < 0) return Clock::time_point::max();
			const auto Remaining = Clock::time_point::max() - Now;
			if (static_cast<long double>(Milliseconds) >= std::chrono::duration<long double, std::milli>(Remaining).count())
			{
				return Clock::time_point::max();
			}
			return Now + std::chrono::milliseconds(Milliseconds);
		}
	}

	FChannelHandle FChannel::MakeMutex()
	{
		auto Result = std::make_shared<FChannel>();
		if (!Result->TryBindTypeNames("channel<bool>", "bool")) return {};
		Result->Role = ERole::Mutex;
		Result->MaxCapacity = 1;
		return Result;
	}
	FChannelHandle FChannel::MakeSemaphore(std::int64_t Initial, std::int64_t Maximum, std::string& Error)
	{
		if (Maximum <= 0 || Initial < 0 || Initial > Maximum || static_cast<std::uint64_t>(Maximum) > std::numeric_limits<std::size_t>::max())
		{
			Error = "semaphore requires 0 <= initial <= maximum and maximum > 0";
			return {};
		}
		auto Result = std::make_shared<FChannel>();
		if (!Result->TryBindTypeNames("channel<bool>", "bool")) return {};
		Result->Role = ERole::Semaphore;
		Result->PermitCount = static_cast<std::uint64_t>(Initial);
		Result->MaximumPermits = static_cast<std::uint64_t>(Maximum);
		Result->MaxCapacity = static_cast<std::size_t>(Maximum);
		return Result;
	}
	FChannelHandle FChannel::MakeSynchronized(const std::string& Type, const std::string& Element,
	    FValue Initial, std::string& Error)
	{
		if (!Concurrent::IsChannelPayload(Initial))
		{
			Error = "unsupported synchronized payload";
			return {};
		}
		auto Result = std::make_shared<FChannel>();
		if (!Result->TryBindTypeNames(Type, Element))
		{
			Error = "invalid synchronized type";
			return {};
		}
		// Reuse the ordinary channel's exact payload validator before giving it a
		// special role. No other thread can observe this unpublished capability.
		if (!Result->TrySend(std::move(Initial)))
		{
			Error = "synchronized payload type mismatch";
			return {};
		}
		Result->SynchronizedValue = std::make_shared<const FValue>(std::move(Result->Queue.front()));
		Result->Queue.clear();
		Result->Role = ERole::Synchronized;
		Result->MaxCapacity = 1;
		return Result;
	}

	bool FChannel::AcquireSynchronization(bool Mutex, const std::shared_ptr<const void>& Executor,
	    std::int64_t Timeout, bool& Acquired, std::string& Error, const FMemoryWork& Work)
	{
		Acquired = false;
		if (Role != (Mutex ? ERole::Mutex : ERole::Semaphore) || (Mutex && !Executor) || Timeout < -1)
		{
			Error = "invalid synchronization capability or timeout";
			return false;
		}
		const auto Until = Deadline(Timeout);
		for (;;)
		{
			// Governance/user services always run outside the channel state lock.
			if (Work.Check && !Work.Check(Work.UserData, 1))
			{
				Error = "synchronization interrupted";
				return false;
			}
			std::unique_lock<std::mutex> Lock(Mu);
			if (bClosed)
			{
				Error = "synchronization capability is closed or abandoned";
				return false;
			}
			if (Mutex && bMutexLocked && MutexOwner.lock() == Executor)
			{
				if (Timeout < 0)
				{
					Error = "recursive mutex acquisition is not supported";
					return false;
				}
				return true;
			}
			if (Mutex && bMutexLocked && MutexOwner.expired())
			{
				bClosed = true;
				Lock.unlock();
				CanReceive.notify_all();
				Error = "mutex owner has exited";
				return false;
			}
			if (Mutex ? !bMutexLocked : PermitCount != 0)
			{
				if (Mutex)
				{
					MutexOwner = Executor;
					bMutexLocked = true;
				}
				else
				{
					--PermitCount;
				}
				Acquired = true;
				return true;
			}
			const auto Now = std::chrono::steady_clock::now();
			if (Timeout >= 0 && Now >= Until) return true;
			if (Concurrent::Detail::IsWorkerShutdownRequested())
			{
				Error = "synchronization interrupted by owner shutdown";
				return false;
			}
			CanReceive.wait_until(Lock, std::min(Until, Deadline(10)));
		}
	}

	bool FChannel::ReleaseSynchronization(bool Mutex, const std::shared_ptr<const void>& Executor, std::string& Error)
	{
		if (Role != (Mutex ? ERole::Mutex : ERole::Semaphore))
		{
			Error = "invalid synchronization capability";
			return false;
		}
		{
			std::lock_guard<std::mutex> Lock(Mu);
			if (bClosed)
			{
				Error = "synchronization capability is closed or abandoned";
				return false;
			}
			if (Mutex)
			{
				if (!Executor || !bMutexLocked || MutexOwner.lock() != Executor)
				{
					Error = "mutex unlock requires its current owner";
					return false;
				}
				bMutexLocked = false;
				MutexOwner.reset();
			}
			else
			{
				if (PermitCount == MaximumPermits)
				{
					Error = "semaphore release exceeds maximum";
					return false;
				}
				++PermitCount;
			}
		}
		CanReceive.notify_one();
		return true;
	}
	void FChannel::AbandonMutex(const std::shared_ptr<const void>& Executor) noexcept
	{
		if (Role != ERole::Mutex) return;
		{
			std::lock_guard<std::mutex> Lock(Mu);
			if (!bMutexLocked || MutexOwner.lock() != Executor) return;
			bClosed = true;
		}
		CanReceive.notify_all();
		CanSend.notify_all();
	}

	std::shared_ptr<const FValue> FChannel::SnapshotSynchronized(std::string& Error) const
	{
		std::lock_guard<std::mutex> Lock(Mu);
		if (Role != ERole::Synchronized || bClosed || !SynchronizedValue)
		{
			Error = "invalid or closed synchronized capability";
			return {};
		}
		return SynchronizedValue;
	}
	bool FChannel::PublishSynchronized(const std::shared_ptr<const FValue>& Expected,
	    std::shared_ptr<const FValue> Desired, bool Compare, bool& Published, std::string& Error)
	{
		Published = false;
		// Keep nested channel metadata/type checks outside this channel's lock.
		if (!Desired || !Concurrent::IsChannelPayload(*Desired))
		{
			Error = "invalid synchronized payload";
			return false;
		}
		if (!Concurrent::IsChannelPayloadFor(*this, *Desired))
		{
			Error = "synchronized payload type mismatch";
			return false;
		}
		{
			std::lock_guard<std::mutex> Lock(Mu);
			if (Role != ERole::Synchronized || bClosed)
			{
				Error = "invalid or closed synchronized capability";
				return false;
			}
			if (Compare && SynchronizedValue != Expected) return true;
			SynchronizedValue.swap(Desired);
			Published = true;
		}
		// Desired now owns the previous payload. Its cleanup happens outside Mu.
		return true;
	}

	namespace Concurrent
	{
		FExecutorState::~FExecutorState()
		{
			AbandonMutexes();
		}
		void FExecutorState::PrepareMutex(const FChannelHandle& Mutex)
		{
			for (auto It = Mutexes.begin(); It != Mutexes.end();)
			{
				if (auto Current = It->lock())
				{
					if (Current == Mutex) return;
					++It;
				}
				else
				{
					It = Mutexes.erase(It);
				}
			}
			Mutexes.push_back(Mutex);
		}
		void FExecutorState::AbandonMutexes() noexcept
		{
			for (auto& Weak : Mutexes)
			{
				if (auto Mutex = Weak.lock())
				{
					Mutex->AbandonMutex(Token);
				}
			}
			Mutexes.clear();
		}
	}
}
