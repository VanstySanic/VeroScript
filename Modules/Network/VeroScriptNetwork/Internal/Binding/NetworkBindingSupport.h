// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include "VeroScriptNetworkBackend.h"
#include "VeroScriptNetworkProviders.h"
#include "VeroScriptNetworkStream.h"
#include "VeroScriptNetworkFiles.h"
#include "VeroScriptNetworkUpgrade.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptMemory.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace VeroScript::NetworkBinding
{
	struct FReadStreamValue
	{
		FHostObjectHandle Context;
		std::shared_ptr<Network::FReadableStream> Value;
		Network::FStreamResult Last;
	};
	struct FWriteStreamValue
	{
		FHostObjectHandle Context;
		std::shared_ptr<Network::FWritableStream> Value;
		Network::FStreamResult Last;
	};
	struct FUpgradeSourceValue
	{
		FHostObjectHandle Context;
		std::shared_ptr<Network::FUpgradeSource> Value;
	};
	// FState below owns callbacks of this type.
	using FCall = std::function<bool(const FNativeCallContext&, FValue&, std::string&)>;
	class INetworkBindingService : public INativeExtensionService
	{
	public:
		virtual bool MakeReadable(
			const FHostObjectHandle&, std::shared_ptr<Network::FReadableStream>, FValue&, std::string&) const = 0;
		virtual bool MakeFileProgress(Network::FFileProgress, FValue&, std::string&) const = 0;
		virtual bool MakeUpgradeSource(
			const FHostObjectHandle&, std::shared_ptr<Network::FUpgradeSource>, FValue&, std::string&) const = 0;
		virtual bool AttachCompletion(const FHostObjectHandle&, const Network::FOperation&, const FValue&,
			const FHostObjectHandle& Receiver, IScriptCallInvoker&, std::string&) const = 0;
		virtual bool Dispatch(Network::FContext&, IScriptCallInvoker&, std::size_t, std::size_t&, std::string&) const = 0;
	};

	struct FContextValue
	{
		std::shared_ptr<Network::FContext> Value;
		// Destroy the service while Value still retains the Network registration/code owner.
		std::shared_ptr<const INetworkBindingService> Services;
	};

	template <class T>
	struct FHandle
	{
		FHostObjectHandle Context;
		std::shared_ptr<T> Value;
	};

	struct FOperationValue
	{
		FHostObjectHandle Context;
		Network::FOperation Value;
	};

	struct FState
	{
		const char* DiagnosticId = "network.binding";
		INativeExtensionHost* Host = nullptr;
		std::shared_ptr<const Network::IBackendCatalog> Backends;
		std::weak_ptr<const void> CodeOwner;
		std::vector<std::unique_ptr<FCall>> Calls;
	};

	template <class T>
	std::shared_ptr<T> BackendService(FState& State, std::string_view Service, std::string_view Backend,
		std::string_view Contract, std::string& Error)
	{
		if (!State.Backends)
		{
			Error = "network.backend_catalog_unavailable: the embedding host must provide its registered backend catalog";
			return {};
		}
		auto Value = State.Backends->Resolve(Service, Backend, Contract, "0.1.1", Error);
		if (!Value) return {};
		auto* Pointer = static_cast<T*>(Value.get());
		return std::shared_ptr<T>(std::move(Value), Pointer);
	}

	inline FNativeCallContext Explicit(FNativeCallContext C)
	{
		if (C.HostThis)
		{
			++C.Args;
			if (C.MutableArgs)
			{
				++C.MutableArgs;
			}
			--C.ArgCount;
		}
		return C;
	}

	template <class T>
	T& This(const FNativeCallContext& C)
	{
		if (!C.HostThis)
		{
			throw std::invalid_argument("Network object is null");
		}
		return *static_cast<T*>(C.HostThis);
	}
	inline const FHostObjectHandle& HostArgument(const FNativeCallContext& C, std::size_t I)
	{
		if (!C.Args || I >= C.ArgCount)
		{
			throw std::invalid_argument("Network object argument is missing");
		}
		const auto* H = std::get_if<FHostObjectHandle>(&C.Args[I]);
		if (!H || !*H || !(*H)->Payload)
		{
			throw std::invalid_argument("Network object argument is null");
		}
		return *H;
	}
	template <class T>
	T& Object(const FNativeCallContext& C, std::size_t I)
	{
		return *static_cast<T*>(HostArgument(C, I)->Payload);
	}
	inline FHostObjectHandle ContextArg(const FNativeCallContext& C, std::size_t I = 0)
	{
		return HostArgument(C, I);
	}
	inline Network::FContext& Context(const FHostObjectHandle& C)
	{
		if (!C || !C->Payload || !static_cast<FContextValue*>(C->Payload)->Value)
		{
			throw std::invalid_argument("Network context is null");
		}
		return *static_cast<FContextValue*>(C->Payload)->Value;
	}
	inline std::int64_t Integer(const FNativeCallContext& C, std::size_t I)
	{
		return std::get<std::int64_t>(C.Args[I]);
	}
	inline const std::string& String(const FNativeCallContext& C, std::size_t I)
	{
		return std::get<std::string>(C.Args[I]);
	}
	inline std::uint16_t Port(std::int64_t Value)
	{
		if (Value < 0 || Value > 65535)
		{
			throw std::invalid_argument("Port is out of range");
		}
		return static_cast<std::uint16_t>(Value);
	}
	inline std::uint64_t Count(std::int64_t Value)
	{
		if (Value < 0)
		{
			throw std::invalid_argument("Count must be nonnegative");
		}
		return static_cast<std::uint64_t>(Value);
	}
	inline Network::FOperationOptions Options(std::int64_t Milliseconds)
	{
		if (Milliseconds < -1 || Milliseconds > 365LL * 24 * 60 * 60 * 1000)
		{
			throw std::invalid_argument("Timeout must be -1 or between zero and 365 days");
		}
		Network::FOperationOptions O;
		O.Timeout = Milliseconds == -1 ? std::nullopt : std::optional<std::chrono::milliseconds>(Milliseconds);
		return O;
	}
	inline std::string BytesText(const Network::FBytes& Bytes)
	{
		return std::string(Bytes.begin(), Bytes.end());
	}
	inline Network::FBytes Bytes(const std::string& Text)
	{
		return Network::FBytes(Text.begin(), Text.end());
	}
	inline bool CheckWork(const FNativeCallContext& C, std::uint64_t Units, std::string& Error)
	{
		auto W = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
		if (W.Check && !W.Check(W.UserData, Units))
		{
			Error = "Network operation cancelled";
			return false;
		}
		return true;
	}

	inline bool Invoke(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		try
		{
			return (*static_cast<const FCall*>(C.UserData))(Explicit(C), Out, Error);
		}
		catch (const std::exception& E)
		{
			Error = E.what();
			return false;
		}
		catch (...)
		{
			Error = "Network native callback failed";
			return false;
		}
	}
	inline bool InvokeClone(const FNativeCallContext& C, FValue& Out, std::string& Error)
	{
		try
		{
			return (*static_cast<const FCall*>(C.UserData))(C, Out, Error);
		}
		catch (const std::exception& E)
		{
			Error = E.what();
			return false;
		}
		catch (...)
		{
			Error = "Network value clone failed";
			return false;
		}
	}

	template <class T>
	bool Make(FState& S, const char* Name, T Value, FValue& Out, std::string& Error)
	{
		auto P = std::make_unique<T>(std::move(Value));
		auto O = S.Host->MakeHostObjectNamed(
			Name,
			P.get(),
			[](void* V)
			{
				delete static_cast<T*>(V);
			},
			Error);
		if (!O)
		{
			return false;
		}
		P.release();
		Out = std::move(O);
		return true;
	}

	inline bool Type(FState& S, const char* Name, FHostTypeId& Id, std::string& Error)
	{
		FHostTypeDesc D;
		D.Name = Name;
		D.Kind = EHostTypeKind::Class;
		return S.Host->RegisterType(std::move(D), Id, Error);
	}

	template <class T>
	bool ValueType(FState& S, const char* Name, FHostTypeId& Id, std::string& Error)
	{
		auto Clone = std::make_unique<FCall>(
			[&S, Name](const auto& C, auto& O, auto& E)
			{
				return Make(S, Name, Object<T>(C, 0), O, E);
			});
		FHostTypeDesc D;
		D.Name = Name;
		D.Kind = EHostTypeKind::Struct;
		D.CloneCallback = InvokeClone;
		D.CloneUserData = Clone.get();
		if (!S.Host->RegisterType(std::move(D), Id, Error))
		{
			return false;
		}
		S.Calls.push_back(std::move(Clone));
		return true;
	}

	inline bool Member(FState& S, FHostTypeId Id, const char* Name, ENativeType Return,
		std::vector<ENativeType> Parameters, FCall Call, std::string& Error, const char* ReturnName = "",
		std::vector<std::string> ParameterNames = {}, bool bStatic = false, bool bMutable = false)
	{
		auto Owned = std::make_unique<FCall>(
			[Call = std::move(Call), DiagnosticId = S.DiagnosticId](
				const FNativeCallContext& C, FValue& Out, std::string& Error)
			{
				try
				{
					if (Call(C, Out, Error))
					{
						return true;
					}
				}
				catch (const std::exception& Exception)
				{
					Error = Exception.what();
				}
				catch (...)
				{
					Error = "Network native callback failed";
				}
				if (C.DiagnosticOutput && C.DiagnosticOutput->MessageId.empty())
				{
					try
					{
						FScriptError Value;
						Value.MessageId = DiagnosticId;
						Value.Message = Error;
						Value.MessageArgs = {Error};
						*C.DiagnosticOutput = std::move(Value);
					}
					catch (...)
					{
					} // Preserve the primary error if display metadata cannot allocate.
				}
				return false;
			});
		FHostMemberDesc D;
		D.Name = Name;
		D.Kind = bStatic ? EHostMemberKind::StaticMethod : EHostMemberKind::Method;
		D.ReturnType = Return;
		D.ReturnHostTypeName = ReturnName;
		D.ParamTypes = std::move(Parameters);
		D.ParamHostTypeNames = std::move(ParameterNames);
		D.Callback = Invoke;
		D.UserData = Owned.get();
		D.bMutableThis = bMutable;
		D.bConstMethod = !bMutable;
		if (!S.Host->RegisterMember(Id, std::move(D), Error))
		{
			return false;
		}
		S.Calls.push_back(std::move(Owned));
		return true;
	}

	inline bool Lease(FState& S, const FHostObjectHandle& C, std::string& Error)
	{
		Network::FError E;
		if (!Context(C).RetainCodeOwner(S.CodeOwner.lock(), E))
		{
			Error = E.Message;
			return false;
		}
		return true;
	}

	struct FOperationAdmission
	{
		Network::FOperation Operation;
		Network::FContext& Context;
		bool Committed = false;
		~FOperationAdmission()
		{
			if (!Committed)
			{
				try
				{
					Operation.Cancel();
					if (Context.IsClosed())
					{
						Context.Shutdown();
					}
				}
				catch (...)
				{
					try
					{
						Context.Shutdown();
					}
					catch (...)
					{
					}
				}
			}
		}
	};
	template <class F>
	bool Start(
		FState& S, const char* OperationType, const FHostObjectHandle& C, F&& Function, FValue& Out, std::string& Error)
	{
		if (!Lease(S, C, Error))
		{
			return false;
		}
		auto Operation = Function(Context(C));
		FOperationAdmission Guard{Operation, Context(C)};
		Guard.Committed = Make(S, OperationType, FOperationValue{C, std::move(Operation)}, Out, Error);
		return Guard.Committed;
	}

	template <class T>
	std::shared_ptr<const T> Result(const FOperationValue& O)
	{
		if (!O.Value.IsComplete())
		{
			throw std::logic_error("Operation is not complete");
		}
		if (O.Value.Error())
		{
			throw std::runtime_error(O.Value.Error().Message);
		}
		auto R = O.Value.Result<T>();
		if (!R)
		{
			throw std::logic_error("Operation result has a different type");
		}
		return R;
	}

	inline bool OperationMembers(FState& S, FHostTypeId Id, std::string& Error, const char* OperationName)
	{
		using T = ENativeType;
		return Member(
				   S,
				   Id,
				   "on_complete",
				   T::Void,
				   {T::Function},
				   [&S, Name = std::string(OperationName)](const auto& C, auto&, auto& E)
				   {
					   const auto& Operation = This<FOperationValue>(C);
					   if (!C.ScriptInvoker || !Operation.Context || !Operation.Context->Payload)
					   {
						   E = "Completion registration requires an active script context";
						   return false;
					   }
					   auto& Owner = *static_cast<FContextValue*>(Operation.Context->Payload);
					   if (!Owner.Services)
					   {
						   E = "Network context has no binding service";
						   return false;
					   }
					   FValue Prepared;
					   if (!Make(S, Name.c_str(), FOperationValue{}, Prepared, E))
					   {
						   return false;
					   }
					   return Owner.Services->AttachCompletion(Operation.Context,
						   Operation.Value,
						   C.Args[0],
						   std::get<FHostObjectHandle>(Prepared),
						   *C.ScriptInvoker,
						   E);
				   },
				   Error,
				   "",
				   {"fn(" + std::string(OperationName) + ")->void"},
				   false,
				   true) &&
			   Member(
				   S,
				   Id,
				   "done",
				   T::Bool,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = This<FOperationValue>(C).Value.IsComplete();
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "state",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = static_cast<std::int64_t>(This<FOperationValue>(C).Value.State());
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "cancel",
				   T::Bool,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = This<FOperationValue>(C).Value.Cancel();
					   return true;
				   },
				   Error,
				   "",
				   {},
				   false,
				   true) &&
			   Member(
				   S,
				   Id,
				   "error",
				   T::String,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = This<FOperationValue>(C).Value.Error().Message;
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "message_id",
				   T::String,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = std::string(This<FOperationValue>(C).Value.Error().Id());
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "error_code",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   O = static_cast<std::int64_t>(This<FOperationValue>(C).Value.Error().Code);
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "count",
				   T::Int64,
				   {},
				   [](const auto& C, auto& O, auto&)
				   {
					   auto N = *Result<std::uint64_t>(This<FOperationValue>(C));
					   if (N > INT64_MAX)
					   {
						   throw std::range_error("Result count exceeds int64");
					   }
					   O = static_cast<std::int64_t>(N);
					   return true;
				   },
				   Error) &&
			   Member(
				   S,
				   Id,
				   "wait",
				   T::Int64,
				   {T::Int64},
				   [](const auto& C, auto& O, auto& E)
				   {
					   const auto Limit = Integer(C, 0);
					   if (Limit < 0 || Limit > 365LL * 24 * 60 * 60 * 1000)
					   {
						   throw std::invalid_argument("Invalid observation timeout");
					   }
					   auto& Op = This<FOperationValue>(C).Value;
					   const auto End = std::chrono::steady_clock::now() + std::chrono::milliseconds(Limit);
					   auto W = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
					   for (;;)
					   {
						   if (W.Check && !W.Check(W.UserData, 1))
						   {
							   E = "Network wait cancelled";
							   return false;
						   }
						   auto Remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
							   End - std::chrono::steady_clock::now());
						   auto R = Op.Wait(std::max(
							   std::chrono::milliseconds(0), std::min(Remaining, std::chrono::milliseconds(10))));
						   if (R != Network::EWaitResult::WaitTimedOut || std::chrono::steady_clock::now() >= End)
						   {
							   O = static_cast<std::int64_t>(R);
							   return true;
						   }
					   }
				   },
				   Error);
	}
} // namespace VeroScript::NetworkBinding
