// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConcurrent.h"
#include "VeroScriptConcurrentInternal.h"

#include "VeroScriptLocale.h"
#include "VeroScriptNative.h"

#include <chrono>
#include <condition_variable>
#include <limits>
#include <thread>
#include <utility>

namespace VeroScript
{
	namespace
	{
		thread_local const std::atomic_bool* WorkerShutdown = nullptr;

		bool IsWorkerShuttingDown()
		{
			return WorkerShutdown && WorkerShutdown->load(std::memory_order_acquire);
		}

		template <typename TPredicate>
		bool WaitInterruptible(std::condition_variable& Condition, std::unique_lock<std::mutex>& Lock,
		    std::chrono::steady_clock::time_point Deadline, TPredicate Predicate, const bool bInterruptible = true)
		{
			if (!WorkerShutdown || !bInterruptible)
			{
				if (Deadline == std::chrono::steady_clock::time_point::max())
				{
					Condition.wait(Lock, Predicate);
					return true;
				}
				return Condition.wait_until(Lock, Deadline, Predicate);
			}
			while (!Predicate())
			{
				if (IsWorkerShuttingDown()) return false;
				const auto Now = std::chrono::steady_clock::now();
				if (Now >= Deadline) return false;
				// Poll only blocked owned workers. The atomic predicate is checked
				// again on each bounded wait, including shutdown before registration.
				if (Condition.wait_until(Lock, std::min(Deadline, Now + std::chrono::milliseconds(10)), Predicate)) return true;
			}
			return true;
		}

		std::chrono::steady_clock::time_point MakeSaturatingDeadline(
		    const std::int64_t TimeoutMs)
		{
			const auto Now = std::chrono::steady_clock::now();
			if (TimeoutMs <= 0)
			{
				return Now;
			}

			const auto MaximumRemaining =
			    std::chrono::steady_clock::time_point::max() - Now;
			const long double MaximumMilliseconds =
			    std::chrono::duration<long double, std::milli>(MaximumRemaining).count();
			if (MaximumMilliseconds <= 0.0L || static_cast<long double>(TimeoutMs) >= MaximumMilliseconds)
			{
				return std::chrono::steady_clock::time_point::max();
			}

			return Now + std::chrono::milliseconds(TimeoutMs);
		}

		bool ChannelPayloadMatchesElementType(
		    const FValue& Value,
		    const std::string_view ElementTypeName,
		    const FChannel* const ReceivingChannel)
		{
			const std::string CanonicalElementType =
			    CanonicalizeNativeElementTypeName(ElementTypeName);
			if (CanonicalElementType.empty())
			{
				return false;
			}
			std::string_view ActualTypeName;
			if (std::holds_alternative<std::int8_t>(Value))
			{
				ActualTypeName = "char";
			}
			else if (std::holds_alternative<std::uint8_t>(Value))
			{
				ActualTypeName = "unsigned char";
			}
			else if (std::holds_alternative<std::int16_t>(Value))
			{
				ActualTypeName = "short";
			}
			else if (std::holds_alternative<std::uint16_t>(Value))
			{
				ActualTypeName = "unsigned short";
			}
			else if (std::holds_alternative<std::int32_t>(Value))
			{
				ActualTypeName = "int";
			}
			else if (std::holds_alternative<std::uint32_t>(Value))
			{
				ActualTypeName = "unsigned int";
			}
			else if (std::holds_alternative<std::int64_t>(Value))
			{
				return CanonicalElementType == "int64" || CanonicalElementType == "long";
			}
			else if (std::holds_alternative<std::uint64_t>(Value))
			{
				return CanonicalElementType == "uint64" || CanonicalElementType == "unsigned long";
			}
			else if (std::holds_alternative<float>(Value))
			{
				ActualTypeName = "float";
			}
			else if (std::holds_alternative<double>(Value))
			{
				ActualTypeName = "double";
			}
			else if (std::holds_alternative<bool>(Value))
			{
				ActualTypeName = "bool";
			}
			else if (std::holds_alternative<std::string>(Value))
			{
				ActualTypeName = "string";
			}
			else if (std::holds_alternative<std::u16string>(Value))
			{
				ActualTypeName = "wstring";
			}
			else if (const auto* Channel = std::get_if<FChannelHandle>(&Value);
			    Channel && *Channel)
			{
				// No finite channel<T> can contain itself with an exact declared type,
				// and avoiding the metadata lock here also prevents self-deadlock for
				// malformed direct C++ API calls.
				if (Channel->get() == ReceivingChannel)
				{
					return false;
				}
				std::string NestedTypeName;
				std::string NestedElementTypeName;
				(*Channel)->GetTypeNames(NestedTypeName, NestedElementTypeName);
				return !NestedTypeName.empty() && NativeElementTypeNamesEqual(NestedTypeName, ElementTypeName);
			}
			return !ActualTypeName.empty() && NativeElementTypeNamesEqual(ActualTypeName, CanonicalElementType);
		}

