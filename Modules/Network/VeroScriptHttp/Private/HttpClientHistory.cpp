// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "HttpClientHistory.h"

namespace VeroScript::Http
{
	namespace N = Network::Detail;
	struct FAttemptHistory::FImpl
	{
		std::shared_ptr<N::FContext> Context;
		std::shared_ptr<N::FByteLease> Memory;
		// Retain the terminal operation so diagnostics also survive disposal of the caller's handle.
		std::shared_ptr<N::FOperation> Operation;
		std::mutex Mutex;
		std::vector<FAttempt> Attempts;
		std::uint32_t Active = 0;
		std::atomic<bool> Truncated{false};
	};
	FAttemptHistory::FAttemptHistory() : Impl(std::make_shared<FImpl>())
	{
	}
	FAttemptHistory::~FAttemptHistory() = default;
	std::shared_ptr<const FAttemptSnapshot> FAttemptHistory::Snapshot() const
	{
		auto P = Impl;
		FAttemptSnapshot Value;
		std::shared_ptr<N::FContext> Context;
		std::shared_ptr<N::FByteLease> Memory;
		{
			std::lock_guard<std::mutex> Lock(P->Mutex);
			Context = P->Context;
			Value.bFinal = !P->Active && (!P->Operation || N::Terminal(N::State(P->Operation)));
			Value.bTruncated = P->Truncated;
			if (P->Operation)
			{
				const N::FOperationErrorRead OperationLock(P->Operation);
				const auto& Error = OperationLock.Get();
				if (Context)
				{
					std::uint64_t Bytes =
						static_cast<std::uint64_t>(P->Attempts.size()) * sizeof(FAttempt) + Error.Message.size() + 32;
					for (const auto& A : P->Attempts)
					{
						Bytes += A.EffectiveUrl.size() + 32;
					}
					Network::FError Failure;
					Memory = N::ReserveSnapshotBytes(Context, Bytes, Failure);
					if (!Memory)
					{
						if (Failure.Code == Network::EError::Allocation)
						{
							throw std::bad_alloc();
						}
						throw std::length_error("HTTP history snapshot byte limit exceeded");
					}
				}
				Value.Error = Error;
			}
			Value.Attempts = P->Attempts;
		}
		if (!Context)
		{
			return std::make_shared<const FAttemptSnapshot>(std::move(Value));
		}
		auto Owner = N::OwnResult(Context, std::move(Value), std::move(Memory));
		const auto* Result = static_cast<const FAttemptSnapshot*>(Owner->Data);
		return std::shared_ptr<const FAttemptSnapshot>(std::move(Owner), Result);
	}
	bool Detail::FAttemptHistoryAccess::Bind(const std::shared_ptr<FAttemptHistory>& History,
		const std::shared_ptr<N::FContext>& Context, const std::shared_ptr<N::FOperation>& Op)
	{
		if (!History)
		{
			return true;
		}
		auto P = History->Impl;
		std::shared_ptr<N::FContext> OldContext;
		std::shared_ptr<N::FOperation> OldOperation;
		std::shared_ptr<N::FByteLease> OldMemory;
		std::vector<FAttempt> OldAttempts;
		{
			std::lock_guard<std::mutex> Lock(P->Mutex);
			if (P->Active || (P->Operation && !N::Terminal(N::State(P->Operation))))
			{
				return false;
			}
			OldContext = std::move(P->Context);
			OldOperation = std::move(P->Operation);
			OldMemory = std::move(P->Memory);
			OldAttempts = std::move(P->Attempts);
			P->Context = Context;
			P->Operation = Op;
			P->Truncated = false;
		}
		return true;
	}

	bool Detail::FAttemptHistoryAccess::Begin(const std::shared_ptr<FAttemptHistory>& History,
		const std::shared_ptr<N::FOperation>& Op, std::uint32_t Attempt) noexcept
	{
		if (!History)
		{
			return !N::Terminal(N::State(Op));
		}
		try
		{
			std::lock_guard<std::mutex> Lock(History->Impl->Mutex);
			// A cancelled queued factory may finish after this history has been rebound.
			// Do not activate or append an old operation after a final snapshot was observable.
			if (History->Impl->Operation != Op || N::Terminal(N::State(Op)))
			{
				return false;
			}
			History->Impl->Active = Attempt;
			return true;
		}
		catch (...)
		{
			History->Impl->Truncated = true;
			return false;
		}
	}

	void Detail::FAttemptHistoryAccess::Append(
		const std::shared_ptr<FAttemptHistory>& History, const FAttempt& Attempt) noexcept
	{
		if (!History)
		{
			return;
		}
		auto P = History->Impl;
		try
		{
			std::lock_guard<std::mutex> Lock(P->Mutex);
			if (P->Attempts.size() >= 32)
			{
				P->Truncated = true;
				return;
			}
			Network::FError Failure;
			if (!P->Memory)
			{
				P->Memory = N::ReserveBytes(P->Context, 32 * sizeof(FAttempt), Failure);
			}
			const auto Extra = Attempt.EffectiveUrl.size() + 32;
			if (!P->Memory ||
				Extra > N::Options(P->Context).MaxRetainedBytes -
							std::min(P->Memory->Size(), N::Options(P->Context).MaxRetainedBytes) ||
				!P->Memory->TryResize(P->Memory->Size() + Extra))
			{
				P->Truncated = true;
				return;
			}
			P->Attempts.reserve(32);
			P->Attempts.push_back(Attempt);
		}
		catch (...)
		{
			P->Truncated = true;
		}
	}

	void Detail::FAttemptHistoryAccess::Lost(const std::shared_ptr<FAttemptHistory>& History) noexcept
	{
		if (History)
		{
			History->Impl->Truncated = true;
		}
	}

	void Detail::FAttemptHistoryAccess::End(const std::shared_ptr<FAttemptHistory>& History,
		const std::shared_ptr<N::FOperation>& Op, std::uint32_t Attempt) noexcept
	{
		if (!History)
		{
			return;
		}
		auto P = History->Impl;
		try
		{
			std::lock_guard<std::mutex> Lock(P->Mutex);
			if (P->Operation == Op && P->Active == Attempt)
			{
				P->Active = 0;
			}
		}
		catch (...)
		{
			P->Truncated = true;
		}
	}
} // namespace VeroScript::Http
