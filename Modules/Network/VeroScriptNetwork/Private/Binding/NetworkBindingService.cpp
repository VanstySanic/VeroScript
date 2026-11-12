// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkBinding.h"
#include "VeroScriptValueRoot.h"
#include <thread>

namespace VeroScript::NetworkBinding
{
	namespace
	{
		struct FContextScope
		{
			std::weak_ptr<Network::FContext> Context;
			~FContextScope()
			{
				try
				{
					if (const auto Value = Context.lock())
					{
						Value->Shutdown();
					}
				}
				catch (...)
				{
				} // Final native cleanup cannot unwind a value destructor.
			}
		};
	} // namespace
	// Foreign protocol modules ask the Network owner to construct Network-owned host objects.
	class FNetworkBindingService final : public INetworkBindingService, public std::enable_shared_from_this<FNetworkBindingService>
	{
	public:
		explicit FNetworkBindingService(FState& S) : State(S)
		{
		}
		bool Initialize(const std::shared_ptr<Network::FContext>& Context, IScriptCallInvoker& Invoker, std::string& Error)
		{
			Program = Invoker.GetProgramOwner();
			Generation = Invoker.GetProgramGeneration();
			// An internal context shell supplies the generation-end action. Core
			// allocates its wrapper and pins the payload/deleter code until teardown
			// returns. The shell is never published and holds the context weakly.
			auto Scope = std::make_shared<FContextScope>();
			Scope->Context = Context;
			// A null alias owns the guard without exposing a dispatchable service.
			std::shared_ptr<const INetworkBindingService> Lifetime(Scope, nullptr);
			FValue Value;
			if (!Make(State, "Network::context", FContextValue{nullptr, std::move(Lifetime)}, Value, Error))
			{
				return false;
			}
			return Invoker.RetainForGeneration(Value, ScopeRoot, Error);
		}
		bool MakeReadable(const FHostObjectHandle& Context, std::shared_ptr<Network::FReadableStream> Stream, FValue& Out,
			std::string& Error) const override
		{
			return Make(State, "Network::readable_stream", FReadStreamValue{Context, std::move(Stream), {}}, Out, Error);
		}
		bool MakeFileProgress(Network::FFileProgress Progress, FValue& Out, std::string& Error) const override
		{
			return Make(State, "Network::file_progress", std::move(Progress), Out, Error);
		}
		bool MakeUpgradeSource(const FHostObjectHandle& Context, std::shared_ptr<Network::FUpgradeSource> Source,
			FValue& Out, std::string& Error) const override
		{
			return Make(State, "Network::upgrade_source", FUpgradeSourceValue{Context, std::move(Source)}, Out, Error);
		}
		bool AttachCompletion(const FHostObjectHandle& Context, const Network::FOperation& Operation,
			const FValue& Callback, const FHostObjectHandle& Receiver, IScriptCallInvoker& Invoker,
			std::string& Error) const override
		{
			if (!CheckScope(Invoker, Error))
			{
				return false;
			}
			const auto* Function = std::get_if<FFunctionHandle>(&Callback);
			if (!Function || !*Function)
			{
				Error = "Completion callback is unbound";
				return false;
			}
			FValueRoot Root;
			if (!Invoker.RetainForGeneration(Callback, Root, Error))
			{
				return false;
			}
			// A receiver may escape into script globals after dispatch. It must not
			// retain the operation or context while the callback is still queued.
			if (!Receiver || !Receiver->Payload)
			{
				Error = "Completion receiver is unavailable";
				return false;
			}
			const auto Program = Invoker.GetProgramOwner();
			const auto Generation = Invoker.GetProgramGeneration();
			const std::weak_ptr<const FNetworkBindingService> Service = shared_from_this();
			Network::FError Failure;
			const bool Registered = Operation.OnComplete(
				[Service,
					Context = std::weak_ptr<FHostObject>(Context),
					Root = std::move(Root),
					Receiver,
					Program,
					Generation](const auto& Completed)
				{
					const auto Self = Service.lock();
					const auto OwnerContext = Context.lock();
					const auto* Value = Root.Read();
					if (!Self || !OwnerContext || !Value)
					{
						return;
					}
					if (!Self->ActiveInvoker || !Self->DispatchError)
					{
						throw std::logic_error("Script completions require an active script dispatch");
					}
					if (Self->bDispatchFailed || !Self->DispatchError->empty())
					{
						return;
					}
					// Stop the remaining script callbacks even if preparing arguments,
					// invoking script or recording its diagnostic throws.
					Self->bDispatchFailed = true;
					if (Self->ActiveInvoker->GetProgramOwner() != Program ||
						Self->ActiveInvoker->GetProgramGeneration() != Generation)
					{
						*Self->DispatchError = "Completion belongs to a different script program generation";
						return;
					}
					auto& Payload = *static_cast<FOperationValue*>(Receiver->Payload);
					Payload.Context = OwnerContext;
					Payload.Value = Completed;
					std::vector<FValue> Arguments{Receiver};
					FValue Result;
					std::string Error;
					const auto Callable = std::get<FFunctionHandle>(*Value);
					if (!Self->ActiveInvoker->InvokeCallable(Callable, Arguments, Result, Error))
					{
						*Self->DispatchError = Error.empty() ? "Script completion failed" : std::move(Error);
					}
					else
					{
						Self->bDispatchFailed = false;
					}
				},
				Failure);
			if (!Registered)
			{
				Error = Failure.Message.empty() ? Failure.Id() : std::move(Failure.Message);
			}
			return Registered;
		}
		bool Dispatch(Network::FContext& Context, IScriptCallInvoker& Invoker, std::size_t Limit, std::size_t& Count,
			std::string& Error) const override
		{
			Count = 0;
			if (!CheckScope(Invoker, Error))
			{
				return false;
			}
			if (ActiveInvoker)
			{
				Error = "Script dispatch requires the owner thread and cannot recurse";
				return false;
			}
			struct FScope
			{
				const FNetworkBindingService& Self;
				~FScope()
				{
					Self.ActiveInvoker = nullptr;
					Self.DispatchError = nullptr;
					Self.bDispatchFailed = false;
				}
			} Scope{*this};
			ActiveInvoker = &Invoker;
			DispatchError = &Error;
			Network::FError Failure;
			Count = Context.Dispatch(Failure, Limit);
			if (Failure && Error.empty())
			{
				Error = Failure.Message.empty() ? Failure.Id() : std::move(Failure.Message);
			}
			return !Failure && Error.empty();
		}

