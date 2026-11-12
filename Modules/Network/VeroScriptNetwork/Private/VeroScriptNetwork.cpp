// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetwork.h"
#include "NetworkInternal.h"
#include <cstring>
#include <stdexcept>

namespace VeroScript::Network::Detail
{
	bool Terminal(EOperationState State) noexcept
	{
		return State >= EOperationState::Succeeded;
	}

	FContext::FContext(std::shared_ptr<Backend::IService> InService, FContextOptions InOptions)
	    : Service(std::move(InService)), Options(InOptions), Owner(std::this_thread::get_id())
	{
		CreateOperation = []
		{
			return std::make_shared<FOperation>();
		};
		CreateResult = [](std::shared_ptr<FContext> Context, std::shared_ptr<const void> Retention, std::any Value)
		{
			return std::make_shared<FOwnedResult>(FOwnedResult{
			    std::move(Context), std::move(Retention), std::move(Value)});
		};
		CreateByteLease = [](std::shared_ptr<FContext> Context)
		{
			return std::make_shared<FByteLease>(std::move(Context));
		};
		RetainObject = [](std::shared_ptr<const void> Object, std::shared_ptr<const void> Lifetime)
		{
			struct FRetainedObject
			{
				std::shared_ptr<const void> Lifetime;
				std::shared_ptr<const void> Object;
			};
			const auto* Pointer = Object.get();
			auto Owner = std::make_shared<FRetainedObject>(FRetainedObject{std::move(Lifetime), std::move(Object)});
			return std::shared_ptr<const void>(std::move(Owner), Pointer);
		};
		if (!Options.MaxOperations || !Options.MaxCallbacks || !Options.MaxConnections || !Options.MaxQueuedWrites ||
		    !Options.MaxBufferBytes || !Options.MaxRetainedBytes)
		{
			throw std::invalid_argument("Network limits must be positive");
		}
		if (!Service)
		{
			throw std::invalid_argument("Network context requires an explicit backend service");
		}
		if (!Service->GetFoundationIdentity() ||
		    std::string_view(Service->GetFoundationIdentity()) != Backend::FoundationIdentity())
		{
			throw std::invalid_argument("Network backend/foundation implementation mismatch");
		}
		Runtime = Service->CreateRuntime();
		if (!Runtime)
		{
			throw std::runtime_error("Network backend did not create its runtime");
		}
		if (!Runtime->GetBackendId() || !Service->GetBackendId() ||
		    std::string_view(Runtime->GetBackendId()) != Service->GetBackendId())
		{
			throw std::invalid_argument("Network runtime/backend identity mismatch");
		}
	}

	FContext::~FContext()
	{
		Shutdown();
	}

	void FContext::StartWorker()
	{
		if (Options.Mode == EDriveMode::NativeBackground)
		{
			Worker = std::thread(
			    [Self = shared_from_this()]
			{
				Self->bWorkerRunning.store(true);
				try
				{
					Self->Runtime->Run();
				}
				catch (const std::bad_alloc&)
				{
					Self->Fault(EError::Allocation);
				}
				catch (...)
				{
					Self->Fault(EError::Internal);
				}
				if (Self->bShutdownRequested.load())
				{
					// An external closer may already hold this lock while joining us.
					std::unique_lock<std::recursive_mutex> Lock(Self->ShutdownMutex, std::defer_lock);
					while (!Lock.try_lock())
					{
						// try_lock can fail spuriously. Only an established closer may take over cleanup.
						if (Self->bShutdownStarted.load())
						{
							break;
						}
						std::this_thread::yield();
					}
					if (Lock.owns_lock() && !Self->bShutdownStarted.exchange(true))
					{
						Self->Drain();
						// I/O and cleanup have ended; Self pins the state until this thread returns.
						Self->Worker.detach();
					}
				}
				Self->bWorkerRunning.store(false);
			});
			WorkerId = Worker.get_id();
		}
	}