		bool ChannelPayloadMatchesBoundType(
		    const FChannel& Channel,
		    const FValue& Value,
		    std::string* const OutTypeName = nullptr)
		{
			std::string TypeName;
			std::string ElementTypeName;
			Channel.GetTypeNames(TypeName, ElementTypeName);
			if (OutTypeName)
			{
				*OutTypeName = std::move(TypeName);
			}
			return !ElementTypeName.empty() && ChannelPayloadMatchesElementType(Value, ElementTypeName, &Channel);
		}

	}

	bool FChannel::Send(FValue Value, std::string& OutError)
	{
		if (Role != ERole::Ordinary)
		{
			OutError = "generic channel operations cannot modify synchronization capabilities";
			return false;
		}
		std::string BoundTypeName;
		if (!Concurrent::IsChannelPayload(Value) || !ChannelPayloadMatchesBoundType(*this, Value, &BoundTypeName))
		{
			const std::vector<std::string_view> Args{BoundTypeName};
			OutError = Locale::FormatEnglish(
			    "diag.channel_send_value_type_mismatch_for", Args);
			return false;
		}
		{
			std::unique_lock<std::mutex> Lock(Mu);
			bUsed = true;
			if (!WaitInterruptible(CanSend, Lock, std::chrono::steady_clock::time_point::max(), [this]()
			{
				return bClosed || MaxCapacity == 0 || Queue.size() < MaxCapacity;
			}))
			{
				OutError = Locale::FormatEnglish("diag.channel_is_closed");
				return false;
			}
			if (bClosed)
			{
				OutError = Locale::FormatEnglish("diag.channel_is_closed");
				return false;
			}
			Queue.push_back(std::move(Value));
		}
		CanReceive.notify_one();
		return true;
	}

	bool FChannel::TrySend(FValue Value)
	{
		if (Role != ERole::Ordinary) return false;
		if (!Concurrent::IsChannelPayload(Value) || !ChannelPayloadMatchesBoundType(*this, Value))
		{
			return false;
		}
		{
			std::lock_guard<std::mutex> Lock(Mu);
			bUsed = true;
			if (bClosed || (MaxCapacity != 0 && Queue.size() >= MaxCapacity))
			{
				return false;
			}
			Queue.push_back(std::move(Value));
		}
		CanReceive.notify_one();
		return true;
	}

