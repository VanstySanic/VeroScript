// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkProvider.h"

namespace VeroScript::Network::Detail
{
	struct FFilePool;

	struct FOperation
	{
		// Declared first: timers, callbacks and results are destroyed before their I/O/code owner.
		std::shared_ptr<FContext> Lifetime;
		std::weak_ptr<FContext> Context;
		std::mutex Mutex;
		std::condition_variable Changed;
		std::atomic<EOperationState> Status{EOperationState::Created};
		FError Failure;
		std::shared_ptr<const Network::Detail::FOwnedResult> Result;
		std::function<void()> Abort;
		// Internal provider notification only: no script and no I/O work in this hook.
		std::function<void(EOperationState, const FError&)> TerminalObserver;
		std::function<void(const Network::FOperation&)> Callback;
		bool bCallbackRegistered = false;
		std::uint64_t Id = 0;
		std::shared_ptr<Backend::ITimer> Timer;
		// Intrusive cleanup chain: Fault can retain abort/timer work without allocating.
		std::shared_ptr<FOperation> ShutdownNext;
	};

	struct FContext : std::enable_shared_from_this<FContext>
	{
		FContext(std::shared_ptr<Backend::IService> InService, FContextOptions InOptions);
		~FContext();
		void StartWorker();
		void Post(std::function<void()> Function);
		void QueueCallback(const std::shared_ptr<FOperation>& Operation);
		std::uint64_t AddResource(std::function<void()> Close);
		bool ReplaceResource(std::uint64_t Id, std::function<void()> Close);
		void RemoveResource(std::uint64_t Id);
		bool Finish(const std::shared_ptr<FOperation>& Operation, EOperationState State, FError Error = {},
			std::shared_ptr<const Network::Detail::FOwnedResult> Result = {});
		void Shutdown();
		void Drain();
		void Fault(EError Code) noexcept;
		// Declared first so code remains loaded while all other members are destroyed.
		std::shared_ptr<Backend::IService> Service;
		// Capture allocation in the context creator, outside a dynamically loaded
		// provider. Hot operation/result/byte leases keep their single allocation.
		std::shared_ptr<FOperation> (*CreateOperation)() = nullptr;
		std::shared_ptr<FOwnedResult> (*CreateResult)(std::shared_ptr<FContext>,
			std::shared_ptr<const void>, std::any) = nullptr;
		std::shared_ptr<FByteLease> (*CreateByteLease)(std::shared_ptr<FContext>) = nullptr;
		std::shared_ptr<const void> (*RetainObject)(std::shared_ptr<const void>,
			std::shared_ptr<const void>) = nullptr;
		std::vector<std::shared_ptr<const void>> CodeOwners;
		std::shared_ptr<Backend::IRuntime> Runtime;
		std::mutex FilePoolMutex;
		std::shared_ptr<FFilePool> FilePool;
		FContextOptions Options;
		std::thread Worker;
		std::thread::id WorkerId;
		std::thread::id Owner;
		std::mutex Mutex;
		std::recursive_mutex ShutdownMutex;
		std::mutex PostMutex;
		std::recursive_mutex ControlMutex;
		std::unordered_map<std::uint64_t, std::shared_ptr<FOperation>> Active;
		std::shared_ptr<FOperation> FaultedOperations;
		std::deque<std::shared_ptr<FOperation>> Completed;
		std::unordered_map<std::uint64_t, std::function<void()>> Resources;
		std::atomic<bool> Closed{false};
		std::atomic<std::uint64_t> FaultEpoch{0};
		std::atomic<std::uint64_t> RetainedBytes{0}, PeakRetainedBytes{0};
		std::uint64_t NextId = 1;
		std::uint32_t CallbackCount = 0;
		bool bDispatching = false;
		bool bPolling = false; // Owner thread, under ControlMutex.
		bool bDrained = false;
		std::atomic<bool> bShutdownStarted{false};
		std::atomic<bool> bShutdownRequested{false};
		std::atomic<bool> bWorkerRunning{false};
	};

} // namespace VeroScript::Network::Detail