	void FContext::Post(std::function<void()> Function)
	{
		{
			std::lock_guard<std::mutex> Lock(PostMutex);
			if (!bDrained)
			{
				Runtime->Post(std::move(Function));
				return;
			}
		}
		std::lock_guard<std::recursive_mutex> Lock(ControlMutex);
		Function();
	}

	void FContext::QueueCallback(const std::shared_ptr<FOperation>& Operation)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Completed.push_back(Operation);
	}

	std::uint64_t FContext::AddResource(std::function<void()> Close)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (Closed || Resources.size() >= Options.MaxConnections)
		{
			return 0;
		}
		auto Id = NextId++;
		Resources.emplace(Id, std::move(Close));
		return Id;
	}

	bool FContext::ReplaceResource(std::uint64_t Id, std::function<void()> Close)
	{
		std::function<void()> Previous;
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			const auto I = Resources.find(Id);
			if (Closed || I == Resources.end())
			{
				return false;
			}
			Previous = std::move(I->second);
			I->second = std::move(Close);
		}
		return true;
	}

	void FContext::RemoveResource(std::uint64_t Id)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Resources.erase(Id);
	}

	bool FContext::Finish(const std::shared_ptr<FOperation>& Operation, EOperationState State, FError Error,
	    std::shared_ptr<const Network::Detail::FOwnedResult> Result)
	{
		std::function<void()> Abort;
		bool bCleanup = false;
		std::function<void(EOperationState, const FError&)> Observer;
		bool bCallback = false;
		{
			std::lock_guard<std::mutex> Lock(Operation->Mutex);
			if (Terminal(Operation->Status.load()))
			{
				return false;
			}
			Operation->Failure = std::move(Error);
			Operation->Result = std::move(Result);
			bCleanup =
			    Operation->Timer ||
			    (Operation->Abort && (State == EOperationState::Cancelled || State == EOperationState::TimedOut));
			if (!bCleanup)
			{
				Abort.swap(Operation->Abort);
			}
			Observer.swap(Operation->TerminalObserver);
			bCallback = static_cast<bool>(Operation->Callback);
			Operation->Status.store(State);
		}
		Operation->Changed.notify_all();
		if (Observer)
		{
			try
			{
				Observer(State, Operation->Failure);
			}
			catch (...)
			{
			}
		}
		bool bDropCallback = false;
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (bCallback && !Closed)
			{
				Completed.push_back(Operation);
			}
			else
			{
				bDropCallback = bCallback;
			}
		}
		if (bDropCallback)
		{
			std::function<void(const Network::FOperation&)> Callback;
			{
				std::lock_guard<std::mutex> Lock(Operation->Mutex);
				Callback.swap(Operation->Callback);
			}
		}
		if (bCleanup)
		{
			Post(
			    [Operation, State]
			{
				std::function<void()> Abort;
				{
					std::lock_guard<std::mutex> Lock(Operation->Mutex);
					Abort.swap(Operation->Abort);
				}
				if (Operation->Timer)
				{
					Operation->Timer->Cancel();
				}
				if (Abort && (State == EOperationState::Cancelled || State == EOperationState::TimedOut))
				{
					Abort();
				}
			});
		}
		// Shutdown must still find the timer if this cleanup post waits for its control lock.
		// A failed completion-queue growth or post also leaves the operation visible to Fault.
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			// A closed context still needs to find a terminal operation whose cleanup was not queued.
			if (!Closed)
			{
				Active.erase(Operation->Id);
			}
		}
		return true;
	}

	void FContext::Shutdown()
	{
		// A provider owner can be released inside a handler. Defer cleanup until run/poll returns.
		// This check also avoids waiting for a shutdown lock held by an external joining thread.
		if ((bWorkerRunning.load() && WorkerId == std::this_thread::get_id()) ||
		    (Options.Mode == EDriveMode::HostDriven && Owner == std::this_thread::get_id() && bPolling))
		{
			if (!bShutdownStarted.load())
			{
				Closed.store(true);
				bShutdownRequested.store(true);
				Runtime->Stop();
			}
			return;
		}
		std::lock_guard<std::recursive_mutex> ShutdownLock(ShutdownMutex);
		if (bShutdownStarted)
		{
			return;
		}
		Closed.store(true);
		Runtime->Stop();
		bShutdownStarted = true;
		if (Worker.joinable())
		{
			Worker.join();
		}
		Drain();
	}

	void FContext::Drain()
	{
		std::lock_guard<std::recursive_mutex> ControlLock(ControlMutex);
		{
			std::lock_guard<std::mutex> Lock(PostMutex);
			bDrained = true;
		}
		Runtime->ReleaseWork();
		// Extract one entry at a time: shutdown also has to work with no free heap.
		std::shared_ptr<FOperation> Cleanup;
		for (;;)
		{
			std::shared_ptr<FOperation> Operation;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Active.empty())
				{
					if (!FaultedOperations)
					{
						break;
					}
					Operation = std::move(FaultedOperations);
					FaultedOperations = std::move(Operation->ShutdownNext);
				}
				else
				{
					auto It = Active.begin();
					Operation = std::move(It->second);
					Active.erase(It);
				}
			}
			std::function<void(const Network::FOperation&)> Callback;
			std::function<void(EOperationState, const FError&)> Observer;
			{
				std::lock_guard<std::mutex> Lock(Operation->Mutex);
				Callback.swap(Operation->Callback);
				Observer.swap(Operation->TerminalObserver);
				if (!Terminal(Operation->Status.load()))
				{
					Operation->Failure.Message.clear();
					Operation->Failure.Code = EError::Cancelled;
					Operation->Failure.SystemCode = 0;
					Operation->Status.store(EOperationState::Cancelled);
				}
			}
			Operation->Changed.notify_all();
			try
			{
				if (Observer)
				{
					Observer(Operation->Status.load(), Operation->Failure);
				}
			}
			catch (...)
			{
			}
			Operation->ShutdownNext = std::move(Cleanup);
			Cleanup = std::move(Operation);
		}
		// Publish every terminal state before aborting shared resources or sibling operations.
		while (Cleanup)
		{
			auto Operation = std::move(Cleanup);
			Cleanup = std::move(Operation->ShutdownNext);
			std::function<void()> Abort;
			{
				std::lock_guard<std::mutex> Lock(Operation->Mutex);
				Abort.swap(Operation->Abort);
			}
			try
			{
				if (Operation->Timer)
				{
					Operation->Timer->Cancel();
				}
				if (Operation->Status.load() != EOperationState::Succeeded && Abort)
				{
					Abort();
				}
			}
			catch (...)
			{
				// Registered resource closers below still reclaim the native handles.
			}
		}
		for (;;)
		{
			std::function<void()> Close;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Resources.empty())
				{
					break;
				}
				auto It = Resources.begin();
				Close = std::move(It->second);
				Resources.erase(It);
			}
			try
			{
				Close();
			}
			catch (...)
			{
			}
		}
		// Reclaim queued handlers and physical resolver work before releasing their owners.
		for (;;)
		{
			const auto Before = FaultEpoch.load();
			Runtime->Restart();
			try
			{
				Runtime->Run();
				if (FaultEpoch.load() == Before)
				{
					break;
				}
			}
			catch (...)
			{
				// A failed handler is removed by the runtime; continue draining the remaining handlers.
			}
		}
		for (;;)
		{
			std::shared_ptr<FOperation> Operation;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Completed.empty())
				{
					CallbackCount = 0;
					break;
				}
				Operation = std::move(Completed.front());
				Completed.pop_front();
			}
			std::function<void(const Network::FOperation&)> Callback;
			{
				std::lock_guard<std::mutex> Lock(Operation->Mutex);
				Callback.swap(Operation->Callback);
			}
		}
	}

	void FContext::Fault(EError Code) noexcept
	{
		++FaultEpoch;
		Closed.store(true);
		Runtime->Stop();
		for (;;)
		{
			std::shared_ptr<FOperation> Operation;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Active.empty())
				{
					break;
				}
				const auto It = Active.begin();
				Operation = It->second;
				Active.erase(It);
				Operation->ShutdownNext = std::move(FaultedOperations);
				FaultedOperations = Operation;
			}
			// Match OnComplete's lock order; no error allocation is needed to wake a waiter.
			std::function<void(const Network::FOperation&)> Callback;
			std::function<void(EOperationState, const FError&)> Observer;
			{
				std::lock_guard<std::mutex> Lock(Operation->Mutex);
				Callback.swap(Operation->Callback);
				Observer.swap(Operation->TerminalObserver);
				if (!Terminal(Operation->Status.load()))
				{
					Operation->Failure.Message.clear();
					Operation->Failure.Code = Code;
					Operation->Failure.SystemCode = 0;
					Operation->Status.store(EOperationState::Failed);
				}
			}
			Operation->Changed.notify_all();
			if (Observer)
			{
				try
				{
					Observer(Operation->Status.load(), Operation->Failure);
				}
				catch (...)
				{
				}
			}
		}
	}

	std::shared_ptr<FContext> FAccess::Context(Network::FContext& Value)
	{
		if (std::string_view(Value.GetFoundationIdentity()) != Backend::FoundationIdentity())
		{
			throw std::invalid_argument("Network context/foundation implementation mismatch");
		}
		return Value.Impl;
	}

	Network::FOperation FAccess::Wrap(std::shared_ptr<FOperation> Value)
	{
		Network::FOperation Result;
		Result.Impl = std::move(Value);
		return Result;
	}

	std::shared_ptr<FOperation> FAccess::Operation(const Network::FOperation& Value)
	{
		return Value.Impl;
	}

	std::shared_ptr<FOperation> Begin(const std::shared_ptr<FContext>& Context, FOperationOptions Options)
	{
		auto Op = Context->CreateOperation();
		Op->Lifetime = Context;
		Op->Context = Context;
		std::optional<std::chrono::steady_clock::time_point> Deadline;
		if (Options.Timeout)
		{
			if (Options.Timeout->count() < 0 || *Options.Timeout > std::chrono::hours(24 * 365))
			{
				Op->Failure = {EError::InvalidArgument, "Timeout must be between zero and 365 days", 0};
				Op->Status = EOperationState::Failed;
				return Op;
			}
			Deadline = std::chrono::steady_clock::now() + *Options.Timeout;
			Op->Timer = Context->Runtime->CreateTimer();
			if (!Op->Timer)
			{
				Op->Failure = {EError::Unsupported, "Network backend did not create a deadline timer", 0};
				Op->Status = EOperationState::Failed;
				return Op;
			}
		}
		{
			std::lock_guard<std::mutex> Lock(Context->Mutex);
			if (Context->Closed || Context->Active.size() >= Context->Options.MaxOperations)
			{
				Op->Failure = {
				    Context->Closed ? EError::Closed : EError::QueueFull, "Context closed or operation queue full", 0};
				Op->Status = EOperationState::Failed;
				return Op;
			}
			Op->Id = Context->NextId++;
			Op->Status = EOperationState::Queued;
			Context->Active.emplace(Op->Id, Op);
		}
		if (Deadline)
		{
			Context->Post(
			    [Op, Deadline]
			{
				if (Terminal(Op->Status))
				{
					return;
				}
				Op->Timer->ExpiresAt(*Deadline);
				Op->Timer->AsyncWait(
				    [Weak = std::weak_ptr<FOperation>(Op)](std::error_code Error)
				{
					if (!Error)
					{
						if (auto Locked = Weak.lock())
						{
							if (auto C = Locked->Context.lock())
							{
								C->Finish(Locked,
								    EOperationState::TimedOut,
								    {EError::TimedOut, "Operation deadline expired", 0});
							}
						}
					}
				});
			});
		}
		return Op;
	}

	void SetAbort(const std::shared_ptr<FOperation>& Operation, std::function<void()> Function)
	{
		bool bAborted = false;
		{
			std::lock_guard<std::mutex> Lock(Operation->Mutex);
			auto State = Operation->Status.load();
			bAborted = Terminal(State) && State != EOperationState::Succeeded;
			if (!Terminal(State))
			{
				Operation->Abort = Function;
			}
		}
		if (bAborted)
		{
			if (auto Context = Operation->Context.lock())
			{
				Context->Post(std::move(Function));
			}
		}
	}

	void ObserveTerminal(
	    const std::shared_ptr<FOperation>& Operation, std::function<void(EOperationState, const FError&)> Function)
	{
		EOperationState State;
		{
			std::lock_guard<std::mutex> Lock(Operation->Mutex);
			State = Operation->Status.load();
			if (!Terminal(State))
			{
				Operation->TerminalObserver = std::move(Function);
				return;
			}
		}
		Function(State, Operation->Failure);
	}

	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, std::string Message, int SystemCode)
	{
		if (Terminal(Operation->Status.load()))
		{
			return;
		}
		if (auto Context = Operation->Context.lock())
		{
			Context->Finish(Operation, EOperationState::Failed, {Code, std::move(Message), SystemCode});
		}
	}

	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, const char* Message, int SystemCode)
	{
		if (!Terminal(Operation->Status.load()))
		{
			Fail(Operation, Code, std::string(Message), SystemCode);
		}
	}

	void Fail(const std::shared_ptr<FOperation>& Operation, EError Code, const std::error_code& Error)
	{
		if (!Terminal(Operation->Status.load()))
		{
			Fail(Operation, Code, Error.message(), Error.value());
		}
	}

	void Running(const std::shared_ptr<FOperation>& Operation)
	{
		auto Expected = EOperationState::Queued;
		Operation->Status.compare_exchange_strong(Expected, EOperationState::Running);
	}
} // namespace VeroScript::Network::Detail