	bool FChannel::SendFor(
	    FValue Value,
	    const std::int64_t TimeoutMs,
	    bool& OutTimedOut,
	    std::string& OutError)
	{
		OutTimedOut = false;
		if (Role != ERole::Ordinary)
		{
			OutError = "generic channel operations cannot modify synchronization capabilities";
			return false;
		}
		if (TimeoutMs < 0)
		{
			OutError = "channel timeout must be non-negative";
			return false;
		}
		std::string BoundTypeName;
		if (!Concurrent::IsChannelPayload(Value) || !ChannelPayloadMatchesBoundType(*this, Value, &BoundTypeName))
		{
			const std::vector<std::string_view> Args{BoundTypeName};
			OutError = Locale::FormatEnglish(
			    "diag.channel_send_value_type_mismatch_for", Args);
			return false;
		}
		{
			std::unique_lock<std::mutex> Lock(Mu);
			bUsed = true;
			if (!WaitInterruptible(CanSend, Lock,
			        MakeSaturatingDeadline(TimeoutMs),
			        [this]()
			{
				return bClosed || MaxCapacity == 0 || Queue.size() < MaxCapacity;
			}))
			{
				OutTimedOut = !IsWorkerShuttingDown();
				if (!OutTimedOut)
				{
					OutError = Locale::FormatEnglish("diag.channel_is_closed");
				}
				return false;
			}
			if (bClosed)
			{
				OutError = Locale::FormatEnglish("diag.channel_is_closed");
				return false;
			}
			Queue.push_back(std::move(Value));
		}
		CanReceive.notify_one();
		return true;
	}

	bool FChannel::Recv(FValue& OutValue, std::string& OutError)
	{
		if (Role != ERole::Ordinary)
		{
			OutError = "generic channel operations cannot modify synchronization capabilities";
			return false;
		}
		std::unique_lock<std::mutex> Lock(Mu);
		bUsed = true;
		if (!WaitInterruptible(CanReceive, Lock, std::chrono::steady_clock::time_point::max(), [this]()
		{
			return bClosed || !Queue.empty();
		}))
		{
			OutError = Locale::FormatEnglish("diag.channel_is_closed");
			return false;
		}
		if (Queue.empty())
		{
			OutError = Locale::FormatEnglish("diag.channel_is_closed");
			return false;
		}
		OutValue = std::move(Queue.front());
		Queue.pop_front();
		Lock.unlock();
		CanSend.notify_one();
		return true;
	}

	bool FChannel::TryRecv(FValue& OutValue)
	{
		if (Role != ERole::Ordinary) return false;
		{
			std::lock_guard<std::mutex> Lock(Mu);
			bUsed = true;
			if (Queue.empty())
			{
				return false;
			}
			OutValue = std::move(Queue.front());
			Queue.pop_front();
		}
		CanSend.notify_one();
		return true;
	}

	bool FChannel::RecvFor(
	    FValue& OutValue,
	    const std::int64_t TimeoutMs,
	    bool& OutTimedOut,
	    std::string& OutError)
	{
		OutTimedOut = false;
		if (Role != ERole::Ordinary)
		{
			OutError = "generic channel operations cannot modify synchronization capabilities";
			return false;
		}
		if (TimeoutMs < 0)
		{
			OutError = "channel timeout must be non-negative";
			return false;
		}
		std::unique_lock<std::mutex> Lock(Mu);
		bUsed = true;
		if (!WaitInterruptible(CanReceive, Lock,
		        MakeSaturatingDeadline(TimeoutMs),
		        [this]()
		{
			return bClosed || !Queue.empty();
		}))
		{
			OutTimedOut = !IsWorkerShuttingDown();
			if (!OutTimedOut)
			{
				OutError = Locale::FormatEnglish("diag.channel_is_closed");
			}
			return false;
		}
		if (Queue.empty())
		{
			OutError = Locale::FormatEnglish("diag.channel_is_closed");
			return false;
		}
		OutValue = std::move(Queue.front());
		Queue.pop_front();
		Lock.unlock();
		CanSend.notify_one();
		return true;
	}

	bool FChannel::ConfigureCapacity(
	    const std::size_t InCapacity,
	    std::string& OutError)
	{
		if (Role != ERole::Ordinary)
		{
			OutError = "synchronization capacity is fixed at creation";
			return false;
		}
		if (InCapacity == 0)
		{
			OutError = "channel capacity must be positive";
			return false;
		}
		if (InCapacity > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			OutError = "channel capacity exceeds int64 range";
			return false;
		}
		std::lock_guard<std::mutex> Lock(Mu);
		if (bClosed || bUsed || bCapacityConfigured)
		{
			OutError = "channel capacity can only be configured before first use";
			return false;
		}
		MaxCapacity = InCapacity;
		bCapacityConfigured = true;
		return true;
	}

