// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <cmath>
#include <limits>
#include <functional>

static_assert(!std::is_copy_constructible_v<VeroScript::Gui::FWidget>);
static_assert(!std::is_move_constructible_v<VeroScript::Gui::FWidget>);
static_assert(!std::is_copy_constructible_v<VeroScript::Gui::FContext>);

namespace
{
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}

	class FBackend final : public VeroScript::Gui::IBackendContext
	{
	public:
		//~ Begin VeroScript::Gui::IBackendContext Interface
		VeroScript::Gui::FCapabilities GetCapabilities() const noexcept override
		{
			return Capabilities;
		}
		bool Synchronize(const VeroScript::Gui::FViewport&, const std::vector<VeroScript::Gui::FBackendNode>& Value,
		    std::string&) override
		{
			Nodes = Value;
			return true;
		}
		bool Update(double, std::vector<VeroScript::Gui::FLayoutBox>& Layout, std::string&) override
		{
			for (const auto& Node : Nodes)
			{
				Layout.push_back({Node.Id, {0, 0, 100, 100}, {0, 0, 100, 100}});
			}
			if (OnLayout)
			{
				OnLayout(Layout);
			}
			if (bInvalidLayout && !Layout.empty())
			{
				Layout[0].Border.Width = -1;
			}
			return true;
		}
		bool Render(bool& bPresented, std::string&) override
		{
			bPresented = false;
			return true;
		}
		bool Close(std::string&) override
		{
			return true;
		}
		//~ End VeroScript::Gui::IBackendContext Interface
		VeroScript::Gui::FCapabilities Capabilities{true};
		std::function<void(std::vector<VeroScript::Gui::FLayoutBox>&)> OnLayout;
		std::vector<VeroScript::Gui::FBackendNode> Nodes;
		bool bInvalidLayout = false;
	};

	void CheckResolvedOutputs()
	{
		using namespace VeroScript;
		using namespace Gui;
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* View = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Parent = FWidget::Create(), Child = FWidget::Create();
		double Font = 18, Opacity = .25;
		int Reads = 0;
		Require(Parent->AddChild(Child, Error) && Context->SetRoot(Parent, Error) &&
		        Parent->Bind(EProperty::FontSize, [&](auto& Value, auto&)
		{
			Value = Font;
			return true;
		}, Error) &&
		        Child->Bind(EProperty::Opacity, [&](auto& Value, auto&)
		{
			++Reads;
			Value = Opacity;
			return true;
		}, Error),
		    Error);
		Require(Context->Update(0, Error) && View->Nodes.back().Properties.FontSize == 18 &&
		        View->Nodes.back().Properties.Opacity == .25,
		    Error);
		const auto Before = Reads;
		Require(Context->Update(1, Error) && Reads > Before, "resolved output cache skipped a dynamic getter");
		Font = 23;
		Opacity = .5;
		Require(Context->Update(2, Error) && View->Nodes.back().Properties.FontSize == 23 &&
		        View->Nodes.back().Properties.Opacity == .5,
		    "resolved output cache missed dynamic inheritance");
		FAnimationOptions Options;
		Options.Duration = 1;
		Options.MaxStep = 1;
		Require(Child->Animate(EProperty::Opacity, .9, Options, Error) && Context->Update(3, Error) &&
		        Context->Update(3.5, Error) && View->Nodes.back().Properties.Opacity > .5,
		    Error);
		Require(Child->StopAnimation(EProperty::Opacity, Error) && Context->Update(4, Error) &&
		        View->Nodes.back().Properties.Opacity == .5,
		    "stopped animation retained cached presentation");
		Require(Child->Animate(EProperty::Opacity, .9, Options, Error) && Context->Update(5, Error) &&
		        Context->Update(6, Error) && View->Nodes.back().Properties.Opacity == .5,
		    "finished animation retained cached presentation");
		Require(Child->Clear(EProperty::Opacity, Error) && Child->SetStyle(EProperty::Opacity, .75, Error) &&
		        Context->Update(7, Error) && View->Nodes.back().Properties.Opacity == .75,
		    Error);
		Require(Child->Set(EProperty::Text, std::string("updated"), Error) && Context->Update(8, Error) &&
		        View->Nodes.back().Properties.Text == "updated",
		    "resolved output cache missed text replacement");
		Require(Context->Close(Error), Error);
	}

	void CheckSynchronizedOutputs()
	{
		using namespace VeroScript::Gui;
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* View = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create(), Child = FWidget::Create();
		int Calls = 0;
		double Model = .2;
		bool bReadBeforeWrite = false, bClear = false, bFail = false;
		Require(Root->AddChild(Child, Error) && Root->SetLifecycleObserver([&](EWidgetLifecycle Stage, auto& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize || !bReadBeforeWrite) return true;
			FPropertyValue Value;
			return Child->Get(EProperty::Opacity, Value, Failure) &&
			    Root->SetSynchronized(EProperty::Gap, std::get<double>(Value) * 10, Failure);
		}, Error) &&
		        Child->SetLifecycleObserver([&](EWidgetLifecycle Stage, auto& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			++Calls;
			if (bFail)
			{
				Failure = "synchronized output failure";
				return false;
			}
			return bClear ? Child->ClearSynchronized(EProperty::Opacity, Failure) : Child->SetSynchronized(EProperty::Opacity, Model, Failure);
		}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		Calls = 0;
		Model = .4;
		Require(Context->Update(1, Error) && Calls == 1 && View->Nodes.back().Properties.Opacity == .4,
		    "unobserved script output caused redundant stabilization");
		Calls = 0;
		Model = .8;
		bReadBeforeWrite = true;
		Require(Context->Update(2, Error) && Calls == 2 && View->Nodes.front().Properties.Gap == 8,
		    "read-before-write dependency did not stabilize");
		Calls = 0;
		bClear = true;
		bReadBeforeWrite = false;
		Require(Child->SetStyle(EProperty::Opacity, .35, Error) && Context->Update(3, Error) && Calls == 1 &&
		        View->Nodes.back().Properties.Opacity == .35,
		    "synchronized clear lost its style fallback");
		bFail = true;
		Require(!Context->Update(4, Error) && Error == "synchronized output failure", "failed sync was swallowed");
		Calls = 0;
		bFail = false;
		bClear = false;
		Model = .6;
		Require(Context->Update(4, Error) && Calls == 1 && View->Nodes.back().Properties.Opacity == .6,
		    "failed synchronization did not recover");
		Require(Context->Close(Error), Error);
	}

	void CheckSynchronizedRemount()
	{
		using namespace VeroScript::Gui;
		std::string Error;
		auto First = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Second = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto Widget = FWidget::Create();
		bool bRead = true;
		int Calls = 0;
		Require(Widget->SetLifecycleObserver([&](EWidgetLifecycle Stage, auto& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			++Calls;
			FPropertyValue Value;
			return bRead ? Widget->Get(EProperty::Opacity, Value, Failure) : Widget->SetSynchronized(EProperty::Opacity, .5, Failure);
		}, Error) &&
		        First->SetRoot(Widget, Error) && First->Update(0, Error),
		    Error);
		Require(Calls == 1, "remount test did not begin from a single evaluation round");
		Require(First->SetRoot(nullptr, Error) && Second->SetRoot(Widget, Error), Error);
		bRead = false;
		Calls = 0;
		Require(Second->Update(0, Error) && Calls == 1, "read dependencies leaked between contexts");
		Require(First->Close(Error) && Second->Close(Error), Error);
	}

	void CheckSynchronizedInheritedReads()
	{
		using namespace VeroScript::Gui;
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* View = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create(), Reader = FWidget::Create(), Parent = FWidget::Create();
		auto Middle = FWidget::Create(), Child = FWidget::Create();
		double Model = 20;
		int Reads = 0;
		Require(Root->AddChild(Reader, Error) && Root->AddChild(Parent, Error) &&
		        Parent->AddChild(Middle, Error) && Middle->AddChild(Child, Error) &&
		        Reader->SetLifecycleObserver([&](EWidgetLifecycle Stage, auto& Failure)
		{
			if (Stage != EWidgetLifecycle::Synchronize) return true;
			++Reads;
			FPropertyValue Value;
			return Child->Get(EProperty::FontSize, Value, Failure);
		}, Error) &&
		        Parent->SetLifecycleObserver([&](EWidgetLifecycle Stage, auto& Failure)
		{
			return Stage != EWidgetLifecycle::Synchronize || Parent->SetSynchronized(EProperty::FontSize, Model, Failure);
		}, Error) &&
		        Context->SetRoot(Root, Error) && Context->Update(0, Error),
		    Error);
		Require(Reads == 2 && View->Nodes.back().Properties.FontSize == 20,
		    "an inherited read did not stabilize its ancestor output");
		Model = 32;
		Reads = 0;
		Require(Context->Update(1, Error) && Reads == 2 && View->Nodes.back().Properties.FontSize == 32,
		    "a cached ancestor hid a read-before-write dependency");
		Require(Child->Bind(EProperty::FontSize, [](auto& Value, auto&)
		{
			Value.reset();
			return true;
		}, Error),
		    Error);
		Model = 40;
		Reads = 0;
		Require(Context->Update(2, Error) && Reads == 2 && View->Nodes.back().Properties.FontSize == 40,
		    "an empty optional getter lost its inherited dependency");
		Require(Child->Set(EProperty::FontSize, 12.0, Error), Error);
		Model = 48;
		Reads = 0;
		Require(Context->Update(3, Error) && Reads == 1 && View->Nodes.back().Properties.FontSize == 12,
		    "a local override introduced a false ancestor dependency");
		Require(Context->Close(Error), Error);
	}

	void CheckPaintCallbacks()
	{
		using namespace VeroScript::Gui;
		class FPaintedWidget final : public FWidget
		{
		public:
			int Calls = 0;

		protected:
			void OnPaint(FPaintContext&) override
			{
				++Calls;
			}
		};
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* View = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		auto Root = FWidget::Create();
		auto Native = std::make_shared<FPaintedWidget>();
		Require(Root->AddChild(Native, Error) && Context->SetRoot(Root, Error) && Context->Update(0, Error), Error);
		Require(!View->Nodes[0].Paint && bool(View->Nodes[1].Paint), "empty elements retained custom paint work or a native override was lost");
		FPaintContext Canvas({0, 0, 100, 100});
		Require(View->Nodes[1].Paint(Canvas, Error) && Native->Calls == 1, Error);
		int Calls = 0;
		Require(Root->SetPaintCallback([&](auto&, auto&)
		{
			++Calls;
			return true;
		}, Error) &&
		        Context->Update(1, Error),
		    Error);
		Require(View->Nodes[0].Paint(Canvas, Error) && Calls == 1, "explicit custom paint was skipped");
		Require(Root->SetPaintCallback({}, Error) && Context->Update(2, Error) && !View->Nodes[0].Paint, Error);
		Require(Context->Close(Error), Error);
	}

	class FObservedWidget final : public VeroScript::Gui::FWidget
	{
	public:
		static std::shared_ptr<FObservedWidget> Create()
		{
			return std::shared_ptr<FObservedWidget>(new FObservedWidget);
		}
		int Initialized = 0, Changed = 0, Mounted = 0, Unmounted = 0;
		bool bThrowNext = false;
		std::shared_ptr<VeroScript::Gui::FContext>* ResetOwner = nullptr;

	protected:
		//~ Begin VeroScript::Gui::FWidget Interface
		void OnInitialized() override
		{
			++Initialized;
		}
		void OnMounted() noexcept override
		{
			++Mounted;
		}
		void OnUnmounted() noexcept override
		{
			++Unmounted;
			if (ResetOwner)
			{
				ResetOwner->reset();
			}
		}
		void OnPropertyChanged(VeroScript::Gui::EProperty) override
		{
			++Changed;
			if (bThrowNext)
			{
				bThrowNext = false;
				throw std::runtime_error("property hook failed");
			}
		}
		//~ End VeroScript::Gui::FWidget Interface
	};
} // namespace