namespace VeroScript::Network
{
	const char* FError::Id() const noexcept
	{
		static constexpr const char* Ids[] = {"",
		    "network.invalid_argument",
		    "network.unsupported",
		    "network.queue_full",
		    "network.cancelled",
		    "network.timed_out",
		    "network.closed",
		    "network.resolve",
		    "network.connect",
		    "network.read",
		    "network.write",
		    "network.protocol",
		    "network.limit",
		    "network.state",
		    "network.callback",
		    "network.allocation",
		    "network.internal"};
		const auto Index = static_cast<unsigned>(Code);
		return Index < sizeof(Ids) / sizeof(*Ids) ? Ids[Index] : "network.unknown";
	}

	FError::operator bool() const noexcept
	{
		return Code != EError::None;
	}
	bool FOperation::IsValid() const noexcept
	{
		return static_cast<bool>(Impl);
	}

	EOperationState FOperation::State() const noexcept
	{
		return Impl ? Impl->Status.load() : EOperationState::Failed;
	}

	bool FOperation::IsComplete() const noexcept
	{
		return Detail::Terminal(State());
	}

	bool FOperation::Cancel() const
	{
		if (!Impl)
		{
			return false;
		}
		auto Context = Impl->Context.lock();
		if (!Context || Detail::Terminal(Impl->Status))
		{
			return false;
		}
		FError Error;
		Error.Code = EError::Cancelled;
		try
		{
			Error.Message = "Operation cancelled";
		}
		catch (const std::bad_alloc&)
		{
		}
		try
		{
			return Context->Finish(Impl, EOperationState::Cancelled, std::move(Error));
		}
		catch (const std::bad_alloc&)
		{
			Context->Fault(EError::Allocation);
		}
		catch (...)
		{
			Context->Fault(EError::Internal);
		}
		return Impl->Status == EOperationState::Cancelled;
	}