	bool FChannel::TryBindTypeNames(
	    const std::string& InTypeName,
	    const std::string& InElementTypeName)
	{
		if (InTypeName.empty() || InElementTypeName.empty())
		{
			return false;
		}
		// Validate iteratively before recursive canonicalization so hostile metadata
		// cannot bypass the channel nesting limit by overflowing the native stack.
		if (!NativeChannelElementTypeIsValid(InElementTypeName) || !NativeChannelElementTypeIsValid(InTypeName))
		{
			return false;
		}
		const std::string CanonicalElement =
		    CanonicalizeNativeElementTypeName(InElementTypeName);
		if (!NativeElementTypeNamesEqual(
		        InTypeName, "channel<" + CanonicalElement + ">"))
		{
			return false;
		}
		std::lock_guard<std::mutex> Lock(Mu);
		if ((!TypeName.empty() && TypeName != "channel" && !NativeElementTypeNamesEqual(TypeName, InTypeName)) || (!ElementTypeName.empty() && !NativeElementTypeNamesEqual(ElementTypeName, InElementTypeName)))
		{
			return false;
		}
		// Metadata is write-once after construction. Revalidation must not write
		// the strings because another VM may already be reading this channel.
		if (TypeName.empty() || TypeName == "channel")
		{
			TypeName = InTypeName;
		}
		if (ElementTypeName.empty())
		{
			ElementTypeName = InElementTypeName;
		}
		bTypeMetadataBound.store(true, std::memory_order_release);
		return true;
	}

	void FChannel::GetTypeNames(
	    std::string& OutTypeName,
	    std::string& OutElementTypeName) const
	{
		if (bTypeMetadataBound.load(std::memory_order_acquire))
		{
			// The release store in TryBindTypeNames publishes immutable strings.
			// Runtime code never mutates them after this point.
			OutTypeName = TypeName;
			OutElementTypeName = ElementTypeName;
			return;
		}
		std::lock_guard<std::mutex> Lock(Mu);
		OutTypeName = TypeName;
		OutElementTypeName = ElementTypeName;
	}

	std::string_view FChannel::GetTypeNameView() const
	{
		if (!bTypeMetadataBound.load(std::memory_order_acquire))
		{
			return {};
		}
		return TypeName;
	}

	void FChannel::SetMutationOrigin(FContainerMutationOrigin InOrigin)
	{
		std::lock_guard<std::mutex> Lock(Mu);
		const std::thread::id CurrentThread = std::this_thread::get_id();
		if (MutationOriginThread != std::thread::id{} && MutationOriginThread != CurrentThread)
		{
			// A shared handle must not let a worker steal the field origin from
			// the VM that owns the corresponding script object graph.
			return;
		}
		MutationOrigin = std::move(InOrigin);
		MutationOriginThread = CurrentThread;
		bHasMutationOrigin.store(true, std::memory_order_release);
	}

	bool FChannel::TryGetMutationOriginForCurrentThread(
	    FContainerMutationOrigin& OutOrigin) const
	{
		if (!bHasMutationOrigin.load(std::memory_order_acquire))
		{
			return false;
		}
		std::lock_guard<std::mutex> Lock(Mu);
		if (MutationOriginThread != std::thread::id{} && MutationOriginThread != std::this_thread::get_id())
		{
			return false;
		}
		OutOrigin = MutationOrigin;
		return true;
	}

	std::size_t FChannel::Size() const
	{
		std::lock_guard<std::mutex> Lock(Mu);
		if (Role == ERole::Mutex) return !bMutexLocked && !bClosed ? 1 : 0;
		if (Role == ERole::Semaphore) return static_cast<std::size_t>(PermitCount);
		if (Role == ERole::Synchronized) return SynchronizedValue ? 1 : 0;
		return Queue.size();
	}

	std::size_t FChannel::Capacity() const
	{
		std::lock_guard<std::mutex> Lock(Mu);
		return MaxCapacity;
	}