int main()
{
	using namespace VeroScript;
	using namespace Gui;
	try
	{
		CheckResolvedOutputs();
		CheckSynchronizedOutputs();
		CheckSynchronizedRemount();
		CheckSynchronizedInheritedReads();
		CheckPaintCallbacks();
		{
			std::string Failure;
			auto Backend = std::make_unique<FBackend>();
			auto* View = Backend.get();
			View->Capabilities.bScrolling = true;
			View->OnLayout = [View](auto& Layout)
			{
				const auto& Scroll = View->Nodes.front().Control;
				Layout.front() = {View->Nodes.front().Id, {0, 0, 100, 100}, {0, 0, 100, 100}, 400, 400,
				    Scroll.ScrollX, Scroll.ScrollY};
				Layout.back().Border = {100 - Scroll.ScrollX, 100 - Scroll.ScrollY, 200, 200};
				Layout.back().Content = Layout.back().Border;
			};
			auto Scene = FContext::Create(std::move(Backend), {100, 100, 1}, Failure);
			auto Scroll = FWidget::Create(), Target = FWidget::Create();
			FControlOptions Options;
			Options.Kind = EControlKind::ScrollView;
			Require(Scroll->SetControlOptions(Options, Failure) && Target->Set(EProperty::Focusable, true, Failure) &&
			        Scroll->AddChild(Target, Failure) && Scene->SetRoot(Scroll, Failure),
			    Failure);
			for (double Offset : {0.0, 100.0, 200.0})
			{
				Options.ScrollX = Options.ScrollY = Offset;
				Require(Scroll->SetControlOptions(Options, Failure) && Scene->Update(0, Failure) &&
				        Target->FocusControl(Failure) && Scene->Update(0, Failure),
				    Failure);
				const auto Revealed = Scroll->GetControlOptions();
				for (int Attempt = 0; Attempt < 3; ++Attempt)
				{
					Require(Target->FocusControl(Failure) && Scene->Update(0, Failure), Failure);
					const auto Current = Scroll->GetControlOptions();
					Require(Current.ScrollX == Revealed.ScrollX && Current.ScrollY == Revealed.ScrollY,
					    "an oversized focus target aligned exactly with a viewport edge must not oscillate");
				}
			}
			Require(Scene->Close(Failure), Failure);
		}
		{
			std::string Failure;
			FPaintContext Canvas({0, 0, 10, 10});
			std::thread Worker([&]
			{
				Canvas.DrawLine(0, 0, 1, 1, 1, Graphics::FColor{1, 1, 1, 1}, Failure);
			});
			Worker.join();
			Require(Failure == "gui.wrong_thread", "paint operation lost its thread diagnostic");
			Require(Canvas.Finish(Failure) && !Canvas.DrawLine(0, 0, 1, 1, 1, Graphics::FColor{1, 1, 1, 1}, Failure) &&
			        Failure == "gui.paint_context_expired",
			    "paint operation lost its expired-canvas diagnostic");
		}
		{
			std::string Failure;
			auto Backend = std::make_unique<FBackend>();
			auto* View = Backend.get();
			auto Scene = FContext::Create(std::move(Backend), {200, 20, 1}, Failure);
			auto ScrollBar = FWidget::Create();
			FControlOptions Options;
			Options.Kind = EControlKind::ScrollBar;
			struct FRangeCase
			{
				double Maximum, PageSize, Width;
			};
			const auto Limit = std::numeric_limits<double>::max();
			const FRangeCase Cases[] = {{Limit, Limit, 100}, {Limit, Limit / 2, 200.0 / 3},
			    {1, 2, 400.0 / 3}, {1, 0, 8},
			    {0, 0, 200}, {1e-300, 1e300, 200}};
			Require(Scene->SetRoot(ScrollBar, Failure), Failure);
			for (const auto& Case : Cases)
			{
				Options.Maximum = Case.Maximum;
				Options.PageSize = Case.PageSize;
				Options.Value = Case.Maximum / 2;
				Require(ScrollBar->SetControlOptions(Options, Failure) && Scene->Update(0, Failure), Failure);
				FPaintContext Canvas({0, 0, 200, 20});
				Require(View->Nodes.at(0).Paint(Canvas, Failure) && Canvas.Finish(Failure), Failure);
				const auto& Vertices = Canvas.GetMeshes().at(0).Vertices;
				Require(std::abs(Vertices.at(1).X - Vertices.at(0).X - Case.Width) < .01,
				    "scrollbar page-size proportion changed at a numeric boundary");
			}
			Require(Scene->Close(Failure), Failure);
		}
		{
			std::string Failure;
			auto AnimationBackend = std::make_unique<FBackend>();
			auto* View = AnimationBackend.get();
			auto Scene = FContext::Create(std::move(AnimationBackend), {100, 100, 1}, Failure);
			auto Item = FWidget::Create();
			double Base = 0.2;
			Require(Item->Bind(
			            EProperty::Opacity,
			            [&](auto& Value, auto&)
			{
				Value = Base;
				return true;
			},
			            Failure) &&
			        Scene->SetRoot(Item, Failure) && Scene->Update(0, Failure),
			    Failure);
			FAnimationOptions Options;
			Options.Duration = 1;
			Options.MaxStep = 1;
			Require(Item->Animate(EProperty::Opacity, 1.0, Options, Failure) && Scene->Update(0, Failure), Failure);
			Base = 0.4;
			Require(Scene->Update(0.5, Failure) && std::abs(View->Nodes.at(0).Properties.Opacity - 0.6) < 1e-6,
			    "animation did not affect backend presentation");
			FPropertyValue Value;
			Require(Item->Get(EProperty::Opacity, Value, Failure) && std::get<double>(Value) == 0.4,
			    "animation replaced the base binding");
			Require(Scene->Update(1, Failure) && View->Nodes.at(0).Properties.Opacity == 0.4,
			    "animation completion did not reveal the current base");
			Options.bHoldEnd = true;
			Require(Item->Animate(EProperty::Opacity, 0.0, Options, Failure) && Scene->Update(1, Failure) &&
			        Scene->Update(2, Failure) && View->Nodes.at(0).Properties.Opacity == 0,
			    Failure);
			Base = 0.7;
			Require(Scene->Update(3, Failure) && View->Nodes.at(0).Properties.Opacity == 0 &&
			        Item->Get(EProperty::Opacity, Value, Failure) && std::get<double>(Value) == 0.7,
			    "held animation endpoint replaced the live base binding");
			Require(Scene->SetRoot({}, Failure) && Scene->SetRoot(Item, Failure) &&
			        Scene->Update(4, Failure) && View->Nodes.at(0).Properties.Opacity == 0,
			    "held animation endpoint was lost after remounting");
			Options.Duration = -1;
			Require(!Item->Animate(EProperty::Opacity, 1.0, Options, Failure) &&
			        Item->GetPresentation(EProperty::Opacity, Value, Failure) && std::get<double>(Value) == 0,
			    "rejected animation destroyed the previous presentation");
			Options.Duration = 1;
			Require(Item->Animate(EProperty::Opacity, 1.0, Options, Failure) && Scene->Update(4, Failure) &&
			        Scene->Update(4.5, Failure) && std::abs(View->Nodes.at(0).Properties.Opacity - 0.5) < 1e-6,
			    "new animation did not start from the held endpoint");
			Require(Item->StopAnimation(EProperty::Opacity, Failure) && Scene->Update(5, Failure) &&
			        View->Nodes.at(0).Properties.Opacity == 0.7 && Scene->Close(Failure),
			    Failure);
		}
		std::string Error;
		auto Backend = std::make_unique<FBackend>();
		auto* Recording = Backend.get();
		auto Context = FContext::Create(std::move(Backend), {100, 100, 1}, Error);
		Require(Context != nullptr, Error);
		auto Root = FObservedWidget::Create();
		auto Child = FWidget::Create();
		Require(Root->AddChild(Child, Error), Error);
		Require(!Child->AddChild(Root, Error), "cycle accepted");
		Require(Context->SetRoot(Root, Error), Error);
		Require(Context->Update(0, Error), Error);
		Require(Root->Initialized == 1 && Root->Mounted == 1 && Root->Changed == static_cast<int>(EProperty::Count),
		    "initial hooks");
		Require(Context->Update(1, Error), Error);
		Require(Root->Changed == static_cast<int>(EProperty::Count), "unchanged hooks repeated");
		Recording->bInvalidLayout = true;
		Require(!Context->Update(1, Error), "invalid backend geometry published");
		bool bPresented = true;
		Require(!Context->Render(bPresented, Error) && !bPresented, "render after failed update");
		FLayoutBox Previous;
		Require(Root->GetLayout(Previous, Error) && Previous.Border.Width == 100, "last successful geometry lost");
		Recording->bInvalidLayout = false;
		Root->bThrowNext = true;
		Require(Root->Set(EProperty::Gap, 1.0, Error), Error);
		Require(!Context->Update(1, Error), "property exception accepted");
		const int Notifications = Root->Changed;
		Require(Context->Update(1, Error) && Root->Changed == Notifications + 1,
		    "failed property notification was not retried");
		Require(!Child->Set(EProperty::Opacity, 2.0, Error), "invalid opacity accepted");
		Require(Child->SetStyle(EProperty::Gap, FPropertyValue{4.0}, Error), Error);
		Require(Child->Set(EProperty::Gap, 8.0, Error), Error);
		FPropertyValue Value;
		Require(Child->Get(EProperty::Gap, Value, Error) && std::get<double>(Value) == 8, "local precedence");
		Require(Child->Clear(EProperty::Gap, Error), Error);
		Require(Child->Get(EProperty::Gap, Value, Error) && std::get<double>(Value) == 4, "style fallback");
		int Reads = 0;
		Require(Child->Bind(
		            EProperty::Gap,
		            [&](auto& Out, auto&)
		{
			++Reads;
			Out = 12.0;
			return true;
		},
		            Error),
		    Error);
		Require(Context->Update(2, Error), Error);
		Require(Reads == 1, "binding evaluated repeatedly");
		Require(Child->Get(EProperty::Gap, Value, Error) && Reads == 1, "binding cache missed");
		Require(Child->Bind(
		            EProperty::Gap, [&](auto&, auto& Failure)
		{
			return Child->Get(EProperty::Gap, Value, Failure);
		}, Error),
		    Error);
		Require(!Context->Update(3, Error) && Error.find("binding_cycle") != std::string::npos,
		    "binding cycle undetected");
		Require(Child->Set(EProperty::Gap, 0.0, Error), Error);
		Require(Context->Update(3, Error), Error);
		int BoundaryEvents = 0;
		for (const auto Type : {Platform::EEventType::MouseEntered, Platform::EEventType::MouseLeft})
		{
			auto Subscription = Root->Subscribe(Type, false,
			    [&](auto& Event)
			{
				Require(Event.Target == Root, "mouse boundary notification did not target the root");
				++BoundaryEvents;
			}, Error);
			Require(Subscription.IsConnected(), "mouse boundary subscription failed: " + Error);
			Platform::FEvent Boundary;
			Boundary.Type = Type;
			Boundary.WindowId = 1;
			bool bBoundaryHandled = false;
			Require(Context->ProcessEvent(Boundary, bBoundaryHandled, Error), Error);
		}
		Require(BoundaryEvents == 2, "mouse boundary notifications were not delivered");
		std::string Order;
		Root->Subscribe(Platform::EEventType::PointerDown, true, [&](auto&)
		{
			Order += 'C';
		}, Error);
		Child->Subscribe(
		    Platform::EEventType::PointerDown, false,
		    [&](auto& Event)
		{
			Order += 'T';
			Event.PreventDefault();
		},
		    Error);
		Root->Subscribe(Platform::EEventType::PointerDown, false, [&](auto&)
		{
			Order += 'B';
		}, Error);
		Platform::FEvent Input;
		Input.Type = Platform::EEventType::PointerDown;
		Input.WindowId = 1;
		Input.X = Input.Y = 10;
		Input.Button = 1;
		bool bHandled = false;
		Require(Context->ProcessEvent(Input, bHandled, Error), Error);
		Require(Order == "CTB" && bHandled, "event route");
		Require(Child->Set(EProperty::Focusable, true, Error), Error);
		Require(Context->SetFocus(Child, Error), Error);
		Require(Context->CapturePointer(7, Child, Error), Error);
		Require(Child->Set(EProperty::Focusable, false, Error), Error);
		Require(Context->Update(4, Error), Error);
		Require(!Context->GetFocus(), "nonfocusable retained focus");
		Order.clear();
		Input.PointerId = 7;
		Input.X = Input.Y = 200;
		Require(Context->ProcessEvent(Input, bHandled, Error) && Order == "CTB",
		    "focusability change released pointer capture");
		Require(Child->Set(EProperty::Gap, 2.0, Error), Error);
		Require(Context->ProcessEvent(Input, bHandled, Error), "event queue blocked by property mutation");
		Require(Child->Bind(
		            EProperty::Gap,
		            [&](auto& Out, auto& Failure)
		{
			Out = 5.0;
			return Root->Set(EProperty::Gap, 10.0, Failure);
		},
		            Error),
		    Error);
		Require(!Context->Update(4, Error) && Error.find("getter_mutation") != std::string::npos,
		    "getter side effect accepted");
		Require(Child->Set(EProperty::Gap, 2.0, Error), Error);
		int Deferred = 0;
		Require(Context->Post([&](auto& Failure)
		{
			return !Context->Update(4, Failure);
		}, Error),
		    Error);
		Require(Context->Post([&](auto&) -> bool
		{
			throw std::runtime_error("deferred failure");
		}, Error),
		    Error);
		Require(Context->Post(
		            [&](auto&)
		{
			++Deferred;
			return true;
		},
		            Error),
		    Error);
		Require(!Context->Update(4, Error), "deferred exception accepted");
		Require(Context->Update(4, Error) && Deferred == 1, "remaining deferred work lost");
		Require(Root->RemoveChild(Child, Error), Error);
		Require(!Child->IsMounted(), "detached child mounted");
		Require(Context->SetRoot({}, Error), Error);
		Require(Context->SetRoot(Root, Error), Error);
		Require(Context->Update(5, Error), Error);
		Require(Root->Initialized == 1 && Root->Mounted == 2 && Root->Unmounted == 1, "remount hooks");
		double External = 1;
		Require(Child->Bind(
		            EProperty::Gap,
		            [&](auto& Out, auto&)
		{
			Out = External;
			return true;
		},
		            Error),
		    Error);
		Require(Root->AddChild(Child, Error) && Context->Update(6, Error), Error);
		Require(Root->RemoveChild(Child, Error), Error);
		auto SecondContext = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		External = 19;
		Require(SecondContext->SetRoot(Child, Error) && SecondContext->Update(0, Error), Error);
		Require(Child->Get(EProperty::Gap, Value, Error) && std::get<double>(Value) == 19,
		    "remount reused an old context binding cache");
		Require(SecondContext->Close(Error), Error);
		auto Disposable = FWidget::Create();
		const std::weak_ptr<FWidget> WeakDisposable = Disposable;
		auto* RawDisposable = Disposable.get();
		Require(Root->AddChild(Disposable, Error), Error);
		Disposable.reset();
		Require(RawDisposable->Dispose(Error) && WeakDisposable.expired(), "parent-owned disposal lifetime");
		auto ClosingContext = FContext::Create(std::make_unique<FBackend>(), {100, 100, 1}, Error);
		auto ClosingRoot = FObservedWidget::Create();
		auto ClosingChild = FObservedWidget::Create();
		Require(ClosingRoot->AddChild(ClosingChild, Error) && ClosingContext->SetRoot(ClosingRoot, Error) &&
		        ClosingContext->Update(0, Error),
		    Error);
		ClosingChild->ResetOwner = &ClosingContext;
		Require(ClosingRoot->RemoveChild(ClosingChild, Error) && !ClosingContext && ClosingRoot->Unmounted == 1,
		    "owner release interrupted unmount notifications");
		auto BatchRoot = FWidget::Create();
		auto First = FWidget::Create();
		auto Second = FWidget::Create();
		Require(BatchRoot->SetChildren({First}, Error), Error);
		Require(!BatchRoot->SetChildren({Second, Second}, Error) && BatchRoot->GetChild(0, Error) == First &&
		        First->GetParent() == BatchRoot && !Second->GetParent(),
		    "duplicate child changed tree before validation");
		Require(First->SetId("same", Error) && Second->SetId("same", Error), Error);
		Require(!BatchRoot->SetChildren({First, Second}, Error) && BatchRoot->GetChildCount() == 1 &&
		        !Second->GetParent(),
		    "duplicate ID left a partially attached child");
		Require(Second->SetId("second", Error) && BatchRoot->SetChildren({Second, First}, Error) &&
		        BatchRoot->GetChild(0, Error) == Second && BatchRoot->GetChild(1, Error) == First,
		    Error);
		Require(!Second->SetChildren({BatchRoot}, Error) && BatchRoot->GetChildCount() == 2,
		    "batch replacement introduced a cycle");
		Require(BatchRoot->SetChildren({Second}, Error) && !First->GetParent(), Error);
		bool bWrongThreadRejected = false;
		std::thread Worker(
		    [&]
		{
			std::string Failure;
			bWrongThreadRejected = !Root->Set(EProperty::Gap, 1.0, Failure);
		});
		Worker.join();
		Require(bWrongThreadRejected, "cross-thread mutation accepted");
		Require(Context->Close(Error), Error);
		std::cout << "GUI contracts passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
