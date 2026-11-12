// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiEventBindings.h"
#include "Compilation/VeroScriptGuiMarkupInternal.h"

namespace VeroScript::Gui::ScriptBindings
{
	bool CheckScriptProgram(const FBridge& Bridge, const std::weak_ptr<const void>& Owner, std::uint64_t Generation,
							std::string& Error)
	{
		const auto Program = Owner.lock();
		if (Program && Generation &&
			(Bridge.Invoker
				 ? (Bridge.Invoker->GetProgramOwner() == Program &&
					Bridge.Invoker->GetProgramGeneration() == Generation)
				 : (Bridge.Services.IsCurrentProgram && Bridge.Services.IsCurrentProgram(Owner, Generation))))
		{
			return true;
		}
		Error = "gui.stale_script_callback";
		return false;
	}
	bool DispatchScriptEvent(const std::shared_ptr<FBridge>& Bridge, std::int64_t Connection, FEvent& Event,
							 std::string& Error)
	{
		if (Bridge->Thread != std::this_thread::get_id())
		{
			Error = "gui.wrong_thread";
			return false;
		}
		auto Snapshot = std::make_shared<FScriptEvent>();
		Snapshot->Input = Event.Input;
		Snapshot->Phase = Event.Phase;
		Snapshot->Target = Event.Target;
		Snapshot->Sender = Event.CurrentTarget;
		Snapshot->Active = &Event;
		struct FActiveScope
		{
			FScriptEvent& Snapshot;
			~FActiveScope()
			{
				Snapshot.bDefaultPrevented = Snapshot.Active && Snapshot.Active->bDefaultPrevented;
				Snapshot.Active = nullptr;
			}
		} Guard{*Snapshot};
		std::vector<FValue> Arguments{Make(Bridge, 0, std::weak_ptr<FWidget>(Event.CurrentTarget)),
									  Make(Bridge, 5, Snapshot), Connection};
		FValue Ignored;
		constexpr const char* Function = "VS.Gui.Internal.DispatchEvent";
		if (Bridge->Invoker)
		{
			return Bridge->Invoker->InvokeFunction(Function, Arguments, Ignored, Error);
		}
		if (Bridge->Services.Invoke)
		{
			return Bridge->Services.Invoke(Function, Arguments, Ignored, Error);
		}
		Error = "gui.script_execution_bridge_unavailable";
		return false;
	}

	bool RegisterWidgetEvents(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
							  std::string& Error)
	{
		if (!Member(
				Registry, Bridge->Types[0], "Subscribe", EHostMemberKind::Method,
				FNativeTypeDesc::Scalar(ENativeType::Int64),
				{FNativeTypeDesc::Scalar(ENativeType::String), FNativeTypeDesc::Scalar(ENativeType::Bool)},
				[Bridge](const auto& Call, auto& Out, auto& Error)
				{
					FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
					Platform::EEventType Type;
					if (!FindMarkupEvent(std::get<std::string>(Call.Args[1]), Type))
					{
						Error = "gui.unknown_event";
						return false;
					}
					const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
					if (!Widget)
					{
						Error = "gui.widget_expired";
						return false;
					}
					for (unsigned Index = 0; Index < 8 && !Bridge->Subscriptions.empty(); ++Index)
					{
						auto Item = Bridge->Subscriptions.upper_bound(Bridge->SubscriptionSweep);
						if (Item == Bridge->Subscriptions.end())
						{
							Item = Bridge->Subscriptions.begin();
						}
						Bridge->SubscriptionSweep = Item->first;
						if (!Item->second.IsConnected())
						{
							Bridge->Subscriptions.erase(Item);
						}
					}
					if (Bridge->NextSubscription == INT64_MAX)
					{
						Error = "gui.subscription_exhausted";
						return false;
					}
					const auto Id = Bridge->NextSubscription++;
					if (!Call.ScriptInvoker)
					{
						Error = "gui.script_invoker_missing";
						return false;
					}
					const std::weak_ptr<const void> Program = Call.ScriptInvoker->GetProgramOwner();
					const auto Generation = Call.ScriptInvoker->GetProgramGeneration();
					auto Token = Widget->Subscribe(
						Type, std::get<bool>(Call.Args[2]),
						[Bridge, Id, Program, Generation](FEvent& Event)
						{
							std::string Failure;
							if (!CheckScriptProgram(*Bridge, Program, Generation, Failure))
							{
								throw std::runtime_error(Failure);
							}
							if (!DispatchScriptEvent(Bridge, Id, Event, Failure))
							{
								throw std::runtime_error(Failure);
							}
						},
						Error);
					if (!Token.IsConnected())
					{
						return false;
					}
					try
					{
						Bridge->Subscriptions.emplace(Id, Token);
					}
					catch (...)
					{
						std::string Ignored;
						Token.Disconnect(Ignored);
						throw;
					}
					Out = Id;
					return true;
				},
				Error) ||
			!Member(
				Registry, Bridge->Types[0], "Disconnect", EHostMemberKind::Method,
				FNativeTypeDesc::Scalar(ENativeType::Void), {FNativeTypeDesc::Scalar(ENativeType::Int64)},
				[Bridge](const auto& Call, auto&, auto& Error)
				{
					FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
					const auto Found = Bridge->Subscriptions.find(std::get<std::int64_t>(Call.Args[1]));
					if (Found == Bridge->Subscriptions.end())
					{
						return true;
					}
					if (!Found->second.Disconnect(Error))
					{
						return false;
					}
					Bridge->Subscriptions.erase(Found);
					return true;
				},
				Error))
		{
			return false;
		}
		if (!Member(
				Registry, Bridge->Types[0], "IsConnected", EHostMemberKind::Method,
				FNativeTypeDesc::Scalar(ENativeType::Bool), {FNativeTypeDesc::Scalar(ENativeType::Int64)},
				[Bridge](const auto& Call, auto& Out, auto&)
				{
					const auto Found = Bridge->Subscriptions.find(std::get<std::int64_t>(Call.Args[1]));
					Out = Found != Bridge->Subscriptions.end() && Found->second.IsConnected();
					return true;
				},
				Error))
		{
			return false;
		}
		for (const auto* Name : {"IsMounted", "IsDisposed", "IsInitialized", "Dispose"})
		{
			if (!Member(
					Registry, Bridge->Types[0], Name, EHostMemberKind::Method,
					FNativeTypeDesc::Scalar(ENativeType::Bool), {},
					[Bridge, Name = std::string(Name)](const auto& Call, auto& Out, auto& Error)
					{
						FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
						const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
						if (!Widget)
						{
							if (Name == "IsDisposed")
							{
								Out = true;
								return true;
							}
							Error = "gui.widget_expired";
							return false;
						}
						if (Name == "Dispose" && !Widget->Dispose(Error))
						{
							return false;
						}
						Out = Name == "IsMounted"		? Widget->IsMounted()
							  : Name == "IsInitialized" ? Widget->IsInitialized()
														: (Name == "IsDisposed" ? Widget->IsDisposed() : true);
						return true;
					},
					Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::Gui::ScriptBindings