	bool FChannel::IsClosed() const
	{
		std::lock_guard<std::mutex> Lock(Mu);
		return bClosed;
	}

	void FChannel::Close()
	{
		{
			std::lock_guard<std::mutex> Lock(Mu);
			bClosed = true;
		}
		CanSend.notify_all();
		CanReceive.notify_all();
	}

	bool FChannel::ConsumeTaskResult(FValue& Output, std::uint64_t MaxTextBytes, bool& LimitExceeded)
	{
		LimitExceeded = false;
		std::unique_lock<std::mutex> Lock(Mu);
		if (Role != ERole::Ordinary || Queue.size() != 1) return false;
		std::uint64_t Bytes = 0;
		if (const auto* Text = std::get_if<std::string>(&Queue.front()))
		{
			Bytes = Text->size();
		}
		else if (const auto* WideText = std::get_if<std::u16string>(&Queue.front()))
		{
			if (WideText->size() > UINT64_MAX / 2)
			{
				LimitExceeded = true;
				return false;
			}
			Bytes = static_cast<std::uint64_t>(WideText->size()) * 2;
		}
		if (Bytes > INT64_MAX || (MaxTextBytes && Bytes > MaxTextBytes))
		{
			LimitExceeded = true;
			return false;
		}
		Output = std::move(Queue.front());
		Queue.pop_front();
		Lock.unlock();
		CanSend.notify_one();
		return true;
	}

	namespace Concurrent
	{
		bool Detail::IsWorkerShutdownRequested() noexcept
		{
			return IsWorkerShuttingDown();
		}
		bool IsChannelPayloadFor(const FChannel& Channel, const FValue& Value)
		{
			return IsChannelPayload(Value) && ChannelPayloadMatchesBoundType(Channel, Value);
		}
		bool IsChannelPayload(const FValue& Value)
		{
			return std::holds_alternative<std::int8_t>(Value) || std::holds_alternative<std::uint8_t>(Value) || std::holds_alternative<std::int16_t>(Value) || std::holds_alternative<std::uint16_t>(Value) || std::holds_alternative<std::int32_t>(Value) || std::holds_alternative<std::int64_t>(Value) || std::holds_alternative<std::uint32_t>(Value) || std::holds_alternative<std::uint64_t>(Value) || std::holds_alternative<float>(Value) || std::holds_alternative<double>(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value) || (std::holds_alternative<FChannelHandle>(Value) && static_cast<bool>(std::get<FChannelHandle>(Value)));
		}

		struct FTaskGroup::FTaskState
		{
			void MarkFinished(const bool bSuccess, std::string InError)
			{
				{
					std::lock_guard<std::mutex> Lock(Mu);
					bOk = bSuccess;
					Error = std::move(InError);
					bFinished = true;
				}
				Cv.notify_all();
			}

			void JoinThreadOnce()
			{
				std::thread ToJoin;
				{
					std::unique_lock<std::mutex> Lock(Mu);
					// A moved thread handle means another collector owns join(), not
					// that the worker has exited. Every collector must observe its end.
					Cv.wait(Lock, [this]()
					{
						return !bThreadJoining;
					});
					if (bThreadJoined || bAbandoned)
					{
						return;
					}
					bThreadJoining = true;
					ToJoin = std::move(Worker);
				}
				if (ToJoin.joinable())
				{
					ToJoin.join();
				}
				{
					std::lock_guard<std::mutex> Lock(Mu);
					bThreadJoining = false;
					bThreadJoined = true;
				}
				Cv.notify_all();
			}

			// Detach a still-running worker so JoinAll / destruction cannot hang.
			void Abandon()
			{
				std::thread ToDetach;
				bool bNotify = false;
				{
					std::lock_guard<std::mutex> Lock(Mu);
					if (bAbandoned || bThreadJoined || bFinished)
					{
						return;
					}
					bAbandoned = true;
					bThreadJoined = true;
					ToDetach = std::move(Worker);
					bNotify = true;
				}
				if (bNotify)
				{
					Cv.notify_all();
				}
				if (ToDetach.joinable())
				{
					ToDetach.detach();
				}
			}

