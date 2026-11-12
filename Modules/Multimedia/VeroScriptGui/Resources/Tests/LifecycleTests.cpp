// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript::Gui;

	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FBackend final : public IBackendContext
	{
	public:
		//~ Begin IBackendContext Interface
		FCapabilities GetCapabilities() const noexcept override
		{
			return Capabilities;
		}
		bool Synchronize(const FViewport&, const std::vector<FBackendNode>& Value, std::string&) override
		{
			Nodes = Value;
			return true;
		}
		bool Update(double, std::vector<FLayoutBox>& Boxes, std::string&) override
		{
			for (const auto& Node : Nodes)
			{
				FLayoutBox Box{Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}};
				Box.ScrollWidth = ScrollWidth;
				Box.ScrollHeight = ScrollHeight;
				Boxes.push_back(Box);
			}
			return true;
		}
		bool Render(bool& bPresented, std::string&) override
		{
			bPresented = false;
			if (OnRender)
			{
				OnRender();
			}
			return true;
		}
		bool Close(std::string&) override
		{
			if (OnClose)
			{
				OnClose();
			}
			return true;
		}
		bool FocusTextControl(std::uint64_t Id, const FTextInputServices& Services, std::string& Error) override
		{
			return !Services.SetInputArea || Services.SetInputArea(Id != 0, {0, 0, 100, 100}, 0, Error);
		}
		//~ End IBackendContext Interface

		FCapabilities Capabilities{true};
		std::function<void()> OnRender, OnClose;
		std::vector<FBackendNode> Nodes;
		double ScrollWidth = 0, ScrollHeight = 0;
	};

	struct FReleaseAction
	{
		~FReleaseAction()
		{
			Action();
		}
		std::function<void()> Action;
	};

	class FFrameService final : public IFrameService
	{
	public:
		//~ Begin IFrameService Interface
		bool BeginFrame(double, std::string&) override
		{
			return true;
		}
		//~ End IFrameService Interface

		std::shared_ptr<FReleaseAction> Lifetime;
	};

	void CheckConfigurationOwnerRelease()
	{
		for (const bool Replace : {false, true})
		{
			std::string Error;
			auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
			const std::weak_ptr<FContext> Weak = Context;
			bool AliveDuringRelease = false;
			auto Lifetime = std::make_shared<FReleaseAction>();
			Lifetime->Action = [&]
			{
				Context.reset();
				AliveDuringRelease = !Weak.expired();
			};
			auto Service = std::make_shared<FFrameService>();
			Service->Lifetime = std::move(Lifetime);
			Require(Context->SetFrameService("lifetime", std::move(Service), Error), Error);
			auto* Raw = Context.get();
			std::shared_ptr<IFrameService> Next;
			if (Replace)
			{
				Next = std::make_shared<FFrameService>();
			}
			Require(Raw->SetFrameService("lifetime", std::move(Next), Error), Error);
			Require(AliveDuringRelease && Weak.expired(),
			    "frame service replacement did not retain its active context");
		}

		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		const std::weak_ptr<FContext> Weak = Context;
		bool AliveDuringRelease = false;
		auto Lifetime = std::make_shared<FReleaseAction>();
		Lifetime->Action = [&]
		{
			Context.reset();
			AliveDuringRelease = !Weak.expired();
		};
		FMarkupDiagnostic Diagnostic;
		auto Theme = FStyleSheet::Compile({"lifetime.vswcss", "* { gap: 4px; }"}, Diagnostic,
		    [Lease = std::move(Lifetime)](const auto&, const auto& Reference, auto& Identity, auto&)
		{
			Identity = Reference;
			return true;
		});
		Require(Theme && Context->SetTheme(std::move(Theme), Error), Diagnostic.Format() + Error);
		auto* Raw = Context.get();
		Require(Raw->SetTheme({}, Error), Error);
		Require(AliveDuringRelease && Weak.expired(), "theme replacement did not retain its active context");
	}

	void CheckConfigurationReleaseReentrancy()
	{
		for (int Kind = 0; Kind < 4; ++Kind)
		{
			std::string Error;
			auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
			bool Released = false, CloseAccepted = false;
			auto Lifetime = std::make_shared<FReleaseAction>();
			Lifetime->Action = [&]
			{
				std::string Reentry;
				Released = true;
				CloseAccepted = Context->Close(Reentry);
			};
			if (Kind == 0)
			{
				auto Service = std::make_shared<FFrameService>();
				Service->Lifetime = std::move(Lifetime);
				Require(Context->SetFrameService("lifetime", std::move(Service), Error) &&
				        Context->SetFrameService("lifetime", {}, Error), Error);
			}
			else if (Kind == 1)
			{
				FMarkupDiagnostic Diagnostic;
				auto Theme = FStyleSheet::Compile({"reentry.vswcss", "* { gap: 4px; }"}, Diagnostic,
				    [Lease = std::move(Lifetime)](const auto&, const auto& Reference, auto& Identity, auto&)
				{
					Identity = Reference;
					return true;
				});
				Require(Theme && Context->SetTheme(std::move(Theme), Error) && Context->SetTheme({}, Error),
				    Diagnostic.Format() + Error);
			}
			else if (Kind == 2)
			{
				FPointerServices Services;
				Services.SetCursor = [Lease = std::move(Lifetime)](auto, bool, auto&)
				{
					return true;
				};
				Require(Context->SetPointerServices(std::move(Services), Error) &&
				        Context->SetPointerServices({}, Error), Error);
			}
			else
			{
				FTextInputServices Services;
				Services.SetInputArea = [Lease = std::move(Lifetime)](bool, const auto&, auto, auto&)
				{
					return true;
				};
				Require(Context->SetTextInputServices(std::move(Services), Error) &&
				        Context->SetTextInputServices({}, Error), Error);
			}
			Require(Released && !CloseAccepted && Context->IsOpen(),
			    "configuration destruction closed its context during replacement");
			Require(Context->Close(Error), Error);
		}
	}

	void CheckScrollRangeNotifications()
	{
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* Output = Backend.get();
		Output->ScrollWidth = Output->ScrollHeight = 100;
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		int Calls = 0;
		bool Fail = false;
		double SeenHeight = 0;
		Require(Root->SetLifecycleObserver([&](auto Stage, auto& Failure)
		{
			if (Stage != EWidgetLifecycle::LayoutChanged) return true;
			++Calls;
			FLayoutBox Box;
			Require(Root->GetLayout(Box, Failure), Failure);
			SeenHeight = Box.ScrollHeight;
			if (Fail)
			{
				Failure = "test.scroll_range_retry";
				return false;
			}
			return true;
		}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		Require(Calls == 1 && SeenHeight == 100 && Context->Update(1, Error) && Calls == 1,
		    "unchanged scroll geometry repeated layout notifications");
		Output->ScrollHeight = 240;
		Require(Context->Update(2, Error) && Calls == 2 && SeenHeight == 240,
		    "content range changes did not notify an unchanged viewport");
		Output->ScrollWidth = 300;
		Fail = true;
		Require(!Context->Update(3, Error) && Calls == 3 && Error == "test.scroll_range_retry", Error);
		Fail = false;
		Require(Context->Update(4, Error) && Calls == 4 && Context->Update(5, Error) && Calls == 4,
		    "scroll range notification failed to retry or settle");
		Require(Context->Close(Error), Error);
	}

	class FHookWidget final : public FWidget
	{
	public:
		std::function<void()> Initialize, Mount;
		std::function<void(EProperty)> Change;

	protected:
		//~ Begin FWidget Interface
		void OnInitialized() override
		{
			if (Initialize)
			{
				Initialize();
			}
		}
		void OnMounted() noexcept override
		{
			if (Mount)
			{
				Mount();
			}
		}
		void OnPropertyChanged(EProperty Property) override
		{
			if (Change)
			{
				Change(Property);
			}
		}
		//~ End FWidget Interface
	};

	void CheckLifecycle()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = std::make_shared<FHookWidget>();
		auto Child = std::make_shared<FHookWidget>();
		double SynchronizedChild = 0, SynchronizedRoot = 0;
		bool bParentInitialized = false, bChildMounted = false;
		Child->Change = [&](EProperty Property)
		{
			if (Property == EProperty::Gap)
			{
				FPropertyValue Value;
				Require(Child->Get(Property, Value, Error), Error);
				SynchronizedChild = std::get<double>(Value);
			}
		};
		Child->Initialize = [&]
		{
			Require(Child->Set(EProperty::Gap, 7.0, Error), Error);
		};
		Root->Initialize = [&]
		{
			Require(SynchronizedChild == 7, "parent initialized before child properties synchronized");
			bParentInitialized = true;
		};
		Root->Change = [&](EProperty Property)
		{
			if (Property == EProperty::Gap)
			{
				FPropertyValue Value;
				Require(Root->Get(Property, Value, Error), Error);
				SynchronizedRoot = std::get<double>(Value);
			}
		};
		bool bSet = false;
		Root->Mount = [&]
		{
			bSet = Root->Set(EProperty::Gap, 9.0, Error);
		};
		Child->Mount = [&]
		{
			bChildMounted = SynchronizedRoot == 9;
		};
		Require(Root->AddChild(Child, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(bParentInitialized && bSet && bChildMounted, "mount observed unsynchronized parent properties");
		Require(Context->Close(Error), Error);

		// One-time lifecycle progress is not a property oscillation, even for a large tree.
		Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		Root = std::make_shared<FHookWidget>();
		int Initialized = 0;
		int BindingReads = 0;
		Require(Root->Bind(
		            EProperty::Opacity,
		            [&](auto& Out, auto&)
		{
			++BindingReads;
			Out = 1.0;
			return true;
		},
		            Error),
		    Error);
		for (int Index = 0; Index < 80; ++Index)
		{
			auto Item = std::make_shared<FHookWidget>();
			const std::weak_ptr<FHookWidget> Weak = Item;
			Item->Initialize = [&, Weak]
			{
				Require(Weak.lock()->Set(EProperty::Gap, 1.0, Error), Error);
				++Initialized;
			};
			Require(Root->AddChild(Item, Error), Error);
		}
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error) && Initialized == 80, Error);
		Require(BindingReads < 10, "sibling initialization caused a full update per widget");
		Root->Change = [&](EProperty Property)
		{
			if (Property == EProperty::Gap)
			{
				FPropertyValue Value;
				Require(Root->Get(Property, Value, Error) &&
				        Root->Set(Property, std::get<double>(Value) == 1 ? 2.0 : 1.0, Error),
				    Error);
			}
		};
		Require(Root->Set(EProperty::Gap, 1.0, Error), Error);
		Require(!Context->Update(1, Error) && Error.find("update_cycle") != std::string::npos,
		    "property oscillation not bounded");
		Require(Context->Close(Error), Error);
	}

	void CheckObserverReplacement()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Payload = std::make_shared<int>(7);
		const auto WeakPayload = std::weak_ptr<int>(Payload);
		int ReplacementCalls = 0;
		Require(Root->SetLifecycleObserver(
		            [&, Payload](auto Stage, auto& Failure)
		{
			if (Stage == EWidgetLifecycle::Initialized)
			{
				Require(Root->SetLifecycleObserver(
				            [&](auto, auto&)
				{
					++ReplacementCalls;
					return true;
				},
				            Failure),
				    Failure);
				Require(!WeakPayload.expired() && *Payload == 7,
				    "replacing an active lifecycle observer destroyed its capture");
			}
			return true;
		},
		            Error),
		    Error);
		Payload.reset();
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(WeakPayload.expired() && ReplacementCalls > 0,
		    "lifecycle replacement was retained or not used for the next stage");
		const auto Before = ReplacementCalls;
		Require(Context->Update(1, Error) && ReplacementCalls > Before, Error);
		Payload = std::make_shared<int>(9);
		const auto WeakClearedPayload = std::weak_ptr<int>(Payload);
		int ClearedCalls = 0;
		Require(Root->SetLifecycleObserver(
		            [&, Payload](auto, auto& Failure)
		{
			++ClearedCalls;
			Require(Root->SetLifecycleObserver({}, Failure), Failure);
			Require(!WeakClearedPayload.expired() && *Payload == 9,
			    "clearing an active lifecycle observer destroyed its capture");
			return true;
		},
		            Error),
		    Error);
		Payload.reset();
		Require(Context->Update(2, Error) && WeakClearedPayload.expired() && ClearedCalls == 1, Error);
		Require(Context->Update(3, Error) && ClearedCalls == 1 && Context->Close(Error), Error);
	}

	void CheckLayoutNotificationRetry()
	{
		for (const bool bThrow : {false, true})
		{
			std::string Error;
			auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
			auto Root = FWidget::Create(), First = FWidget::Create(), Last = FWidget::Create();
			const std::vector<std::shared_ptr<FWidget>> Nodes{Root, First, Last};
			std::vector<int> Calls(3);
			for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
			{
				Require(Nodes[Index]->SetLifecycleObserver([&, Index](auto Stage, auto& Failure)
				{
					if (Stage != EWidgetLifecycle::LayoutChanged) return true;
					++Calls[Index];
					for (const auto& Node : Nodes)
					{
						FLayoutBox Box;
						Require(Node->GetLayout(Box, Failure) && Box.Border.Width == 100,
						    "layout callbacks must see the complete current geometry");
					}
					if (Index == 1 && Calls[Index] == 1)
					{
						if (bThrow)
						{
							throw std::runtime_error("expected layout notification failure");
						}
						Failure = "expected layout notification failure";
						return false;
					}
					return true;
				}, Error),
				    Error);
			}
			Require(Root->SetChildren({First, Last}, Error) && Context->SetRoot(Root, Error), Error);
			Require(!Context->Update(0, Error) && Error.find("expected layout notification failure") != std::string::npos,
			    "layout notification failure was not propagated");
			Require(Calls == std::vector<int>({1, 1, 0}), "layout notification order changed after failure");
			Require(Context->Update(0, Error) && Calls == std::vector<int>({1, 2, 1}),
			    "unchanged geometry lost failed or undelivered layout notifications: " + Error);
			Require(Context->Update(1, Error) && Calls == std::vector<int>({1, 2, 1}),
			    "successful layout notifications were repeated without a geometry change");
			Require(Context->Close(Error), Error);
		}
	}

	void CheckTextServiceReentrancy()
	{
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		Backend->Capabilities.bTextEditing = true;
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		FControlOptions Options;
		Options.Kind = EControlKind::TextBox;
		Require(Root->SetControlOptions(Options, Error) && Root->Set(EProperty::Focusable, true, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		int Calls = 0;
		FTextInputServices Services;
		Services.SetInputArea = [&](bool, const FRect&, std::int64_t, std::string& Failure)
		{
			++Calls;
			std::string Reentry;
			Require(!Context->Close(Reentry) && Reentry == "gui.reentrant_operation",
			    "text input service callback closed its active backend");
			Require(!Context->SetFocus({}, Reentry) && Reentry.find("gui.reentrant_operation") == 0,
			    "text input service callback changed focus recursively");
			Require(!Root->Set(EProperty::Gap, 1.0, Reentry) && Reentry.find("gui.reentrant_operation") == 0,
			    "text input service callback mutated its active widget");
			Require(!Context->SetTextInputServices({}, Reentry) && Reentry.find("gui.reentrant_operation") == 0,
			    "text input service callback replaced its active services");
			Require(Context->GetRoot() == Root && Context->GetFocus() == Root,
			    "text input service callback could not read the current tree and focus");
			Failure.clear();
			return true;
		};
		Require(Context->SetTextInputServices(Services, Error) && Context->SetFocus(Root, Error) && Calls == 1, Error);
		Require(Context->SetTextInputServices(Services, Error) && Calls == 3,
		    "text service replacement failed to release and reacquire focus");
		FTextInputServices Throwing;
		Throwing.SetInputArea = [](bool, const FRect&, std::int64_t, std::string&) -> bool
		{
			throw std::runtime_error("expected text service failure");
		};
		Require(!Context->SetTextInputServices(std::move(Throwing), Error) &&
		        Error == "gui.callback_exception: expected text service failure",
		    "text input service exception escaped its boundary");
		Require(Root->Set(EProperty::Gap, 2.0, Error) && Context->SetTextInputServices(Services, Error),
		    "text input service exception left the reentrancy guard active");
		Require(Context->Close(Error), Error);
	}

	void CheckCleanupFailure()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto A = FWidget::Create(), B = FWidget::Create();
		int Unmounted = 0;
		for (const auto& Child : {A, B})
		{
			Require(Child->SetLifecycleObserver(
			            [&, Weak = std::weak_ptr<FWidget>(Child)](auto Stage, auto& Failure)
			{
				if (Stage == EWidgetLifecycle::Unmounted)
				{
					Require(Weak.lock()->IsMounted(), "cleanup lost its old context");
					++Unmounted;
					Failure = "expected cleanup failure";
					return false;
				}
				return true;
			},
			            Error),
			    Error);
		}
		Require(Root->SetChildren({A, B}, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(!Root->Dispose(Error) && Error == "expected cleanup failure", "cleanup failure was lost");
		Require(Unmounted == 2 && Root->IsDisposed() && A->IsDisposed() && B->IsDisposed() && !A->GetParent() &&
		        !B->GetParent() && !Context->GetRoot(),
		    "failed cleanup left a partial live subtree");
		Require(Root->Dispose(Error) && Context->Close(Error), Error);
	}

	void CheckScopedNames()
	{
		std::string Error;
		auto Root = FWidget::Create(), Child = FWidget::Create();
		const auto Scope = FWidget::AllocateNameScope();
		Require(Child->SetDeclarationScope(Scope, Error) && Child->SetId("part", Error) && Root->AddChild(Child, Error),
		    Error);
		Require(Root->FindInScope("part", Scope, Error) == Child, "scope lookup failed");
		Require(Child->SetId("renamed", Error) && !Root->FindInScope("part", Scope, Error) &&
		        Root->FindInScope("renamed", Scope, Error) == Child,
		    "renaming retained a stale name index");
		Require(Root->RemoveChild(Child, Error) && !Root->FindInScope("renamed", Scope, Error),
		    "removal retained a stale name index");
		const auto Other = FWidget::AllocateNameScope();
		Require(Child->SetDeclarationScope(Other, Error) && Root->SetChildren({Child}, Error) &&
		        !Root->FindInScope("renamed", Scope, Error) && Root->FindInScope("renamed", Other, Error) == Child,
		    "moving the declaration scope retained stale names");
		Require(Child->Dispose(Error) && !Root->FindInScope("renamed", Other, Error),
		    "disposal retained a stale name index");
	}

	void CheckPostedWork()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		const auto Owner = std::this_thread::get_id();
		int Delivered = 0;
		bool Accepted = false;
		std::thread Worker(
		    [&]
		{
			Accepted = Context->Post(
			    [&](auto& Failure)
			{
				Require(std::this_thread::get_id() == Owner, "posted work ran on a worker");
				++Delivered;
				return Context->Post(
				    [&](auto&)
				{
					++Delivered;
					return true;
				},
				    Failure);
			},
			    Error);
		});
		Worker.join();
		Require(Accepted && Delivered == 0, "posting executed work immediately or rejected a worker");
		Require(Context->Update(0, Error) && Delivered == 1, "posted work did not use a stable batch");
		Require(Context->Update(1, Error) && Delivered == 2 && Context->Close(Error), Error);
		std::thread Closed([&]
		{
			Accepted = Context->Post([](auto&)
			{
				return true;
			}, Error);
		});
		Closed.join();
		Require(!Accepted && Error == "gui.context_closed", "closed context accepted posted work");
	}

	void CheckPointerDevices()
	{
		using namespace VeroScript;
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create(), A = FWidget::Create(), B = FWidget::Create();
		Require(Root->SetChildren({A, B}, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		int HitsA = 0, HitsB = 0;
		A->Subscribe(Platform::EEventType::PointerMove, false, [&](auto&)
		{
			++HitsA;
		}, Error);
		B->Subscribe(Platform::EEventType::PointerMove, false, [&](auto&)
		{
			++HitsB;
		}, Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerMove;
		Event.PointerKind = Platform::EPointerKind::Touch;
		Event.DeviceKind = Platform::EInputDeviceKind::Touch;
		Event.PointerId = 7;
		Event.WindowId = 1;
		Event.DeviceId = 1;
		Event.X = Event.Y = 150;
		const auto First = FPointerIdentity::FromEvent(Event);
		Event.DeviceId = 2;
		const auto Second = FPointerIdentity::FromEvent(Event);
		Require(Context->CapturePointer(First, A, Error) && Context->CapturePointer(Second, B, Error), Error);
		bool Handled = false;
		Require(Context->ProcessEvent(Event, Handled, Error) && HitsA == 0 && HitsB == 1,
		    "same-number pointers shared a capture");
		Event.DeviceId = 1;
		Require(Context->ProcessEvent(Event, Handled, Error) && HitsA == 1 && HitsB == 1, Error);
		Event.Type = Platform::EEventType::PointerCancelled;
		Require(Context->ProcessEvent(Event, Handled, Error), Error);
		Event.Type = Platform::EEventType::PointerMove;
		Require(Context->ProcessEvent(Event, Handled, Error) && HitsA == 1, "cancelled pointer retained its capture");
		Event.DeviceId = 2;
		Require(Context->ProcessEvent(Event, Handled, Error) && HitsB == 2,
		    "another device's cancellation cleared this capture");
		Event.Type = Platform::EEventType::InputDeviceRemoved;
		Event.WindowId = 0;
		Require(Context->ProcessEvent(Event, Handled, Error), Error);
		Event.Type = Platform::EEventType::PointerMove;
		Event.WindowId = 1;
		Require(Context->ProcessEvent(Event, Handled, Error) && HitsB == 2, "removed device retained a capture");
		Require(Context->Close(Error), Error);
	}

	void CheckRetiredContext()
	{
		std::string Error;
		const auto Owner = std::this_thread::get_id();
		int Closes = 0, Notifications = 0;
		auto Backend = std::make_unique<FBackend>();
		Backend->OnClose = [&]
		{
			Require(std::this_thread::get_id() == Owner, "backend cleanup ran on a worker");
			++Closes;
		};
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		Require(Root->SetLifecycleObserver(
		            [&](auto Stage, auto&)
		{
			if (Stage == EWidgetLifecycle::Unmounted)
			{
				++Notifications;
			}
			return true;
		},
		            Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		std::thread Worker([Value = std::move(Context)]() mutable
		{
			Value.reset();
		});
		Worker.join();
		Require(Closes == 0 && Notifications == 0, "final release cleaned GUI resources on a worker");
		Require(FContext::CollectRetired(Error) && Closes == 1 && Notifications == 0 && !Root->IsMounted(), Error);
		Require(FContext::CollectRetired(Error) && Closes == 1, "retired context was collected twice");
	}

	void CheckRenderReentry()
	{
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* Recording = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		Require(Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		bool bPresented = false;
		Require(!Context->Update(-1, Error) && !Context->Render(bPresented, Error),
		    "invalid update retained a renderable frame");
		Require(Context->Update(0, Error), Error);
		Recording->OnRender = [&]
		{
			std::string Failure;
			bool bPresented = false;
			Require(!Context->Update(1, Failure), "update reentered render");
			Require(!Context->Render(bPresented, Failure), "render reentered render");
			Require(!Root->Set(EProperty::Gap, 2.0, Failure), "property mutated during render");
			Require(!Context->Close(Failure), "context closed during render");
		};
		Require(Context->Render(bPresented, Error), Error);
		Recording->OnRender = []
		{
			throw std::runtime_error("backend failure");
		};
		Require(!Context->Render(bPresented, Error) && !bPresented, "render exception escaped");
		Recording->OnRender = {};
		Require(Context->Render(bPresented, Error) && Root->Set(EProperty::Gap, 3.0, Error) &&
		        Context->Update(1, Error),
		    Error);
		Recording->OnClose = []
		{
			throw std::runtime_error("backend close failure");
		};
		Require(!Context->Close(Error) && !Context->IsOpen() && !Context->GetRoot(),
		    "failed close retained an operational context");
		Require(!Context->Post([](auto&)
		{
			return true;
		}, Error) &&
		        Error == "gui.context_closed",
		    "closing context accepted more work");
		Recording->OnClose = [&]
		{
			std::string Failure;
			Require(!Context->Close(Failure) && !Context->Update(2, Failure), "backend close was reentered");
		};
		Require(Context->Close(Error), Error);
	}

	void CheckDeferredClose()
	{
		for (int Mode = 0; Mode < 3; ++Mode)
		{
			std::string Error;
			auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
			auto Payload = std::make_shared<int>(42);
			const std::weak_ptr<int> Weak = Payload;
			Require(Context->Post(
			            [&](auto& Failure)
			{
				Require(Context->Close(Failure), Failure);
				if (Mode == 1)
				{
					throw std::runtime_error("deferred failure after close");
				}
				return Mode == 2;
			},
			            Error),
			    Error);
			Require(Context->Post([Payload](auto&)
			{
				return *Payload == 42;
			}, Error),
			    Error);
			Payload.reset();
			Require(!Context->Update(0, Error) && !Context->IsOpen(), "deferred close was not observed");
			Require(Weak.expired(), "failed deferred work restored tasks into a closed context");
		}
	}

	void CheckPublishedHitTest()
	{
		using namespace VeroScript;
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Child = FWidget::Create();
		Require(Root->AddChild(Child, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		int Hits = 0;
		Child->Subscribe(Platform::EEventType::PointerDown, false, [&](auto&)
		{
			++Hits;
		}, Error);
		Platform::FEvent Event;
		Event.Type = Platform::EEventType::PointerDown;
		Event.WindowId = 1;
		Event.X = Event.Y = 10;
		Event.Button = 1;
		bool bHandled = false;
		Require(Context->ProcessEvent(Event, bHandled, Error) && Hits == 1, Error);
		Require(Root->RemoveChild(Child, Error) && Root->AddChild(Child, Error), Error);
		Require(Context->ProcessEvent(Event, bHandled, Error) && Hits == 1, "remounted child reused old geometry");
		Require(Context->Update(1, Error) && Context->ProcessEvent(Event, bHandled, Error) && Hits == 2, Error);
		const std::weak_ptr<FWidget> Weak = Child;
		Require(Root->RemoveChild(Child, Error), Error);
		Child.reset();
		Require(Weak.expired(), "published traversal retained a removed widget");
		Require(Context->ProcessEvent(Event, bHandled, Error) && Hits == 2 && Context->Close(Error), Error);
	}
	void CheckInheritedPropertyReuse()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Child = std::make_shared<FHookWidget>();
		auto Local = FWidget::Create();
		auto Styled = FWidget::Create();
		std::string Family(128, 'a');
		int ParentReads = 0, ChildReads = 0, Changes = 0;
		bool bFail = false;
		Child->Change = [&](EProperty Property)
		{
			if (Property == EProperty::FontFamily)
			{
				++Changes;
			}
		};
		Require(Root->Bind(EProperty::FontFamily, [&](auto& Value, auto& Failure)
		{
			++ParentReads;
			if (bFail)
			{
				Failure = "inherited getter failed";
				return false;
			}
			Value = Family;
			return true;
		}, Error),
		    Error);
		Require(Child->Bind(EProperty::FontFamily, [&](auto& Value, auto&)
		{
			++ChildReads;
			Value.reset();
			return true;
		}, Error),
		    Error);
		Require(Local->Set(EProperty::FontFamily, std::string("local"), Error) &&
		        Styled->SetStyle(EProperty::FontFamily, std::string("styled"), Error) &&
		        Root->SetChildren({Child, Local, Styled}, Error) && Context->SetRoot(Root, Error) &&
		        Context->Update(0, Error),
		    Error);
		const auto Text = [&](const auto& Widget)
		{
			FPropertyValue Value;
			Require(Widget->Get(EProperty::FontFamily, Value, Error), Error);
			return std::get<std::string>(Value);
		};
		const auto PreviousChanges = Changes, PreviousParentReads = ParentReads, PreviousChildReads = ChildReads;
		Require(Context->Update(1, Error) && Text(Child) == Family && Changes == PreviousChanges &&
		        ParentReads > PreviousParentReads && ChildReads > PreviousChildReads,
		    "unchanged inheritance skipped a getter or repeated a notification: " + Error);
		Family.assign(128, 'b');
		Require(Context->Update(2, Error) && Text(Child) == Family && Changes == PreviousChanges + 1 &&
		        Text(Local) == "local" && Text(Styled) == "styled",
		    Error);
		Require(Local->Clear(EProperty::FontFamily, Error) &&
		        Styled->SetStyle(EProperty::FontFamily, std::nullopt, Error) && Context->Update(3, Error) &&
		        Text(Local) == Family && Text(Styled) == Family,
		    Error);
		bFail = true;
		Require(!Context->Update(4, Error) && Error == "inherited getter failed",
		    "cached inheritance hid a getter failure");
		bFail = false;
		Family.assign(128, 'c');
		Require(Context->Update(5, Error) && Text(Child) == Family, Error);
		Require(Context->Close(Error), Error);
	}

	void CheckControlModelDependency()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Provider = FWidget::Create();
		FControlOptions Options;
		Options.Kind = EControlKind::ProgressBar;
		Options.Maximum = 100;
		Require(Provider->SetControlOptions(Options, Error) && Root->AddChild(Provider, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		double Observed = 0, Derived = 0, Model = 12;
		Require(Root->SetLifecycleObserver([&](EWidgetLifecycle Stage, std::string&)
		{
			if (Stage == EWidgetLifecycle::Synchronize)
			{
				Observed = Derived;
			}
			return true;
		}, Error),
		    Error);
		Require(Provider->SetLifecycleObserver([&](EWidgetLifecycle Stage, std::string& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			Options.Value = Model;
			Derived = Model * 2;
			return Provider->SetControlOptions(Options, Failure);
		}, Error),
		    Error);
		Require(Context->Update(1, Error) && Observed == Model * 2,
		    "numeric control synchronization lost a shared-model dependency: " + Error);
		Require(Context->Close(Error), Error);
	}

	void CheckControlReferenceDependency()
	{
		std::string Error;
		auto Context = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Provider = FWidget::Create();
		FControlOptions Options;
		Options.Kind = EControlKind::ProgressBar;
		Options.Maximum = 100;
		Require(Provider->SetControlOptions(Options, Error) && Root->AddChild(Provider, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		const auto& Reference = Provider->GetControlOptions();
		double Observed = 0, Model = 12;
		Require(Root->SetLifecycleObserver([&](EWidgetLifecycle Stage, std::string&)
		{
			if (Stage == EWidgetLifecycle::Synchronize)
			{
				Observed = Reference.Value;
			}
			return true;
		}, Error),
		    Error);
		Require(Provider->SetLifecycleObserver([&](EWidgetLifecycle Stage, std::string& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			Options.Value = Model;
			return Provider->SetControlOptions(Options, Failure);
		}, Error),
		    Error);
		Require(Context->Update(0, Error) && Observed == Model,
		    "a retained native control reference lost a forward dependency: " + Error);
		Model = 17;
		Require(Context->Update(1, Error) && Observed == Model, Error);
		Require(Provider->SetLifecycleObserver([&](EWidgetLifecycle Stage, std::string& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			Options.Value = Reference.Value + 1;
			return Provider->SetControlOptions(Options, Failure);
		}, Error),
		    Error);
		Require(!Context->Update(2, Error) && Error.find("gui.update_cycle") != std::string::npos,
		    "a retained control reference bypassed the stabilization cycle limit");
		Require(Context->Close(Error), Error);
	}
} // namespace

int main()
{
	try
	{
		CheckConfigurationOwnerRelease();
		CheckConfigurationReleaseReentrancy();
		CheckLifecycle();
		CheckObserverReplacement();
		CheckLayoutNotificationRetry();
		CheckScrollRangeNotifications();
		CheckTextServiceReentrancy();
		CheckCleanupFailure();
		CheckScopedNames();
		CheckPostedWork();
		CheckPointerDevices();
		CheckRetiredContext();
		CheckDeferredClose();
		CheckRenderReentry();
		CheckPublishedHitTest();
		CheckInheritedPropertyReuse();
		CheckControlReferenceDependency();
		CheckControlModelDependency();
		std::cout << "GUI lifecycle and render isolation passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