	FError FOperation::Error() const
	{
		if (!Impl)
		{
			return {EError::State, "Invalid operation", 0};
		}
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		return Impl->Failure;
	}

	EWaitResult FOperation::Wait(std::chrono::milliseconds Timeout) const
	{
		if (!Impl || IsComplete())
		{
			return EWaitResult::Completed;
		}
		auto Context = Impl->Context.lock();
		if (Context && Context->Options.Mode == EDriveMode::HostDriven)
		{
			return EWaitResult::RequiresPoll;
		}
		std::unique_lock<std::mutex> Lock(Impl->Mutex);
		return Impl->Changed.wait_for(Lock,
		           Timeout,
		           [&]
		{
			return Detail::Terminal(Impl->Status);
		})
		    ? EWaitResult::Completed
		    : EWaitResult::WaitTimedOut;
	}

	bool FOperation::OnComplete(std::function<void(const FOperation&)> Callback, FError& Error) const
	{
		Error = {};
		auto Context = Impl ? Impl->Context.lock() : nullptr;
		if (!Context || !Callback)
		{
			Error = {EError::State, "Invalid context or callback", 0};
			return false;
		}
		std::lock_guard<std::mutex> OperationLock(Impl->Mutex);
		std::lock_guard<std::mutex> ContextLock(Context->Mutex);
		if (Context->Closed)
		{
			Error = {EError::Closed, "Context closed", 0};
			return false;
		}
		if (Impl->bCallbackRegistered)
		{
			Error = {EError::State, "Completion callback already registered", 0};
			return false;
		}
		if (Context->CallbackCount >= Context->Options.MaxCallbacks)
		{
			Error = {EError::QueueFull, "Completion callback queue full", 0};
			return false;
		}
		if (IsComplete())
		{
			try
			{
				Context->Completed.push_back(Impl);
			}
			catch (const std::bad_alloc&)
			{
				Error.Code = EError::Allocation;
				return false;
			}
		}
		++Context->CallbackCount;
		Impl->bCallbackRegistered = true;
		Impl->Callback = std::move(Callback);
		return true;
	}