			// TimeoutMs < 0 waits forever. Returns false on failure or timeout.
			bool WaitAndCollect(
			    std::string* const OutError,
			    bool* OutTimedOut,
			    const std::int64_t TimeoutMs, const bool bInterruptible = true)
			{
				{
					std::unique_lock<std::mutex> Lock(Mu);
					if (bAbandoned)
					{
						if (OutTimedOut)
						{
							*OutTimedOut = true;
						}
						if (OutError)
						{
							*OutError = Locale::FormatEnglish("diag.join_task_timed_out");
						}
						return false;
					}
					const auto Deadline = TimeoutMs < 0 ? std::chrono::steady_clock::time_point::max() : MakeSaturatingDeadline(TimeoutMs);
					const bool Ready = WaitInterruptible(Cv, Lock, Deadline, [this]()
					{
						return bFinished || bAbandoned;
					}, bInterruptible);
					if (!Ready || (bAbandoned && !bFinished))
					{
						const bool Interrupted = !Ready && bInterruptible && IsWorkerShuttingDown();
						if (OutTimedOut)
						{
							*OutTimedOut = !Interrupted;
						}
						if (OutError)
						{
							*OutError = Interrupted ? "task join interrupted by owner shutdown" : Locale::FormatEnglish("diag.join_task_timed_out");
						}
						return false;
					}
				}
				JoinThreadOnce();
				std::lock_guard<std::mutex> Lock(Mu);
				if (!bOk)
				{
					if (OutError)
					{
						*OutError = Error.empty()
						    ? Locale::FormatEnglish("diag.task_failed")
						    : Error;
					}
					return false;
				}
				return true;
			}

			~FTaskState()
			{
				{
					std::unique_lock<std::mutex> Lock(Mu);
					if (bAbandoned || bThreadJoined || !Worker.joinable())
					{
						// A detached worker holds a shared_ptr until MarkFinished. An
						// unstarted state is also possible when vector growth or thread
						// creation throws; there is no worker that could ever signal Cv.
						return;
					}
					Cv.wait(Lock, [this]()
					{
						return bFinished || bAbandoned;
					});
					if (bAbandoned)
					{
						return;
					}
				}
				JoinThreadOnce();
			}

			std::shared_ptr<std::atomic_bool> Cancellation =
			    std::make_shared<std::atomic_bool>(false);
			std::atomic_bool Shutdown{false};
			std::thread Worker;
			std::thread::id WorkerId{};
			std::mutex Mu;
			std::condition_variable Cv;
			bool bFinished = false;
			bool bThreadJoining = false;
			bool bThreadJoined = false;
			bool bAbandoned = false;
			bool bOk = true;
			std::string Error;
		};

		FTaskGroup::FTicket FTaskGroup::RetainTask(std::int32_t Id) const
		{
			FTicket Result;
			std::lock_guard<std::mutex> Lock(Mu);
			if (Id >= 0 && static_cast<std::size_t>(Id) < Tasks.size())
			{
				Result.State = Tasks[static_cast<std::size_t>(Id)];
			}
			return Result;
		}

		FTaskGroup::~FTaskGroup()
		{
			RequestShutdown();
			// Destruction owns the group exclusively. Collect in place so cleanup
			// cannot fail while allocating JoinAll's concurrent-generation snapshot.
			for (const std::shared_ptr<FTaskState>& State : Tasks)
			{
				if (State)
				{
					(void)State->WaitAndCollect(nullptr, nullptr, -1, false);
				}
			}
		}
		bool FTaskGroup::FTicket::Join(std::string& Error, bool& TimedOut, std::int64_t Timeout) const
		{
			TimedOut = false;
			if (!State)
			{
				Error = "invalid task capability";
				return false;
			}
			{
				std::lock_guard<std::mutex> Lock(State->Mu);
				if (!State->bThreadJoined && State->WorkerId == std::this_thread::get_id())
				{
					Error = "cannot join the running task";
					return false;
				}
			}
			return State->WaitAndCollect(&Error, &TimedOut, Timeout);
		}
		bool FTaskGroup::FTicket::Finished() const
		{
			if (!State) return false;
			std::lock_guard<std::mutex> Lock(State->Mu);
			return State->bFinished;
		}
		void FTaskGroup::FTicket::Cancel() const noexcept
		{
			if (State)
			{
				State->Cancellation->store(true, std::memory_order_release);
			}
		}

