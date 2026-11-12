// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebSocketReconnect.h"
#include "NetworkProvider.h"
#include "WebSocketInternal.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace VeroScript::WebSocket
{
	namespace N = Network::Detail;
	namespace Detail
	{
		constexpr std::size_t MessageLimit = 4096;
		static void ErrorCopy(Network::FError& Out, bool& Truncated, const Network::FError& Input)
		{
			Out.Code = Input.Code;
			Out.SystemCode = Input.SystemCode;
			auto End = std::min(Input.Message.size(), MessageLimit);
			if (End < Input.Message.size())
			{
				while (End && (static_cast<unsigned char>(Input.Message[End]) & 0xc0) == 0x80)
				{
					--End;
				}
			}
			Out.Message.assign(Input.Message.data(), End);
			Truncated = Input.Message.size() > End;
		}
		struct FReconnectState
		{
			std::mutex Mutex;
			std::shared_ptr<N::FByteLease> Memory;
			std::uint64_t Generation = 0;
			std::vector<FReconnectAttempt> Slots;
			std::size_t Started = 0;
			bool Final = false, MessageTruncated = false;
			Network::FError Error;
			// This slot also covers cancellation between starting a child and
			// registering its terminal notification. It never stores a VM value.
			Network::FOperation Child;
			void Finish(Network::EOperationState Status, const Network::FError& Failure)
			{
				Network::FOperation Cancel;
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					if (Final)
					{
						return;
					}
					Final = true;
					ErrorCopy(Error, MessageTruncated, Failure);
					if (Started && !Slots[Started - 1].bComplete)
					{
						Slots[Started - 1].bComplete = true;
						ErrorCopy(Slots[Started - 1].Error, Slots[Started - 1].bMessageTruncated, Failure);
					}
					if (Status != Network::EOperationState::Succeeded)
					{
						Cancel = Child;
					}
				}
				if (Cancel.IsValid())
				{
					Cancel.Cancel();
				}
			}
		};

		struct FReconnect : std::enable_shared_from_this<FReconnect>
		{
			std::shared_ptr<N::FContext> Context;
			std::shared_ptr<N::FOperation> Op;
			std::shared_ptr<FReconnectState> State;
			std::string Url;
			FReconnectPolicy Policy;
			FOptions Options;
			std::shared_ptr<Network::Backend::ITimer> Timer;
			std::chrono::steady_clock::time_point Deadline;
			Network::FOperation Child;
			std::uint32_t Attempt = 0;
			std::shared_ptr<const Network::Detail::FOwnedResult> Result;
			std::shared_ptr<FConnection>* ResultConnection = nullptr;

			bool Retryable(Network::EError Code) const
			{
				switch (Code)
				{
				case Network::EError::Resolve:
				case Network::EError::Connect:
				case Network::EError::Read:
				case Network::EError::Write:
				case Network::EError::Closed:
				case Network::EError::TimedOut:
					return true;
				case Network::EError::Protocol:
					return Policy.bRetryProtocolErrors;
				default:
					return false;
				}
			}
			void Cleanup()
			{
				if (Timer)
				{
					Timer->Cancel();
				}
				if (Child.IsValid() && !Child.IsComplete())
				{
					Child.Cancel();
				}
				Child = {};
				Network::FOperation Retired;
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					Retired = std::move(State->Child);
				}
			}
			void AttemptDone(std::uint32_t Number)
			{
				if (Number != Attempt)
				{
					return;
				}
				if (N::Terminal(N::State(Op)))
				{
					Cleanup();
					return;
				}
				const auto Failure = Child.Error();
				auto Connected = Child.Result<std::shared_ptr<FConnection>>();
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					if (State->Final)
					{
						return;
					}
					auto& Record = State->Slots[Number - 1];
					Record.bComplete = true;
					Record.bConnected = !Failure && Connected && *Connected;
					ErrorCopy(Record.Error, Record.bMessageTruncated, Failure);
					State->Child = {};
				}
				if (!Failure && Connected && *Connected)
				{
					*ResultConnection = *Connected;
					if (!N::Finish(Context, Op, Network::EOperationState::Succeeded, {}, Result))
					{
						ResultConnection->reset();
					}
					Child = {};
					return;
				}
				Child = {};
				if (!Failure)
				{
					N::Fail(Op, Network::EError::Internal, "WebSocket attempt completed without a connection");
					return;
				}
				if (Attempt >= Policy.MaxAttempts || !Retryable(Failure.Code))
				{
					N::Finish(Context, Op, Network::EOperationState::Failed, Failure);
					return;
				}
				auto Delay = Policy.Delay;
				if (Policy.bExponentialBackoff)
				{
					for (std::uint32_t I = 1; I < Attempt && Delay < Policy.MaxDelay; ++I)
					{
						Delay = std::min(Policy.MaxDelay, Delay * 2);
					}
				}
				const auto Remaining =
					std::chrono::ceil<std::chrono::milliseconds>(Deadline - std::chrono::steady_clock::now());
				if (Remaining <= Delay)
				{
					// Keep the original parent deadline; do not publish the last
					// transport error in place of expiry during the retry delay.
					Timer->ExpiresAt(Deadline);
					Timer->AsyncWait(
						[Self = shared_from_this()](std::error_code Error)
						{
							if (!Error)
							{
								Self->Expire();
							}
						});
					return;
				}
				Timer->ExpiresAt(std::chrono::steady_clock::now() + Delay);
				Timer->AsyncWait(
					[Self = shared_from_this()](std::error_code Error)
					{
						if (!Error)
						{
							Self->Next();
						}
					});
			}
			void Expire()
			{
				if (!N::Terminal(N::State(Op)))
				{
					N::Finish(Context, Op,
						Network::EOperationState::TimedOut,
						{Network::EError::TimedOut, "WebSocket reconnect deadline exceeded", 0});
				}
			}
			void Next()
			{
				if (N::Terminal(N::State(Op)))
				{
					Cleanup();
					return;
				}
				const auto Remaining =
					std::chrono::ceil<std::chrono::milliseconds>(Deadline - std::chrono::steady_clock::now());
				if (Remaining.count() <= 0)
				{
					Expire();
					return;
				}
				N::Running(Op);
				++Attempt;
				auto Copy = Options;
				if (!Copy.Operation.Timeout || *Copy.Operation.Timeout > Remaining)
				{
					Copy.Operation.Timeout = Remaining;
				}
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					if (State->Final)
					{
						return;
					}
					State->Started = Attempt;
					State->Slots[Attempt - 1].Number = Attempt;
				}
				Child = FConnectionAccess::Connect(Context, Url, std::move(Copy));
				bool Final;
				{
					std::lock_guard<std::mutex> Lock(State->Mutex);
					State->Child = Child;
					Final = State->Final;
				}
				if (Final)
				{
					Cleanup();
					return;
				}
				auto Native = N::FAccess::Operation(Child);
				if (!Native)
				{
					N::Fail(Op, Network::EError::Internal, "WebSocket attempt is invalid");
					return;
				}
				N::ObserveTerminal(Native,
					[Self = shared_from_this(), Number = Attempt](auto, const auto&)
					{
						N::Post(Self->Context,
							[Self, Number]
							{
								Self->AttemptDone(Number);
							});
					});
			}
		};
	} // namespace Detail

	FReconnectTask ConnectWithRetry(Network::FContext& Public, std::string Url, FReconnectPolicy Policy, FOptions Options)
	{
		auto Context = N::FAccess::Context(Public);
		const auto Year = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(24 * 365));
		const bool Valid = Policy.MaxAttempts && Policy.MaxAttempts <= 32 && Policy.Delay.count() >= 0 &&
						   Policy.MaxDelay >= Policy.Delay && Policy.MaxDelay <= Year &&
						   Policy.MaxElapsed.count() > 0 && Policy.MaxElapsed <= Year &&
						   Url.size() <= N::Options(Context).MaxBufferBytes;
		Network::FOperationOptions Total;
		Total.Timeout = Valid ? std::optional<std::chrono::milliseconds>(Policy.MaxElapsed) : std::nullopt;
		const auto Started = std::chrono::steady_clock::now();
		auto Op = N::Begin(Context, Total);
		FReconnectTask Task;
		Task.Op = N::FAccess::Wrap(Op);
		if (N::Terminal(N::State(Op)))
		{
			return Task;
		}
		try
		{
			auto State = std::make_shared<Detail::FReconnectState>();
			Task.State = State;
			State->Generation = N::OperationId(Op);
			State->Error.Message.reserve(Detail::MessageLimit);
			N::ObserveTerminal(Op,
				[State](auto Status, const auto& Error)
				{
					State->Finish(Status, Error);
				});
			if (!Valid)
			{
				N::Fail(Op, Network::EError::InvalidArgument, "Invalid WebSocket reconnect policy");
				return Task;
			}
			Network::FError Failure;
			const auto Base =
				std::uint64_t(Policy.MaxAttempts) * (sizeof(FReconnectAttempt) + Detail::MessageLimit + 32) + 8192;
			State->Memory = N::ReserveBytes(Context, Base, Failure);
			if (!State->Memory)
			{
				N::Fail(Op, Failure.Code, "WebSocket reconnect history exceeds the context limit");
				return Task;
			}
			State->Slots.resize(Policy.MaxAttempts);
			for (auto& Slot : State->Slots)
			{
				Slot.Error.Message.reserve(Detail::MessageLimit);
			}
			auto P = std::make_shared<Detail::FReconnect>();
			P->Context = Context;
			P->Op = Op;
			P->State = State;
			P->Url = std::move(Url);
			P->Policy = Policy;
			P->Options = std::move(Options);
			P->Deadline = Started + Policy.MaxElapsed;
			P->Timer = N::Runtime(Context)->CreateTimer();
			if (!P->Timer)
			{
				N::Fail(Op, Network::EError::Unsupported, "WebSocket backend did not provide a retry timer");
				return Task;
			}
			std::uint64_t Bytes = Base;
			auto Charge = [&](std::size_t Size)
			{
				if (Size > N::Options(Context).MaxRetainedBytes - std::min(Bytes, N::Options(Context).MaxRetainedBytes))
				{
					throw std::length_error("WebSocket reconnect options exceed the context limit");
				}
				Bytes += Size;
			};
			Charge(P->Url.capacity());
			Charge(P->Options.ProxyUrl.capacity());
			Charge(P->Options.ProxyAuthorization.capacity());
			Charge(P->Options.Tls.TrustPem.capacity());
			Charge(P->Options.Tls.CertificateChainPem.capacity());
			Charge(P->Options.Tls.PrivateKeyPem.capacity());
			Charge(P->Options.Tls.PrivateKeyPassword.capacity());
			Charge(P->Options.Subprotocols.capacity() * sizeof(std::string));
			for (const auto& Protocol : P->Options.Subprotocols)
			{
				Charge(Protocol.capacity());
			}
			if (!State->Memory->TryResize(Bytes))
			{
				N::Fail(Op, Network::EError::Limit, "WebSocket reconnect storage exceeds the context limit");
				return Task;
			}
			auto Prepared = N::CreateTypedResult(Context, {}, std::shared_ptr<FConnection>{});
			P->ResultConnection = static_cast<std::shared_ptr<FConnection>*>(Prepared->Data);
			P->Result = Prepared;
			N::SetAbort(Op,
				[P]
				{
					P->Cleanup();
				});
			N::Post(Context,
				[P]
				{
					P->Next();
				});
		}
		catch (const std::bad_alloc&)
		{
			N::Fail(Op, Network::EError::Allocation, "WebSocket reconnect allocation failed");
		}
		catch (const std::length_error&)
		{
			N::Fail(Op, Network::EError::Limit, "WebSocket reconnect storage exceeds the context limit");
		}
		catch (...)
		{
			N::Fail(Op, Network::EError::Internal, "WebSocket reconnect setup failed");
		}
		return Task;
	}

	const Network::FOperation& FReconnectTask::Operation() const noexcept
	{
		return Op;
	}
	bool FReconnectTask::TrySnapshot(FReconnectSnapshot& Out, Network::FError& Error) const
	{
		Error = {};
		try
		{
			if (!State)
			{
				Error.Code = Network::EError::State;
				return false;
			}
			if (Op.IsComplete())
			{
				State->Finish(Op.State(), Op.Error());
			}
			FReconnectSnapshot Copy;
			{
				std::lock_guard<std::mutex> Lock(State->Mutex);
				Copy.Generation = State->Generation;
				Copy.bFinal = State->Final;
				Copy.Error = State->Error;
				Copy.bMessageTruncated = State->MessageTruncated;
				Copy.Attempts.assign(
					State->Slots.begin(), State->Slots.begin() + static_cast<std::ptrdiff_t>(State->Started));
			}
			Out = std::move(Copy);
			return true;
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
			return false;
		}
		catch (...)
		{
			Error.Code = Network::EError::Internal;
			return false;
		}
	}

	std::shared_ptr<const FReconnectSnapshot> FReconnectTask::Snapshot(Network::FError& Error) const
	{
		Error = {};
		try
		{
			if (!State || !State->Memory)
			{
				Error.Code = Network::EError::State;
				return {};
			}
			if (Op.IsComplete())
			{
				State->Finish(Op.State(), Op.Error());
			}
			struct FOwned
			{
				std::shared_ptr<N::FByteLease> Memory;
				FReconnectSnapshot Data;
			};
			auto Owned = std::make_shared<FOwned>();
			std::lock_guard<std::mutex> Lock(State->Mutex);
			const auto Estimate = sizeof(FReconnectSnapshot) +
								  State->Started * (sizeof(FReconnectAttempt) + Detail::MessageLimit + 32) +
								  Detail::MessageLimit + 32;
			Owned->Memory = N::ReserveSnapshotBytes(State->Memory->OwnerContext(), Estimate, Error);
			if (!Owned->Memory)
			{
				return {};
			}
			Owned->Data.Generation = State->Generation;
			Owned->Data.bFinal = State->Final;
			Owned->Data.Error = State->Error;
			Owned->Data.bMessageTruncated = State->MessageTruncated;
			Owned->Data.Attempts.assign(
				State->Slots.begin(), State->Slots.begin() + static_cast<std::ptrdiff_t>(State->Started));
			std::uint64_t Actual = sizeof(FReconnectSnapshot) + Owned->Data.Error.Message.capacity() +
								   Owned->Data.Attempts.capacity() * sizeof(FReconnectAttempt);
			for (const auto& A : Owned->Data.Attempts)
			{
				Actual += A.Error.Message.capacity();
			}
			if (!Owned->Memory->TryResize(Actual))
			{
				Error.Code = Network::EError::Limit;
				return {};
			}
			const auto* Data = &Owned->Data;
			return std::shared_ptr<const FReconnectSnapshot>(std::move(Owned), Data);
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = Network::EError::Allocation;
			return {};
		}
		catch (...)
		{
			Error.Code = Network::EError::Internal;
			return {};
		}
	}
} // namespace VeroScript::WebSocket
