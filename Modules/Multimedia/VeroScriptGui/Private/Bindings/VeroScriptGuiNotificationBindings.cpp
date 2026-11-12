// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScriptInternal.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace VeroScript::Gui::ScriptBindings
{
	struct FBindingWatch;
	struct FBindingSignal
	{
		bool HasSubscribers();

		std::weak_ptr<FBridge> Owner;
		std::vector<std::weak_ptr<FBindingWatch>> Subscribers;
		std::int64_t Revision = 1;
		bool bQueued = false;
	};

	struct FBindingWatch
	{
		std::weak_ptr<FWidget> Widget;
		std::vector<std::shared_ptr<FBindingSignal>> Sources;
		std::int64_t Id = 0;
		bool bConnected = true;
		bool bCanSuspend = false;
	};

	bool FBindingSignal::HasSubscribers()
	{
		while (!Subscribers.empty())
		{
			const auto Watch = Subscribers.back().lock();
			const auto Widget = Watch ? Watch->Widget.lock() : nullptr;
			if (Watch && Watch->bConnected && Widget && !Widget->IsDisposed()) return true;
			Subscribers.pop_back();
		}
		return false;
	}

	bool PublishBindingChanges(FBridge& Bridge, std::string& Error)
	{
		if (Bridge.BindingBatchDepth != 0)
		{
			Error = "GUI updates cannot run inside a notification batch";
			return false;
		}
		if (Bridge.BindingSources.empty()) return true;
		std::unordered_set<std::uint64_t> Affected;
		std::size_t Index = 0;
		for (; Index < Bridge.BindingSources.size(); ++Index)
		{
			const auto Source = Bridge.BindingSources[Index].lock();
			if (!Source)
			{
				continue;
			}
			auto& Subscribers = Source->Subscribers;
			Subscribers.erase(std::remove_if(Subscribers.begin(), Subscribers.end(),
			                      [](const auto& Value)
			{
				const auto Watch = Value.lock();
				return !Watch || !Watch->bConnected || Watch->Widget.expired();
			}),
			    Subscribers.end());
			for (const auto& Weak : Subscribers)
			{
				const auto Watch = Weak.lock();
				const auto Widget = Watch ? Watch->Widget.lock() : nullptr;
				if (!Widget || Widget->IsDisposed() || !Affected.insert(Widget->GetIdentity()).second)
				{
					continue;
				}
				// Reuse the existing lifecycle retry and immutable-source machinery.
				// No script callback or property setter runs in this queue traversal.
				const auto Found = Bridge.Lifecycles.find(Widget->GetIdentity());
				if (Found != Bridge.Lifecycles.end())
				{
					if (const auto State = Found->second.lock())
					{
						State->bPolling = true;
						// A notification changes getter inputs, not the widget's
						// attribute configurations. Bound readers still run in the
						// new epoch; keep valid configuration snapshots for fixed slots.
						// The attribute reader decides whether the value changed.
						// Waking script work must not force an unchanged scene to reflow.
						continue;
					}
				}
				if (!Widget->Invalidate(Error))
				{
					Bridge.BindingSources.erase(Bridge.BindingSources.begin(), Bridge.BindingSources.begin() + Index);
					return false;
				}
			}
			Source->bQueued = false;
		}
		Bridge.BindingSources.clear();
		return true;
	}

	bool RegisterNotificationBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
	    std::string& Error)
	{
		const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		const auto Bool = FNativeTypeDesc::Scalar(ENativeType::Bool);
		if (!Member(Registry, Bridge->Types[10], "__NativeSignal", EHostMemberKind::Constructor,
		        FNativeTypeDesc::Host(TypeNames[10]), {},
		        [Bridge](const auto& Call, auto& Out, auto&)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			auto Signal = std::make_shared<FBindingSignal>();
			Signal->Owner = Bridge;
			Signal->Revision = Bridge->BindingRevision;
			Out = Make(Bridge, 10, std::move(Signal));
			return true;
		}, Error) ||
		    !Member(Registry, Bridge->Types[10], "GetRevision", EHostMemberKind::Method, Int64, {},
		        [Bridge](const auto& Call, auto& Out, auto&)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			Out = (*static_cast<const std::shared_ptr<FBindingSignal>*>(Call.HostThis))->Revision;
			return true;
		}, Error, true) ||
		    !Member(Registry, Bridge->Types[10], "Notify", EHostMemberKind::Method, Int64, {},
		        [Bridge](const auto& Call, auto& Out, auto& Failure)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto& Signal = *static_cast<const std::shared_ptr<FBindingSignal>*>(Call.HostThis);
			if (Signal->Owner.lock() != Bridge || Bridge->BindingRevision == std::numeric_limits<std::int64_t>::max())
			{
				Failure = "gui.invalid_script_value";
				return false;
			}
			// Retire dead suffixes in amortized linear time, including programs
			// preparing models before any GUI update. No consumers need no queue.
			while (!Bridge->BindingSources.empty())
			{
				const auto Pending = Bridge->BindingSources.back().lock();
				if (Pending && Pending->HasSubscribers())
				{
					break;
				}
				if (Pending)
				{
					Pending->bQueued = false;
				}
				Bridge->BindingSources.pop_back();
			}
			if (!Signal->bQueued && Signal->HasSubscribers())
			{
				Bridge->BindingSources.emplace_back(Signal);
				Signal->bQueued = true;
			}
			Signal->Revision = ++Bridge->BindingRevision;
			Out = Signal->Revision;
			return true;
		}, Error))
		{
			return false;
		}
		if (!Member(Registry, Bridge->Types[10], "ReadRevision", EHostMemberKind::StaticMethod, Int64,
		        {FNativeTypeDesc::ArrayOf(TypeNames[10])},
		        [Bridge](const auto& Call, auto& Out, auto& Failure)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto& Sources = std::get<FDynamicArrayHandle>(Call.Args[0]);
			if (!Sources)
			{
				Failure = "gui.invalid_script_value";
				return false;
			}
			// A globally monotonic stamp makes max(revisions) an exact dependency
			// revision, without hashes, overflow arithmetic or per-read script arrays.
			std::int64_t Revision = 0;
			for (const auto& Value : Sources->Elements)
			{
				const auto& Source = Payload<std::shared_ptr<FBindingSignal>>(Value);
				if (Source->Owner.lock() != Bridge)
				{
					Failure = "gui.invalid_script_value";
					return false;
				}
				Revision = std::max(Revision, Source->Revision);
			}
			Out = Revision;
			return true;
		}, Error, true))
		{
			return false;
		}

		if (!Member(Registry, Bridge->Types[11], "__NativeWatch", EHostMemberKind::Constructor,
		        FNativeTypeDesc::Host(TypeNames[11]),
		        {FNativeTypeDesc::Host(TypeNames[0]), FNativeTypeDesc::ArrayOf(TypeNames[10]), Bool},
		        [Bridge](const auto& Call, auto& Out, auto& Failure)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			if (Bridge->NextBindingWatch == std::numeric_limits<std::int64_t>::max())
			{
				Failure = "gui.invalid_script_value";
				return false;
			}
			auto Watch = std::make_shared<FBindingWatch>();
			Watch->Widget = Payload<FWidgetView>(Call.Args[0]).Weak;
			Watch->Id = Bridge->NextBindingWatch++;
			Watch->bCanSuspend = std::get<bool>(Call.Args[2]);
			const auto& Sources = std::get<FDynamicArrayHandle>(Call.Args[1]);
			if (!Sources || Watch->Widget.expired())
			{
				Failure = "gui.invalid_script_value";
				return false;
			}
			Watch->Sources.reserve(Sources->Elements.size());
			for (const auto& Value : Sources->Elements)
			{
				const auto& Source = Payload<std::shared_ptr<FBindingSignal>>(Value);
				if (Source->Owner.lock() != Bridge)
				{
					Failure = "gui.invalid_script_value";
					return false;
				}
				Watch->Sources.push_back(Source);
			}
			for (const auto& Source : Watch->Sources)
			{
				auto& Subscribers = Source->Subscribers;
				if (Subscribers.size() % 64 == 0)
				{
					Subscribers.erase(std::remove_if(Subscribers.begin(), Subscribers.end(),
					                      [](const auto& Value)
					{
						const auto Watch = Value.lock();
						return !Watch || !Watch->bConnected;
					}),
					    Subscribers.end());
				}
				Subscribers.emplace_back(Watch);
			}
			Out = Make(Bridge, 11, std::move(Watch));
			return true;
		}, Error) ||
		    !Member(Registry, Bridge->Types[11], "GetId", EHostMemberKind::Method, Int64, {},
		        [](const auto& Call, auto& Out, auto&)
		{
			Out = (*static_cast<const std::shared_ptr<FBindingWatch>*>(Call.HostThis))->Id;
			return true;
		}, Error, true) ||
		    !Member(Registry, Bridge->Types[11], "AllowsSuspension", EHostMemberKind::Method, Bool, {},
		        [](const auto& Call, auto& Out, auto&)
		{
			Out = (*static_cast<const std::shared_ptr<FBindingWatch>*>(Call.HostThis))->bCanSuspend;
			return true;
		}, Error, true) ||
		    !Member(Registry, Bridge->Types[11], "Disconnect", EHostMemberKind::Method, Void, {},
		        [Bridge](const auto& Call, auto&, auto&)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto& Watch = *static_cast<const std::shared_ptr<FBindingWatch>*>(Call.HostThis);
			Watch->bConnected = false;
			Watch->Sources.clear();
			return true;
		}, Error))
		{
			return false;
		}
		for (const bool Begin : {true, false})
		{
			if (!Member(Registry, Bridge->Types[10], Begin ? "BeginBatch" : "EndBatch", EHostMemberKind::StaticMethod,
			        Void, {}, [Bridge, Begin](const auto& Call, auto&, auto& Failure)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				if (Begin && Bridge->BindingBatchDepth != std::numeric_limits<unsigned>::max())
				{
					++Bridge->BindingBatchDepth;
					return true;
				}
				if (!Begin && Bridge->BindingBatchDepth != 0)
				{
					--Bridge->BindingBatchDepth;
					return true;
				}
				Failure = "gui.invalid_script_value";
				return false;
			}, Error))
			{
				return false;
			}
		}
		return Member(Registry, Bridge->Types[10], "GetBatchDepth", EHostMemberKind::StaticMethod, Int64, {},
		           [Bridge](const auto& Call, auto& Out, auto&)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			Out = static_cast<std::int64_t>(Bridge->BindingBatchDepth);
			return true;
		}, Error, true) &&
		    Member(Registry, Bridge->Types[10], "RestoreBatchDepth", EHostMemberKind::StaticMethod, Void, {Int64},
		        [Bridge](const auto& Call, auto&, auto& Failure)
		{
			FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
			const auto Depth = std::get<std::int64_t>(Call.Args[0]);
			if (Depth < 0 || static_cast<std::uint64_t>(Depth) > Bridge->BindingBatchDepth)
			{
				Failure = "gui.invalid_script_value";
				return false;
			}
			Bridge->BindingBatchDepth = static_cast<unsigned>(Depth);
			return true;
		}, Error);
	}
} // namespace VeroScript::Gui::ScriptBindings
