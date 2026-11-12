// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace VeroScript::Concurrent
{
	// True if Value may cross a channel (scalars and synchronized channel handles).
	bool IsChannelPayload(const FValue& Value);
	bool IsChannelPayloadFor(const FChannel& Channel, const FValue& Value);

	/** Logical executor identity survives serial Session calls; each worker owns another. */
	class FExecutorState final
	{
	public:
		~FExecutorState();
		const std::shared_ptr<const void>& Identity() const noexcept
		{
			return Token;
		}
		void PrepareMutex(const FChannelHandle& Mutex);
		void AbandonMutexes() noexcept;

	private:
		std::shared_ptr<const void> Token = std::make_shared<int>(0);
		std::vector<std::weak_ptr<FChannel>> Mutexes;
	};

	/**
	 * Per-VM spawn group: task ids are scoped to this group.
	 * JoinAll() (or destruction) waits for every spawned worker.
	 */
	class FTaskGroup
	{
		struct FTaskState;

	public:
		/** Retains a specific task generation independently of numeric id reuse. */
		class FTicket
		{
			friend class FTaskGroup;

		public:
			explicit operator bool() const noexcept
			{
				return static_cast<bool>(State);
			}
			bool Join(std::string& Error, bool& TimedOut, std::int64_t Timeout) const;
			bool Finished() const;
			void Cancel() const noexcept;

		private:
			std::shared_ptr<FTaskState> State;
		};
		FTicket RetainTask(std::int32_t Id) const;
		FTaskGroup() = default;
		~FTaskGroup();

		FTaskGroup(const FTaskGroup&) = delete;
		FTaskGroup& operator=(const FTaskGroup&) = delete;

		/**
		 * Spawn an isolated worker. Callback runs on a new thread; return false on
		 * failure and set OutError. Returns a task id for Join.
		 */
		std::int32_t Spawn(std::function<bool(std::string& OutError)> Work);
		std::int32_t Spawn(std::function<bool(
		        std::shared_ptr<std::atomic_bool> Cancellation,
		        std::string& OutError)>
		        Work);
		void RequestCancel(std::int32_t TaskId);

		/**
		 * Block until the task finishes. Idempotent. Rejects self-join.
		 * Returns false if the task failed, id is invalid, or TimeoutMs elapses
		 * (OutTimedOut set when provided). TimeoutMs < 0 waits forever.
		 */
		bool Join(
		    std::int32_t TaskId,
		    std::string& OutError,
		    bool* OutTimedOut = nullptr,
		    std::int64_t TimeoutMs = -1);

		// Query completion without waiting.
		bool IsFinished(
		    std::int32_t TaskId,
		    bool& OutFinished,
		    std::string& OutError) const;

		/**
		 * Stop waiting on a still-running task (detach). Used after join timeout so
		 * JoinAll / destruction cannot hang. Safe no-op if already finished.
		 */
		void Abandon(std::int32_t TaskId);

		// Interrupt Core channel waits in owned workers before the owner joins.
		// Explicit cancellation and detached workers retain their own contracts.
		void RequestShutdown();

		/**
		 * Join every task. TimeoutMs < 0 waits forever per task; on timeout the
		 * straggler is abandoned so teardown cannot hang.
		 */
		void JoinAll(std::int64_t TimeoutMs = -1);

		/**
		 * Release the completed task generation after an owning VM execution ends.
		 * Returns false and changes nothing if any worker is unfinished or has not
		 * been joined/abandoned. Task ids are intentionally scoped to one VM execution.
		 */
		bool ResetCompleted();
		bool HasActiveWorkers() const;

	private:
		mutable std::mutex Mu;
		std::vector<std::shared_ptr<FTaskState>> Tasks;
	};

	struct FTypedTaskState
	{
		enum class EStatus : std::uint8_t
		{
			Active,
			Consumed,
			Failed,
			Cancelled
		};
		std::shared_ptr<const void> Executor;
		std::shared_ptr<const void> ProgramOwner;
		std::uint64_t Generation = 0;
		std::int32_t Id = -1;
		std::string ElementType;
		FTaskGroup::FTicket Ticket;
		EStatus Status = EStatus::Active;
		bool Waiting = false;
	};
}