	private:
		bool CheckScope(const IScriptCallInvoker& Invoker, std::string& Error) const
		{
			if (Owner != std::this_thread::get_id())
			{
				Error = "Script completions require the context owner thread";
				return false;
			}
			if (!ScopeRoot.IsValid() || Invoker.GetProgramOwner() != Program ||
				Invoker.GetProgramGeneration() != Generation)
			{
				Error = "Network context belongs to a different or retired script program generation";
				return false;
			}
			return true;
		}
		FState& State;
		const std::thread::id Owner = std::this_thread::get_id();
		std::shared_ptr<const void> Program;
		std::uint64_t Generation = 0;
		FValueRoot ScopeRoot;
		mutable IScriptCallInvoker* ActiveInvoker = nullptr; // Borrowed exclusively inside Dispatch.
		mutable std::string* DispatchError = nullptr;
		mutable bool bDispatchFailed = false;
	};
	std::shared_ptr<const INetworkBindingService> MakeNetworkBindingService(
		FState& S, const std::shared_ptr<Network::FContext>& Context, IScriptCallInvoker& Invoker, std::string& Error)
	{
		auto Service = std::make_shared<FNetworkBindingService>(S);
		return Service->Initialize(Context, Invoker, Error) ? std::move(Service) : nullptr;
	}
} // namespace VeroScript::NetworkBinding
