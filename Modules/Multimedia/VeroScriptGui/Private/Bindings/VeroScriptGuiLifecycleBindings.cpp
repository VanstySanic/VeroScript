// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiLifecycleBindings.h"
#include "VeroScriptGuiPaintBindings.h"
#include "VeroScriptValueRoot.h"
#include <algorithm>

namespace VeroScript::Gui::ScriptBindings
{
	namespace
	{
		const FObjectHandle* SourceFieldObject(const FValue& Value)
		{
			if (const auto* Object = std::get_if<FObjectHandle>(&Value)) return Object;
			const auto* Visible = UnwrapClosureReferenceCell(&Value);
			return Visible && Visible != &Value ? std::get_if<FObjectHandle>(Visible) : nullptr;
		}

		bool FlushBindingUpdates(FBridge& Bridge, std::string& Error)
		{
			return PublishBindingChanges(Bridge, Error);
		}

		class FBindingFrameService final : public IFrameBindingService
		{
		public:
			//~ Begin IFrameBindingService Interface
			bool BeginFrame(double, std::string&) override
			{
				return true;
			}
			bool FlushBindings(std::string& Error) override
			{
				// A callback can expose another session to this context. Snapshot the
				// weak owners so registration cannot invalidate the current iteration.
				const auto Snapshot = Owners;
				for (const auto& Owner : Snapshot)
				{
					if (const auto Bridge = Owner.lock(); Bridge && !FlushBindingUpdates(*Bridge, Error))
					{
						return false;
					}
				}
				Owners.erase(std::remove_if(Owners.begin(), Owners.end(),
				                 [](const auto& Owner)
				{
					return Owner.expired();
				}),
				    Owners.end());
				return true;
			}
			//~ End IFrameBindingService Interface

			void Add(const std::shared_ptr<FBridge>& Bridge)
			{
				for (const auto& Owner : Owners)
				{
					if (Owner.lock() == Bridge) return;
				}
				Owners.emplace_back(Bridge);
			}

		private:
			std::vector<std::weak_ptr<FBridge>> Owners;
		};
	}

	bool AttachBindingUpdates(const std::shared_ptr<FBridge>& Bridge, const std::shared_ptr<FContext>& Context,
	    std::string& Error)
	{
		if (!Context || !Context->IsOpen()) return true;
		constexpr const char* Id = "vs.gui.binding-updates";
		const auto Existing = Context->GetFrameService(Id, Error);
		if (!Error.empty()) return false;
		auto Service = std::dynamic_pointer_cast<FBindingFrameService>(Existing);
		if (Existing && !Service)
		{
			Error = "gui.invalid_frame_service_operation";
			return false;
		}
		if (!Service)
		{
			Service = std::make_shared<FBindingFrameService>();
			if (!Context->SetFrameService(Id, Service, Error)) return false;
		}
		Service->Add(Bridge);
		return true;
	}

	bool FScriptLifecycle::SourcesChanged(const FWidget& Widget, bool& bScriptChanged) const
	{
		bScriptChanged = false;
		if (!bObserveSources) return false;
		const auto Object = ScriptOwner.lock();
		if (!Object) return false;
		bScriptChanged = !bSourceSnapshot || Object->MutationVersion != ScriptVersion || Object->Fields.size() != FieldCount;
		if (!bScriptChanged)
		{
			for (const auto& Field : Fields)
			{
				const auto* Value = SourceFieldObject(Object->Fields[Field.Index]);
				if (!Value || !*Value || Value->get() != Field.Identity || (*Value)->MutationVersion != Field.Version)
				{
					bScriptChanged = true;
					break;
				}
			}
		}
		// Layout and control output can change without replacing a script source.
		// They wake suspended widgets but do not invalidate fixed attribute reads.
		return bScriptChanged || !Widget.GetMutationToken() || !Widget.GetStyleMutationToken() ||
		    Widget.GetMutationToken() != NativeVersion || Widget.GetStyleMutationToken() != StyleVersion;
	}

	void FScriptLifecycle::CaptureFields(std::vector<FFieldVersion>& Output) const
	{
		Output.clear();
		const auto Object = ScriptOwner.lock();
		if (!bObserveSources || !Object) return;
		// Attribute configurations live in direct struct fields. Observing their
		// revisions also catches raw default .set/.bind without retaining payloads.
		// Reference captures can wrap those fields in Core-owned closure cells.
		for (std::size_t Index = 0; Index < Object->Fields.size(); ++Index)
		{
			const auto* Value = SourceFieldObject(Object->Fields[Index]);
			if (Value && *Value && (*Value)->Kind == EScriptObjectKind::Struct)
			{
				Output.push_back({Index, Value->get(), (*Value)->MutationVersion});
			}
		}
	}