		std::int32_t FTaskGroup::Spawn(std::function<bool(std::string& OutError)> Work)
		{
			return Spawn([Work = std::move(Work)](
			                 const std::shared_ptr<std::atomic_bool>&,
			                 std::string& OutError) mutable
			{
				return Work(OutError);
			});
		}

		std::int32_t FTaskGroup::Spawn(
		    std::function<bool(std::shared_ptr<std::atomic_bool>, std::string&)> Work)
		{
			std::shared_ptr<FTaskState> State;
			std::int32_t TaskId = -1;
			std::unique_lock<std::mutex> GroupLock(Mu, std::defer_lock);
			try
			{
				State = std::make_shared<FTaskState>();
				{
					GroupLock.lock();
					if (Tasks.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
					{
						return -1;
					}
					TaskId = static_cast<std::int32_t>(Tasks.size());
					// Reserve the slot before starting the worker, retaining the group
					// lock until its thread handle is stored. Join/Abandon/Reset must
					// not observe a partially initialized state, even if Work finishes
					// before std::thread construction returns.
					Tasks.push_back(State);
				}
			}
			catch (...)
			{
				// Resource exhaustion follows the same non-throwing failure contract
				// as std::thread creation so the VM can report spawn failure normally.
				return -1;
			}
			try
			{
				State->Worker = std::thread([State, Work = std::move(Work)]() mutable
				{
					WorkerShutdown = &State->Shutdown;
					std::string Error;
					bool bOk = true;
					try
					{
						bOk = Work(State->Cancellation, Error);
					}
					catch (const std::exception& Exception)
					{
						bOk = false;
						Error = Exception.what();
					}
					catch (...)
					{
						bOk = false;
						Error = Locale::FormatEnglish("diag.task_failed_with_unknown_exception");
					}
					WorkerShutdown = nullptr;
					State->MarkFinished(bOk, std::move(Error));
				});
				// Publish the immutable id with the handle. It remains available for
				// self-join checks while another collector temporarily owns Worker.
				State->WorkerId = State->Worker.get_id();
			}
			catch (...)
			{
				Tasks[static_cast<std::size_t>(TaskId)].reset();
				// Never publish a task that has no worker; Join would wait forever.
				return -1;
			}

			return TaskId;
		}

		void FTaskGroup::RequestCancel(const std::int32_t TaskId)
		{
			std::lock_guard<std::mutex> Lock(Mu);
			if (TaskId >= 0 && static_cast<std::size_t>(TaskId) < Tasks.size() && Tasks[static_cast<std::size_t>(TaskId)])
			{
				Tasks[static_cast<std::size_t>(TaskId)]->Cancellation->store(
				    true, std::memory_order_release);
			}
		}

		bool FTaskGroup::Join(
		    const std::int32_t TaskId,
		    std::string& OutError,
		    bool* const OutTimedOut,
		    const std::int64_t TimeoutMs)
		{
			if (OutTimedOut)
			{
				*OutTimedOut = false;
			}
			std::shared_ptr<FTaskState> State;
			{
				std::lock_guard<std::mutex> Lock(Mu);
				if (TaskId < 0 || static_cast<std::size_t>(TaskId) >= Tasks.size())
				{
					OutError = Locale::FormatEnglish("diag.invalid_task_id");
					return false;
				}
				State = Tasks[static_cast<std::size_t>(TaskId)];
			}
			if (!State)
			{
				OutError = Locale::FormatEnglish("diag.invalid_task_id");
				return false;
			}

			std::thread::id WorkerId{};
			{
				std::lock_guard<std::mutex> Lock(State->Mu);
				if (!State->bThreadJoined)
				{
					WorkerId = State->WorkerId;
				}
			}
			if (WorkerId != std::thread::id{} && WorkerId == std::this_thread::get_id())
			{
				OutError = Locale::FormatEnglish("diag.cannot_join_running_task");
				return false;
			}

			return State->WaitAndCollect(&OutError, OutTimedOut, TimeoutMs);
		}

		bool FTaskGroup::IsFinished(
		    const std::int32_t TaskId,
		    bool& OutFinished,
		    std::string& OutError) const
		{
			OutFinished = false;
			std::shared_ptr<FTaskState> State;
			{
				std::lock_guard<std::mutex> Lock(Mu);
				if (TaskId < 0 || static_cast<std::size_t>(TaskId) >= Tasks.size())
				{
					OutError = Locale::FormatEnglish("diag.invalid_task_id");
					return false;
				}
				State = Tasks[static_cast<std::size_t>(TaskId)];
			}
			if (!State)
			{
				OutError = Locale::FormatEnglish("diag.invalid_task_id");
				return false;
			}
			std::lock_guard<std::mutex> Lock(State->Mu);
			OutFinished = State->bFinished || State->bAbandoned;
			return true;
		}

		void FTaskGroup::Abandon(const std::int32_t TaskId)
		{
			std::shared_ptr<FTaskState> State;
			{
				std::lock_guard<std::mutex> Lock(Mu);
				if (TaskId < 0 || static_cast<std::size_t>(TaskId) >= Tasks.size())
				{
					return;
				}
				State = Tasks[static_cast<std::size_t>(TaskId)];
			}
			if (State)
			{
				State->Abandon();
			}
		}

		void FTaskGroup::RequestShutdown()
		{
			std::lock_guard<std::mutex> GroupLock(Mu);
			for (const auto& State : Tasks)
			{
				if (!State)
				{
					continue;
				}
				std::lock_guard<std::mutex> StateLock(State->Mu);
				if (!State->bAbandoned)
				{
					State->Shutdown.store(true, std::memory_order_release);
				}
			}
		}

		void FTaskGroup::JoinAll(const std::int64_t TimeoutMs)
		{
			std::vector<std::shared_ptr<FTaskState>> Snapshot;
			{
				std::lock_guard<std::mutex> Lock(Mu);
				Snapshot = Tasks;
			}
			// ResetCompleted can release a finished generation while another waiter
			// is collecting it. Retain the original states instead of indexing a
			// vector that can be cleared or repopulated with a new generation.
			for (const std::shared_ptr<FTaskState>& State : Snapshot)
			{
				if (!State)
				{
					continue;
				}
				bool bTimedOut = false;
				// Owning cleanup must observe actual worker exit, even when its
				// caller is itself an interrupted worker collecting child tasks.
				if (!State->WaitAndCollect(nullptr, &bTimedOut, TimeoutMs, false) && bTimedOut)
				{
					State->Abandon();
				}
			}
		}

		bool FTaskGroup::ResetCompleted()
		{
			std::lock_guard<std::mutex> GroupLock(Mu);
			for (const std::shared_ptr<FTaskState>& State : Tasks)
			{
				if (!State)
				{
					continue;
				}
				std::lock_guard<std::mutex> StateLock(State->Mu);
				if (!State->bFinished || (!State->bThreadJoined && !State->bAbandoned))
				{
					return false;
				}
			}
			Tasks.clear();
			return true;
		}
		bool FTaskGroup::HasActiveWorkers() const
		{
			std::lock_guard<std::mutex> GroupLock(Mu);
			for (const auto& State : Tasks)
			{
				if (!State)
				{
					continue;
				}
				std::lock_guard<std::mutex> StateLock(State->Mu);
				if (!State->bFinished) return true;
			}
			return false;
		}
	}
}
