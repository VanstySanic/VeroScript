// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiMarkup.h"
#include "VeroScriptGuiTestResources.h"
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace Gui;
	std::string Error;
	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message + ": " + Error);
		}
	}
	std::shared_ptr<FWidget> Make(EControlKind Kind, double Width, double Height)
	{
		auto Widget = FWidget::Create();
		FControlOptions Options;
		Options.Kind = Kind;
		if (Kind == EControlKind::Splitter)
		{
			Options.Value = .5;
			Options.Maximum = 1;
			Options.DividerSize = 8;
		}
		Require(Widget->SetControlOptions(Options, Error) &&
		        Widget->Set(EProperty::Width, FLength{ELengthUnit::Pixel, Width}, Error) &&
		        Widget->Set(EProperty::Height, FLength{ELengthUnit::Pixel, Height}, Error) &&
		        Widget->Set(EProperty::Focusable, Kind != EControlKind::None, Error),
		    "create control");
		return Widget;
	}
	FLayoutBox Box(const std::shared_ptr<FWidget> &Widget)
	{
		FLayoutBox Value;
		Require(Widget->GetLayout(Value, Error), "read layout");
		return Value;
	}
	struct FPointerHost
	{
		Platform::ECursor Cursor = Platform::ECursor::Default;
		bool bVisible = true, bCapture = false, bFailCapture = false, bFailCursor = false;
		int Throw = 0, CursorCalls = 0, CaptureCalls = 0;
		FPointerServices Services()
		{
			FPointerServices Result;
			Result.SetCursor = [this](auto Shape, bool Visible, auto &Failure)
			{
				++CursorCalls;
				if (Throw == 1)
				{
					throw std::runtime_error("cursor failure");
				}
				if (Throw == 2)
				{
					throw 7;
				}
				if (bFailCursor)
				{
					Failure = "test.cursor_failed";
					return false;
				}
				Cursor = Shape;
				bVisible = Visible;
				return true;
			};
			Result.SetMouseCapture = [this](bool Enabled, auto &Failure)
			{
				++CaptureCalls;
				if (bFailCapture)
				{
					Failure = "test.capture_failed";
					return false;
				}
				bCapture = Enabled;
				return true;
			};
			return Result;
		}
	};
	void CheckCaptureOnlyServices()
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {100, 100, 1}, Error);
		Require(Context != nullptr, "capture-only context");
		auto Root = Make(EControlKind::Button, 100, 40);
		int CursorReads = 0;
		Require(Root->Bind(
		            EProperty::Cursor,
		            [&](auto &, auto &Failure)
		{
			++CursorReads;
			Failure = "test.unavailable_cursor";
			return false;
		},
		            Error) &&
		        Context->SetRoot(Root, Error),
		    "unevaluated cursor configuration");
		FPointerHost Host;
		auto Services = Host.Services();
		Services.SetCursor = {};
		Require(Context->SetPointerServices(std::move(Services), Error), "attach capture-only services");
		Platform::FEvent Enter;
		Enter.Type = Platform::EEventType::MouseEntered;
		Enter.WindowId = 1;
		Enter.X = Enter.Y = 5;
		FEventResult Delivered;
		const FPointerIdentity Mouse{Platform::EInputDeviceKind::Mouse, 0, 0};
		Require(Context->ProcessEvent(Enter, Delivered, Error) && Context->CapturePointer(Mouse, Root, Error) &&
		        Host.bCapture && Context->ReleasePointer(Mouse, Error) && !Host.bCapture && CursorReads == 0,
		    "capture-only hosts do not evaluate an unused cursor binding");
		Require(Context->Close(Error), "close capture-only context");
	}
	void CheckReleaseLifetime()
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {100, 100, 1}, Error);
		Require(Context != nullptr, "capture lifetime context");
		auto Root = Make(EControlKind::Button, 100, 40);
		Require(Context->SetRoot(Root, Error), "capture lifetime root");
		std::weak_ptr<FContext> Weak = Context;
		bool bAliveDuringRelease = false;
		FPointerServices Services;
		Services.SetMouseCapture = [&](bool Enabled, auto &)
		{
			if (!Enabled)
			{
				Context.reset();
				bAliveDuringRelease = !Weak.expired();
			}
			return true;
		};
		const FPointerIdentity Mouse{Platform::EInputDeviceKind::Mouse, 0, 0};
		Require(Context->SetPointerServices(std::move(Services), Error) && Context->CapturePointer(Mouse, Root, Error),
		    "capture before dropping the host reference");
		auto *Raw = Context.get();
		Require(Raw->ReleasePointer(Mouse, Error) && bAliveDuringRelease && Weak.expired(),
		    "release keeps the context alive until its callback returns");
		Require(FContext::CollectRetired(Error), "collect released contexts");
	}
} // namespace
int main()
{
	try
	{
		CheckCaptureOnlyServices();
		CheckReleaseLifetime();
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {600, 180, 1}, Error);
		Require(Context != nullptr, "create context");
		FPointerHost Host;
		Require(Context->SetPointerServices(Host.Services(), Error), "attach pointer services");
		auto Root = FWidget::Create();
		auto Button = Make(EControlKind::Button, 100, 40), Child = FWidget::Create();
		auto Text = Make(EControlKind::TextBox, 100, 40), Slider = Make(EControlKind::Slider, 100, 40);
		auto Splitter = Make(EControlKind::Splitter, 200, 80), Blank = Make(EControlKind::None, 100, 80);
		Require(Child->Set(EProperty::Text, std::string("Child"), Error) && Button->AddChild(Child, Error) &&
		        Root->Set(EProperty::Layout, ELayout::Horizontal, Error) &&
		        Splitter->SetChildren({FWidget::Create(), FWidget::Create()}, Error) &&
		        Root->SetChildren({Button, Text, Slider, Splitter, Blank}, Error) && Context->SetRoot(Root, Error),
		    "create tree");
		double Time = 0;
		const auto Update = [&]
		{
			Require(Context->Update(Time += .01, Error), "update");
		};
		Update();
		Require(Host.CursorCalls == 0 && Host.CaptureCalls == 0, "idle GUI does not request mouse-only services");
		const FPointerIdentity Touch{Platform::EInputDeviceKind::Touch, 44, 2};
		Require(Context->CapturePointer(Touch, Button, Error) && Context->ReleasePointer(Touch, Error) &&
		        Host.CursorCalls == 0 && Host.CaptureCalls == 0,
		    "touch capture does not acquire mouse services");
		Platform::FEvent Input;
		Input.WindowId = 1;
		Input.DeviceId = 31;
		Input.Button = 1;
		FEventResult Delivered;
		const auto Send = [&](Platform::EEventType Type, double X, double Y)
		{
			Input.Type = Type;
			Input.X = X;
			Input.Y = Y;
			Require(Context->ProcessEvent(Input, Delivered, Error), "send pointer");
		};
		const auto Hover = [&](const std::shared_ptr<FWidget> &Widget)
		{
			const auto B = Box(Widget).Border;
			Send(Platform::EEventType::PointerMove, B.X + B.Width / 2, B.Y + B.Height / 2);
		};
		Hover(Child);
		Require(Host.Cursor == Platform::ECursor::Pointer, "child content uses button cursor");
		const auto Calls = Host.CursorCalls;
		Hover(Child);
		Update();
		Require(Host.CursorCalls == Calls, "unchanged cursor is not reapplied");
		Hover(Text);
		Require(Host.Cursor == Platform::ECursor::Text, "text input cursor");
		Hover(Slider);
		Require(Host.Cursor == Platform::ECursor::Pointer, "slider cursor");
		Hover(Splitter);
		Require(Host.Cursor == Platform::ECursor::ResizeEW, "horizontal divider cursor");
		const auto Split = Box(Splitter).Content;
		Send(Platform::EEventType::PointerMove, Split.X + 5, Split.Y + 5);
		Require(Host.Cursor == Platform::ECursor::Default, "pane content is not a resize handle");
		Hover(Splitter);
		Send(Platform::EEventType::PointerDown, Input.X, Input.Y);
		Require(Host.bCapture, "mouse drag acquires window capture");
		Platform::FEvent Boundary;
		Boundary.WindowId = 1;
		Boundary.Type = Platform::EEventType::MouseLeft;
		Require(Context->ProcessEvent(Boundary, Delivered, Error) && Host.bCapture &&
		        Host.Cursor == Platform::ECursor::ResizeEW,
		    "leaving the window preserves a dragging cursor and capture");
		Send(Platform::EEventType::PointerMove, -50, -50);
		Require(Host.bCapture && Host.Cursor == Platform::ECursor::ResizeEW,
		    "drag cursor stays pinned outside the window");
		Send(Platform::EEventType::PointerUp, -50, -50);
		Require(!Host.bCapture && Host.Cursor == Platform::ECursor::Default,
		    "release outside restores the cursor and capture");
		Hover(Button);
		Boundary.Type = Platform::EEventType::MouseLeft;
		Require(Context->ProcessEvent(Boundary, Delivered, Error), "leave window");
		Update();
		Update();
		Require(Host.Cursor == Platform::ECursor::Default,
		    "stationary layout does not resurrect hover outside the window");
		Boundary.Type = Platform::EEventType::MouseEntered;
		Boundary.X = 20;
		Boundary.Y = 10;
		Require(Context->ProcessEvent(Boundary, Delivered, Error) && Host.Cursor == Platform::ECursor::Pointer,
		    "window entry restores hover without a motion event");
		Require(Root->Set(EProperty::Cursor, ECursor::Crosshair, Error), "configure inherited cursor");
		Update();
		Require(Host.Cursor == Platform::ECursor::Crosshair, "cursor inheritance");
		Require(Button->Set(EProperty::Cursor, ECursor::Auto, Error), "override inherited cursor");
		Update();
		Require(Host.Cursor == Platform::ECursor::Pointer, "explicit auto restores control cursor");
		Require(Button->Set(EProperty::Cursor, ECursor::None, Error), "hide cursor");
		Update();
		Require(!Host.bVisible, "none hides the cursor");
		Require(Button->Clear(EProperty::Cursor, Error), "clear cursor");
		Update();
		Require(Host.bVisible && Host.Cursor == Platform::ECursor::Crosshair, "clear restores inherited cursor");
		Require(Root->Clear(EProperty::Cursor, Error) && Button->Set(EProperty::Enabled, false, Error),
		    "disable button");
		Update();
		Require(Host.Cursor == Platform::ECursor::Default, "disabled controls do not advertise activation");
		Require(Button->Set(EProperty::Cursor, ECursor::NotAllowed, Error), "disabled cursor override");
		Update();
		Require(Host.Cursor == Platform::ECursor::NotAllowed, "explicit cursor on a disabled control");
		Require(Button->Set(EProperty::Enabled, true, Error) && Button->Clear(EProperty::Cursor, Error),
		    "restore button");
		Hover(Button);
		Host.bFailCapture = true;
		Require(!Context->CapturePointer({Platform::EInputDeviceKind::Mouse, 31, 0}, Button, Error) &&
		        Error == "test.capture_failed",
		    "capture failure is reported");
		Host.bFailCapture = false;
		Update();
		Require(!Host.bCapture, "failed capture rolls back logical capture");
		Send(Platform::EEventType::PointerDown, Input.X, Input.Y);
		Require(Host.bCapture, "button captures mouse");
		Platform::FEvent Removed;
		Removed.Type = Platform::EEventType::InputDeviceRemoved;
		Removed.DeviceKind = Platform::EInputDeviceKind::Mouse;
		Removed.DeviceId = 99;
		Require(Context->ProcessEvent(Removed, Delivered, Error) && Host.bCapture,
		    "other device removal preserves capture");
		Removed.DeviceId = 31;
		Require(Context->ProcessEvent(Removed, Delivered, Error) && !Host.bCapture &&
		        Host.Cursor == Platform::ECursor::Default,
		    "active device removal clears cursor and capture");
		Hover(Button);
		for (int Throw : {1, 2})
		{
			Host.Throw = Throw;
			Require(Button->Set(EProperty::Cursor, ECursor::Wait, Error) && !Context->Update(Time += .01, Error) &&
			        Error.find("gui.pointer_service_exception") == 0,
			    "callback exceptions are errors");
			Host.Throw = 0;
			Update();
			Require(Host.Cursor == Platform::ECursor::Wait, "failed cursor update is retried");
			Require(Button->Clear(EProperty::Cursor, Error), "restore cursor after callback exception");
			Update();
		}
		Send(Platform::EEventType::PointerDown, Input.X, Input.Y);
		Boundary = {};
		Boundary.Type = Platform::EEventType::FocusLost;
		Boundary.WindowId = 1;
		Require(Context->ProcessEvent(Boundary, Delivered, Error) && !Host.bCapture &&
		        Host.Cursor == Platform::ECursor::Default,
		    "focus loss releases capture and cursor");
		Update();
		Hover(Button);
		Require(Context->CapturePointer({Platform::EInputDeviceKind::Mouse, 31, 0}, Button, Error),
		    "capture before reparenting");
		auto PendingParent = FWidget::Create();
		Require(PendingParent->Bind(
		            EProperty::Cursor,
		            [](auto &, auto &Failure)
		{
			Failure = "test.cursor_binding_failed";
			return false;
		},
		            Error) &&
		        Root->AddChild(PendingParent, Error) && Root->RemoveChild(Button, Error) &&
		        PendingParent->AddChild(Button, Error) && Button->Clear(EProperty::Cursor, Error),
		    "reparent under an unevaluated cursor binding");
		Require(!Context->ReleasePointer({Platform::EInputDeviceKind::Mouse, 31, 0}, Error) &&
		        Error == "test.cursor_binding_failed" && !Host.bCapture,
		    "cursor evaluation failure must not retain native capture");
		Require(PendingParent->RemoveChild(Button, Error) && Root->RemoveChild(PendingParent, Error) &&
		        Root->SetChildren({Button, Text, Slider, Splitter, Blank}, Error),
		    "restore tree after reparenting");
		Update();
		Hover(Button);
		Host.bFailCursor = true;
		Require(!Context->SetPointerServices({}, Error), "failed handoff retains existing services");
		Host.bFailCursor = false;
		FPointerHost Replacement;
		Require(Context->SetPointerServices(Replacement.Services(), Error) &&
		        Host.Cursor == Platform::ECursor::Default && Replacement.Cursor == Platform::ECursor::Pointer,
		    "service replacement restores the old host and updates the new host");
		Replacement.bFailCursor = true;
		Require(!Context->Close(Error) && !Context->IsOpen(),
		    "failed cursor cleanup leaves a retryable closing context");
		Replacement.bFailCursor = false;
		Require(Context->Close(Error) && Replacement.Cursor == Platform::ECursor::Default && Replacement.bVisible,
		    "close retries pointer cleanup");
		FMarkupDiagnostic Diagnostic;
		auto Program = FMarkupProgram::Compile(
		    {"cursor.vswml",
		        R"(<Widget Cursor="crosshair"><style>.hidden { cursor: none; }</style><Widget id="hidden" class="hidden" /></Widget>)"},
		    {}, Diagnostic);
		Require(Program != nullptr, Diagnostic.Format());
		auto Parsed = Program->Create({}, Diagnostic);
		FPropertyValue Value;
		Require(Parsed && Parsed->Get(EProperty::Cursor, Value, Error) &&
		        std::get<ECursor>(Value) == ECursor::Crosshair,
		    "ML cursor literal");
		auto Layout = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {100, 100, 1}, Error);
		Require(Layout && Layout->SetRoot(Parsed, Error) && Layout->Update(0, Error) &&
		        Parsed->GetChild(0, Error)->Get(EProperty::Cursor, Value, Error) &&
		        std::get<ECursor>(Value) == ECursor::None,
		    "CSS cursor property");
		Require(Layout->Close(Error), "close markup cursor context");
		Require(!FMarkupProgram::Compile({"invalid.vswml", "<Widget Cursor=\"unknown-cursor\" />"}, {}, Diagnostic),
		    "invalid cursor is rejected");
		std::cout << "GUI cursor, capture and window-boundary checks passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