	void FScriptLifecycle::CaptureSources(const FWidget& Widget, bool bFieldsCaptured)
	{
		bSourceSnapshot = false;
		const auto Object = ScriptOwner.lock();
		if (!bObserveSources || !Object) return;
		ScriptVersion = Object->MutationVersion;
		FieldCount = Object->Fields.size();
		NativeVersion = Widget.GetMutationToken();
		StyleVersion = Widget.GetStyleMutationToken();
		if (!bFieldsCaptured)
		{
			CaptureFields(Fields);
		}
		// CaptureFields emits indices in ascending order. Validate the range once
		// on commit so suspended widgets need no per-field bounds checks.
		if (!Fields.empty() && Fields.back().Index >= FieldCount)
		{
			return;
		}
		bSourceSnapshot = true;
	}

	bool ObserveScriptWidget(const std::shared_ptr<FBridge>& Bridge, const std::shared_ptr<FWidget>& Widget,
	    std::string& Error)
	{
		if (!Bridge->Invoker)
		{
			Error = "gui.script_invoker_missing";
			return false;
		}
		const std::weak_ptr<const void> Program = Bridge->Invoker->GetProgramOwner();
		const auto Generation = Bridge->Invoker->GetProgramGeneration();
		if (!Widget->SetPaintCallback(
		        [Owner = std::weak_ptr<FBridge>(Bridge), Target = std::weak_ptr<FWidget>(Widget), Program,
		            Generation](FPaintContext& Canvas, std::string& Failure)
		{
			const auto State = Owner.lock();
			if (!State || Target.expired())
			{
				return true;
			}
			if (!CheckScriptProgram(*State, Program, Generation, Failure))
			{
				return false;
			}
			auto Paint = std::make_shared<FScriptPaint>();
			Paint->Active = &Canvas;
			struct FReset
			{
				FScriptPaint& Paint;
				~FReset()
				{
					Paint.Active = nullptr;
				}
			} Reset{*Paint};
			std::vector<FValue> Arguments{Make(State, 0, Target), Make(State, 9, Paint)};
			FValue Ignored;
			if (State->Invoker)
			{
				return State->Invoker->InvokeFunction("VS.Gui.Internal.DispatchPaint", Arguments, Ignored, Failure);
			}
			if (State->Services.Invoke)
			{
				return State->Services.Invoke("VS.Gui.Internal.DispatchPaint", Arguments, Ignored, Failure);
			}
			Failure = "gui.script_execution_bridge_unavailable";
			return false;
		},
		        Error))
		{
			return false;
		}
		if (!Widget->SetControlCallback(
		        [Owner = std::weak_ptr<FBridge>(Bridge), Target = std::weak_ptr<FWidget>(Widget), Program,
		            Generation](const FControlChange& Change, std::string& Failure)
		{
			const auto State = Owner.lock();
			if (!State || Target.expired())
			{
				return true;
			}
			if (!CheckScriptProgram(*State, Program, Generation, Failure))
			{
				return false;
			}
			if (const auto Widget = Target.lock(); Widget && !Widget->IsDisposed())
			{
				const auto Found = State->Lifecycles.find(Widget->GetIdentity());
				const auto Lifecycle = Found == State->Lifecycles.end() ? nullptr : Found->second.lock();
				if (Lifecycle && !Lifecycle->ScriptOwner.expired() && Lifecycle->ControlAction.IsRegistered())
				{
					const auto* Stored = Lifecycle->ControlAction.Read();
					const auto* Function = Stored ? std::get_if<FFunctionHandle>(Stored) : nullptr;
					if (!Function || !*Function)
					{
						Failure = "gui.stale_script_callback";
						return false;
					}
					const auto Callback = *Function;
					std::vector<FValue> Values{static_cast<std::int32_t>(Change.Action), Change.bChecked,
					    Change.Value, Change.Text, Change.X, Change.Y};
					FValue Handled;
					bool Success;
					if (State->Invoker)
					{
						Success = State->Invoker->InvokeCallable(Callback, Values, Handled, Failure);
					}
					else if (State->Services.Invoke)
					{
						Values.insert(Values.begin(), Callback);
						Success = State->Services.Invoke("VS.Gui.Internal.DispatchControlCallback", Values, Handled, Failure);
					}
					else
					{
						Failure = "gui.script_execution_bridge_unavailable";
						return false;
					}
					if (!Success) return false;
					const auto* Dispatched = std::get_if<bool>(&Handled);
					if (!Dispatched)
					{
						Failure = "gui.invalid_script_value";
						return false;
					}
					if (*Dispatched) return true;
					// A collected script view is rehydrated by the existing dispatcher.
				}
			}
			std::vector<FValue> Arguments{Make(State, 0, Target),
			    static_cast<std::int32_t>(Change.Action),
			    Change.bChecked,
			    Change.Value,
			    Change.Text,
			    Change.X,
			    Change.Y};
			FValue Ignored;
			if (State->Invoker)
			{
				return State->Invoker->InvokeFunction("VS.Gui.Internal.DispatchControl", Arguments, Ignored,
				    Failure);
			}
			if (State->Services.Invoke)
			{
				return State->Services.Invoke("VS.Gui.Internal.DispatchControl", Arguments, Ignored, Failure);
			}
			Failure = "gui.script_execution_bridge_unavailable";
			return false;
		},
		        Error))
		{
			return false;
		}
		return Widget->SetLifecycleObserver(
		    [Owner = std::weak_ptr<FBridge>(Bridge), Target = std::weak_ptr<FWidget>(Widget), Program,
		        Generation](EWidgetLifecycle Stage, std::string& Failure)
		{
			const auto Bridge = Owner.lock();
			const auto Widget = Target.lock();
			if (!Bridge || !Widget)
			{
				return true;
			}
			if (!CheckScriptProgram(*Bridge, Program, Generation, Failure))
			{
				return false;
			}
			if (Stage == EWidgetLifecycle::Synchronize && !FlushBindingUpdates(*Bridge, Failure))
			{
				return false;
			}
			std::vector<FValue> Arguments{Make(Bridge, 0, Target), static_cast<std::int32_t>(Stage)};
			FValue Ignored;
			if (Bridge->Invoker)
			{
				return Bridge->Invoker->InvokeFunction("VS.Gui.Internal.DispatchLifecycle", Arguments, Ignored,
				    Failure);
			}
			if (Bridge->Services.Invoke)
			{
				return Bridge->Services.Invoke("VS.Gui.Internal.DispatchLifecycle", Arguments, Ignored, Failure);
			}
			Failure = "gui.script_execution_bridge_unavailable";
			return false;
		},
		    Error);
	}