	std::shared_ptr<const void> FOperation::ResultValue(const char* Type, std::size_t Size, std::size_t Alignment) const
	{
		if (!Impl)
		{
			return {};
		}
		std::shared_ptr<const Detail::FOwnedResult> Result;
		{
			std::lock_guard<std::mutex> Lock(Impl->Mutex);
			Result = Impl->Result;
		}
		// Matching foundation identities enforce a common C++ ABI. Type names,
		// sizes and alignment remain stable when dlopen cannot merge RTTI addresses.
		if (!Result || !Result->Type || !Result->Data || Result->Size != Size || Result->Alignment != Alignment ||
		    std::strcmp(Result->Type, Type) != 0)
		{
			return {};
		}
		const auto* Value = Result->Data;
		return std::shared_ptr<const void>(std::move(Result), Value);
	}

	FContext::FContext(std::shared_ptr<Backend::IService> Service, FContextOptions Options)
	    : Identity(Backend::FoundationIdentity()), Impl(std::make_shared<Detail::FContext>(std::move(Service), Options))
	{
		Impl->StartWorker();
	}
	FContext::~FContext()
	{
		Shutdown();
	}
	void FContext::Shutdown()
	{
		const auto State = Impl;
		State->Shutdown();
	}
	bool FContext::IsClosed() const noexcept
	{
		return Impl->Closed;
	}
	EDriveMode FContext::Mode() const noexcept
	{
		return Impl->Options.Mode;
	}
	const char* FContext::GetBackendId() const noexcept
	{
		return Impl->Service->GetBackendId();
	}
	std::size_t FContext::PollIo(FError& Error, std::size_t MaxHandlers)
	{
		const auto State = Impl;
		auto* const P = State.get();
		Error = {};
		if (P->Closed)
		{
			return 0;
		}
		if (P->Options.Mode == EDriveMode::NativeBackground)
		{
			return 0;
		}
		if (std::this_thread::get_id() != P->Owner)
		{
			Error = {EError::State, "PollIo requires the context owner thread", 0};
			return 0;
		}
		std::size_t Count = 0;
		{
			std::lock_guard<std::recursive_mutex> ControlLock(P->ControlMutex);
			if (P->Closed)
			{
				return 0;
			}
			if (P->bPolling)
			{
				Error = {EError::State, "PollIo cannot recurse", 0};
				return 0;
			}
			P->bPolling = true;
			P->Runtime->Restart();
			try
			{
				while (!P->Closed && Count < MaxHandlers && P->Runtime->PollOne())
				{
					++Count;
				}
			}
			catch (const std::bad_alloc&)
			{
				P->Fault(EError::Allocation);
				Error.Code = EError::Allocation;
			}
			catch (...)
			{
				P->Fault(EError::Internal);
				Error.Code = EError::Internal;
			}
			P->bPolling = false;
		}
		// Release ControlMutex before acquiring ShutdownMutex; an external closer may own it.
		if (P->bShutdownRequested.load())
		{
			P->Shutdown();
		}
		return Count;
	}
	std::size_t FContext::Dispatch(FError& Error, std::size_t MaxCallbacks)
	{
		const auto ContextState = Impl;
		auto* const P = ContextState.get();
		Error = {};
		if (std::this_thread::get_id() != P->Owner || P->bDispatching)
		{
			Error = {EError::State, "Dispatch requires the owner thread and cannot recurse", 0};
			return 0;
		}
		std::vector<std::shared_ptr<Detail::FOperation>> Batch;
		{
			std::lock_guard<std::mutex> Lock(P->Mutex);
			auto Count = std::min(MaxCallbacks, P->Completed.size());
			try
			{
				Batch.reserve(Count);
			}
			catch (const std::bad_alloc&)
			{
				Error.Code = EError::Allocation;
				return 0;
			}
			for (std::size_t I = 0; I < Count; ++I)
			{
				Batch.push_back(std::move(P->Completed.front()));
				P->Completed.pop_front();
			}
			P->CallbackCount -= static_cast<std::uint32_t>(Count);
		}
		P->bDispatching = true;
		struct FDispatchScope
		{
			~FDispatchScope()
			{
				State.bDispatching = false;
			}
			Detail::FContext& State;
		} DispatchScope{*P};
		for (const auto& Op : Batch)
		{
			std::function<void(const FOperation&)> Callback;
			{
				std::lock_guard<std::mutex> Lock(Op->Mutex);
				Callback.swap(Op->Callback);
			}
			try
			{
				if (Callback && !P->Closed)
				{
					Callback(Detail::FAccess::Wrap(Op));
				}
			}
			catch (const std::exception& Exception)
			{
				if (!Error)
				{
					Error.Code = EError::Callback;
					try
					{
						Error.Message = Exception.what();
					}
					catch (const std::bad_alloc&)
					{
					}
				}
			}
			catch (...)
			{
				if (!Error)
				{
					Error.Code = EError::Callback;
					try
					{
						Error.Message = "Completion callback threw";
					}
					catch (const std::bad_alloc&)
					{
					}
				}
			}
		}
		return Batch.size();
	}

	bool FContext::RetainCodeOwner(std::shared_ptr<const void> Owner, FError& Error)
	{
		if (std::string_view(Identity) != Backend::FoundationIdentity())
		{
			Error = {EError::Unsupported, "Network context/foundation implementation mismatch", 0};
			return false;
		}
		return Detail::RetainCodeOwner(Impl, std::move(Owner), Error);
	}
} // namespace VeroScript::Network