	bool RegisterWidgetGeometry(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
	    std::string& Error)
	{
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		if (!RegisterNotificationBindings(Registry, Bridge, Error)) return false;

		if (!Member(
		        Registry, Bridge->Types[0], "IsScriptType", EHostMemberKind::StaticMethod,
		        FNativeTypeDesc::Scalar(ENativeType::Bool),
		        {FNativeTypeDesc::Function("fn()->void"), FNativeTypeDesc::Scalar(ENativeType::String)},
		        [](const auto& Call, auto& Out, auto&)
		{
			const auto Method = std::get<FFunctionHandle>(Call.Args[0]);
			Out = Method && Method->CapturedThis &&
			    Method->CapturedThis->GetTypeName() == std::get<std::string>(Call.Args[1]);
			return true;
		},
		        Error, true))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "RequestSynchronization", EHostMemberKind::Method,
		        FNativeTypeDesc::Scalar(ENativeType::Void), {},
		        [](const auto& Call, auto&, auto&)
		{
			const auto& View = *static_cast<const FWidgetView*>(Call.HostThis);
			if (View.Lifecycle)
			{
				View.Lifecycle->bPolling = true;
			}
			return true;
		},
		        Error))
		{
			return false;
		}
		// Borrowed views replace only callback metadata, including during paint.
		// Only newly created script widgets install a native scene observer.
		if (!Member(Registry, Bridge->Types[0], "SetControlActionCallback", EHostMemberKind::Method,
		        FNativeTypeDesc::Scalar(ENativeType::Void),
		        {FNativeTypeDesc::Function("fn(int,bool,double,string,double,double)->bool")},
		        [Bridge](const auto& Call, auto&, auto& Failure)
		{
			FInvokerScope Scope(*Bridge, Call.ScriptInvoker);
			const auto& View = *static_cast<const FWidgetView*>(Call.HostThis);
			const auto Widget = View.lock();
			if (!Widget || !Call.ScriptInvoker || !std::get<FFunctionHandle>(Call.Args[1]))
			{
				Failure = "gui.script_invoker_missing";
				return false;
			}
			auto Lifecycle = View.Lifecycle;
			if (!Lifecycle)
			{
				const auto Found = Bridge->Lifecycles.find(Widget->GetIdentity());
				if (Found != Bridge->Lifecycles.end())
				{
					Lifecycle = Found->second.lock();
				}
			}
			if (!Lifecycle) return true;
			if (Lifecycle->Owner.lock() != Bridge)
			{
				Failure = "gui.script_owner_missing";
				return false;
			}
			FValueRoot Callback;
			if (!Call.ScriptInvoker->RetainForGeneration(Call.Args[1], Callback, Failure)) return false;
			Lifecycle->ControlAction = std::move(Callback);
			return true;
		}, Error))
		{
			return false;
		}
		if (!Member(Registry, Bridge->Types[0], "SetChildrenChangedCallback", EHostMemberKind::Method,
		        FNativeTypeDesc::Scalar(ENativeType::Void), {FNativeTypeDesc::Function("fn(bool)->void")},
		        [Bridge](const auto& Call, auto&, auto& Failure)
		{
			FInvokerScope Scope(*Bridge, Call.ScriptInvoker);
			const auto& View = *static_cast<const FWidgetView*>(Call.HostThis);
			const auto Widget = View.lock();
			auto State = View.Lifecycle;
			if (!State && Widget)
			{
				const auto Found = Bridge->Lifecycles.find(Widget->GetIdentity());
				if (Found != Bridge->Lifecycles.end())
				{
					State = Found->second.lock();
				}
			}
			const auto* Handler = std::get_if<FFunctionHandle>(&Call.Args[1]);
			if (!Widget || !State || State->Owner.lock() != Bridge || !Call.ScriptInvoker || !Handler || !*Handler)
			{
				Failure = "gui.script_owner_missing";
				return false;
			}
			FValueRoot Root;
			if (!Call.ScriptInvoker->RetainForGeneration(Call.Args[1], Root, Failure)) return false;
			if (!Widget->SetChildrenChangedCallback([State](bool bRetry, auto& Error)
			{
				const auto Bridge = State->Owner.lock();
				if (!Bridge || !CheckScriptProgram(*Bridge, State->Program, State->Generation, Error)) return false;
				const auto* Value = State->ChildrenChanged.Read();
				if (!Value)
				{
					Error = "gui.stale_script_callback";
					return false;
				}
				const auto Callback = std::get<FFunctionHandle>(*Value);
				FValue Result;
				std::vector<FValue> Arguments{bRetry};
				if (Bridge->Invoker) return Bridge->Invoker->InvokeCallable(Callback, Arguments, Result, Error);
				if (Bridge->Services.Invoke)
				{
					Arguments.insert(Arguments.begin(), Callback);
					return Bridge->Services.Invoke("VS.Gui.Internal.DispatchChildrenCallback", Arguments, Result, Error);
				}
				Error = "gui.script_execution_bridge_unavailable";
				return false;
			}, Failure))
			{
				return false;
			}
			State->ChildrenChanged = std::move(Root);
			return true;
		}, Error))
		{
			return false;
		}
		for (unsigned Variant = 0; Variant < 6; ++Variant)
		{
			const bool bInstall = (Variant & 1u) == 0, bWithOwner = Variant >= 2, bSourceState = Variant >= 4;
			std::vector<FNativeTypeDesc> Parameters{FNativeTypeDesc::Function(
			    bSourceState ? "fn(int,int64,bool)->bool" : "fn(int,int64)->bool")};
			if (bWithOwner)
			{
				Parameters.push_back(FNativeTypeDesc::Function("fn()->void"));
			}
			if (bSourceState)
			{
				Parameters.push_back(FNativeTypeDesc::Scalar(ENativeType::Bool));
			}
			if (!Member(
			        Registry, Bridge->Types[0], bInstall ? "SetLifecycleCallback" : "SetBorrowedLifecycleCallback",
			        EHostMemberKind::Method, FNativeTypeDesc::Scalar(ENativeType::Void),
			        std::move(Parameters),
			        [Bridge, bInstall, bWithOwner, bSourceState](const auto& Call, auto&, auto& Failure)
			{
				FInvokerScope Scope(*Bridge, Call.ScriptInvoker);
				auto& View = *static_cast<FWidgetView*>(Call.HostThis);
				const auto Widget = View.lock();
				const auto Callback = std::get<FFunctionHandle>(Call.Args[1]);
				if (!Widget || !Callback || !Call.ScriptInvoker)
				{
					Failure = "gui.script_invoker_missing";
					return false;
				}
				const auto Anchor = bWithOwner ? std::get<FFunctionHandle>(Call.Args[2]) : nullptr;
				if (bWithOwner && (!Anchor || !Anchor->CapturedThis))
				{
					Failure = "gui.script_owner_missing";
					return false;
				}
				auto State = bInstall ? std::make_shared<FScriptLifecycle>() : View.Lifecycle;
				if (!State)
				{
					const auto Found = Bridge->Lifecycles.find(Widget->GetIdentity());
					if (Found != Bridge->Lifecycles.end())
					{
						State = Found->second.lock();
					}
				}
				if (!State)
				{
					// A host-owned widget may have its own unrelated observer.
					return true;
				}
				FValueRoot Root;
				if (!Call.ScriptInvoker->RetainForGeneration(Call.Args[1], Root, Failure))
				{
					return false;
				}
				State->Callback = std::move(Root);
				++State->CallbackRevision;
				State->Owner = Bridge;
				State->Program = Call.ScriptInvoker->GetProgramOwner();
				State->Generation = Call.ScriptInvoker->GetProgramGeneration();
				State->bPolling = true;
				State->ScriptOwner = Anchor ? Anchor->CapturedThis : FObjectHandle{};
				State->bObserveSources = bWithOwner;
				State->bProvideSourceState = bSourceState;
				State->bAllowStableSources = bSourceState && std::get<bool>(Call.Args[3]);
				State->bSourceSnapshot = false;
				State->Fields.clear();
				if (bInstall && !Widget->SetLifecycleObserver([State, Target = std::weak_ptr<FWidget>(Widget)](EWidgetLifecycle Stage, auto& Error)
				{
					const auto Bridge = State->Owner.lock();
					const auto Widget = Target.lock();
					if (!Bridge || !Widget)
					{
						return true;
					}
					if (!CheckScriptProgram(*Bridge, State->Program, State->Generation, Error))
					{
						return false;
					}
					if (Stage == EWidgetLifecycle::Synchronize && !FlushBindingUpdates(*Bridge, Error))
					{
						return false;
					}
					bool bScriptChanged = true;
					const bool bSourcesChanged = Stage == EWidgetLifecycle::Synchronize &&
					    State->SourcesChanged(*Widget, bScriptChanged);
					if (Stage == EWidgetLifecycle::Synchronize && !State->bPolling &&
					    !bSourcesChanged)
					{
						return true;
					}
					const auto* Value = State->Callback.Read();
					if (!Value)
					{
						Error = "gui.stale_script_callback";
						return false;
					}
					const auto Callback = std::get<FFunctionHandle>(*Value);
					const auto CallbackRevision = State->CallbackRevision;
					const auto Token = Widget->GetSynchronizationToken();
					if (Token > static_cast<std::uint64_t>(INT64_MAX))
					{
						Error = "gui.synchronization_token_overflow";
						return false;
					}
					std::vector<FValue> Arguments;
					Arguments.reserve(State->bProvideSourceState ? 3 : 2);
					Arguments.emplace_back(static_cast<std::int32_t>(Stage));
					Arguments.emplace_back(static_cast<std::int64_t>(Token));
					if (State->bProvideSourceState)
					{
						Arguments.emplace_back(!bScriptChanged && State->bAllowStableSources);
					}
					const bool bCaptureInputs = Stage == EWidgetLifecycle::Synchronize &&
					    State->bAllowStableSources;
					if (bCaptureInputs)
					{
						// Reuse the observer's buffer. A failed callback leaves
						// it invalid, so the next update performs a full retry.
						State->bSourceSnapshot = false;
						// SourcesChanged already verified these input identities and
						// revisions. Reuse them before entering script, so changes
						// made inside the callback remain visible on the next round.
						if (bScriptChanged)
						{
							State->CaptureFields(State->Fields);
						}
					}
					FValue Result;
					bool Success;
					if (Bridge->Invoker)
					{
						Success = Bridge->Invoker->InvokeCallable(Callback, Arguments, Result, Error);
					}
					else if (Bridge->Services.Invoke)
					{
						Arguments.insert(Arguments.begin(), Callback);
						Success = Bridge->Services.Invoke("VS.Gui.Internal.DispatchLifecycleCallback",
						    Arguments, Result, Error);
					}
					else
					{
						Error = "gui.script_execution_bridge_unavailable";
						return false;
					}
					if (Success && Stage == EWidgetLifecycle::Synchronize &&
					    State->CallbackRevision == CallbackRevision)
					{
						const auto* Current = State->Callback.Read();
						const auto* CurrentCallback = Current ? std::get_if<FFunctionHandle>(Current) : nullptr;
						if (CurrentCallback && *CurrentCallback == Callback)
						{
							const auto* Continue = std::get_if<bool>(&Result);
							State->bPolling = !Continue || *Continue;
							if (!State->bPolling || State->bAllowStableSources)
							{
								State->CaptureSources(*Widget, bCaptureInputs);
							}
						}
					}
					return Success;
				}, Failure))
				{
					return false;
				}
				View.Lifecycle = State;
				Bridge->Lifecycles[Widget->GetIdentity()] = State;
				if ((++Bridge->LifecycleSweep & 63u) == 0)
				{
					for (auto It = Bridge->Lifecycles.begin(); It != Bridge->Lifecycles.end();)
					{
						if (It->second.expired())
						{
							It = Bridge->Lifecycles.erase(It);
						}
						else
						{
							++It;
						}
					}
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[0], "AdoptDeclarationScope", EHostMemberKind::Method,
		        FNativeTypeDesc::Scalar(ENativeType::Void), {Int64},
		        [](const auto& Call, auto&, auto& Failure)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			const auto Scope = std::get<std::int64_t>(Call.Args[1]);
			if (!Widget || Scope <= 0)
			{
				Failure = "gui.invalid_widget_or_scope";
				return false;
			}
			return Widget->AdoptDeclarationScope(static_cast<std::uint64_t>(Scope), Failure);
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "AllocateNameScope", EHostMemberKind::StaticMethod, Int64, {},
		        [](const auto&, auto& Out, auto&)
		{
			Out = static_cast<std::int64_t>(FWidget::AllocateNameScope());
			return true;
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "SetDeclarationScope", EHostMemberKind::Method,
		        FNativeTypeDesc::Scalar(ENativeType::Void), {Int64},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			const auto Scope = std::get<std::int64_t>(Call.Args[1]);
			if (!Widget || Scope < 0)
			{
				Error = "gui.invalid_widget_or_scope";
				return false;
			}
			return Widget->SetDeclarationScope(static_cast<std::uint64_t>(Scope), Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "FindInScope", EHostMemberKind::Method, FNativeTypeDesc::Host(TypeNames[0]),
		        {FNativeTypeDesc::Scalar(ENativeType::String), Int64},
		        [Bridge](const auto& Call, auto& Out, auto& Error)
		{
			const auto Root = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Root)
			{
				Error = "gui.widget_expired";
				return false;
			}
			const auto& Id = std::get<std::string>(Call.Args[1]);
			const auto Scope = std::get<std::int64_t>(Call.Args[2]);
			if (Scope < 0)
			{
				Error = "gui.invalid_scope";
				return false;
			}
			const auto Found = Root->FindInScope(Id, static_cast<std::uint64_t>(Scope), Error);
			if (!Error.empty())
			{
				return false;
			}
			Out = Found ? Make(Bridge, 0, std::weak_ptr<FWidget>(Found)) : FValue(FHostObjectHandle{});
			return true;
		},
		        Error, true))
		{
			return false;
		}
		return Member(
		    Registry, Bridge->Types[0], "GetLayoutValue", EHostMemberKind::Method,
		    FNativeTypeDesc::Scalar(ENativeType::Double), {FNativeTypeDesc::Scalar(ENativeType::Int)},
		    [](const auto& Call, auto& Out, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			FLayoutBox Box;
			if (!Widget->GetLayout(Box, Error))
			{
				return false;
			}
			const auto Index = std::get<std::int32_t>(Call.Args[1]);
			if (Index < 0 || Index > 7)
			{
				Error = "gui.invalid_geometry_member";
				return false;
			}
			const auto& Rect = Index < 4 ? Box.Border : Box.Content;
			Out = Index % 4 == 0 ? Rect.X : Index % 4 == 1 ? Rect.Y
			    : Index % 4 == 2                           ? Rect.Width
			                                               : Rect.Height;
			return true;
		},
		    Error);
	}
} // namespace VeroScript::Gui::ScriptBindings
