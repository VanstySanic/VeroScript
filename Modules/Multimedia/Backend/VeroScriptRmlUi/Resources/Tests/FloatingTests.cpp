// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiTestResources.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace Gui;
	std::string Error;
	void Require(bool Value, const char* Why)
	{
		if (!Value)
		{
			throw std::runtime_error(std::string(Why) + ": " + Error);
		}
	}
	auto Make(EControlKind Kind, double Width, double Height)
	{
		auto Widget = FWidget::Create();
		FControlOptions Options;
		Options.Kind = Kind;
		Require(Widget->SetControlOptions(Options, Error) &&
		        Widget->Set(EProperty::Width, FLength{ELengthUnit::Pixel, Width}, Error) &&
		        Widget->Set(EProperty::Height, FLength{ELengthUnit::Pixel, Height}, Error) &&
		        Widget->Set(EProperty::Focusable, Kind == EControlKind::Button, Error),
		    "widget");
		return Widget;
	}
	FLayoutBox Box(const std::shared_ptr<FWidget>& Widget)
	{
		FLayoutBox Result;
		Require(Widget->GetLayout(Result, Error), "geometry");
		return Result;
	}
}

int main()
{
	try
	{
		using Platform::EEventType;
		using Platform::EKey;
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 400, 1}, Error);
		Require(Context != nullptr, "context");
		double Time = 0;
		const auto Update = [&]
		{
			Require(Context->Update(Time += .01, Error), "update");
		};
		const auto Send = [&](EEventType Type, EKey Key = EKey::Unknown, double X = 2, double Y = 2)
		{
			Platform::FEvent Input;
			Input.Type = Type;
			Input.WindowId = 1;
			Input.Key = Key;
			Input.X = X;
			Input.Y = Y;
			Input.Button = 1;
			FEventResult Result;
			Require(Context->ProcessEvent(Input, Result, Error), "input");
			return Result;
		};
		auto Root = FWidget::Create();
		auto Background = Make(EControlKind::Button, 100, 28);
		auto First = Make(EControlKind::Popup, 200, 120), Second = Make(EControlKind::Popup, 120, 80);
		auto A = Make(EControlKind::Button, 80, 28), B = Make(EControlKind::Button, 80, 28);
		auto C = Make(EControlKind::Button, 80, 28);
		auto O = First->GetControlOptions();
		O.bModal = true;
		O.bDismissOnOutside = false;
		O.Placement = FControlOptions::EPlacement::Center;
		Require(First->SetControlOptions(O, Error) && Second->SetControlOptions(O, Error) &&
		        First->Set(EProperty::Focusable, true, Error) &&
		        First->SetChildren({A, B}, Error) && Second->AddChild(C, Error) &&
		        Root->SetChildren({Background, First, Second}, Error) && Context->SetRoot(Root, Error),
		    "tree");
		Update();
		Require(Context->SetFocus(Background, Error), "initial focus");
		O.bOpen = true;
		Require(First->SetControlOptions(O, Error), "open first");
		Update();
		Require(std::abs(Box(First).Border.X - 100) < .1 && std::abs(Box(First).Border.Y - 140) < .1,
		    "centered modal");
		Require(Context->GetFocus() == A, "modal initial focus");
		Require(!Context->SetFocus(Background, Error), "modal refuses background focus");
		Error.clear();
		Require(Send(EEventType::PointerDown).bHandled && First->GetControlOptions().bOpen, "outside modal click consumed");
		Send(EEventType::PointerUp);
		Send(EEventType::KeyDown, EKey::Tab);
		Require(Context->GetFocus() == B, "modal tab order");
		Send(EEventType::KeyDown, EKey::Tab);
		Require(Context->GetFocus() == A, "modal focus wraps");
		Require(Second->SetControlOptions(O, Error), "second modal");
		Update();
		Require(Context->GetFocus() == C && !Context->SetFocus(A, Error), "newest modal wins");
		Error.clear();
		Send(EEventType::KeyDown, EKey::Escape);
		Update();
		Require(!Second->GetControlOptions().bOpen && First->GetControlOptions().bOpen && Context->GetFocus() == A,
		    "escape closes only top modal and restores focus");
		// Reopening an earlier declaration must move it above a later declaration.
		O = Second->GetControlOptions();
		O.bOpen = true;
		Require(Second->SetControlOptions(O, Error), "reopen second");
		Update();
		O = First->GetControlOptions();
		O.bOpen = false;
		Require(First->SetControlOptions(O, Error), "close underlying first");
		Update();
		O.bOpen = true;
		Require(First->SetControlOptions(O, Error), "reopen earlier declaration");
		Update();
		int Clicks = 0;
		Require(A->SetControlCallback([&](const FControlChange&, std::string&)
		{
			++Clicks;
			return true;
		}, Error),
		    "click callback");
		Send(EEventType::PointerDown, EKey::Unknown, 105, 145);
		Send(EEventType::PointerUp, EKey::Unknown, 105, 145);
		Require(Context->GetFocus() == A && Clicks == 1, "modal draw and hit order after reopen");
		O.bDismissOnEscape = false;
		Require(First->SetControlOptions(O, Error), "escape policy");
		Send(EEventType::KeyDown, EKey::Escape);
		Update();
		Require(First->GetControlOptions().bOpen && Second->GetControlOptions().bOpen, "non-dismissible modal blocks lower escape");
		O.bOpen = false;
		Require(First->SetControlOptions(O, Error), "close first");
		Update();
		O = Second->GetControlOptions();
		O.bOpen = false;
		Require(Second->SetControlOptions(O, Error), "close second");
		Update();
		Require(Context->GetFocus() == Background, "modal chain restores background focus");
		O = First->GetControlOptions();
		O.bOpen = true;
		Require(First->SetControlOptions(O, Error), "open for focus reset");
		Update();
		Send(EEventType::FocusLost);
		Update();
		Require(!Context->GetFocus(), "unfocused window does not reacquire modal focus");
		Send(EEventType::FocusGained);
		Update();
		Require(Context->GetFocus() == A, "window activation restores modal focus");
		Require(Root->RemoveChild(First, Error), "detach open modal");
		Update();
		Require(Context->GetFocus() == Background, "detached modal restores focus");
		Require(Root->AddChild(First, Error), "reattach modal");
		Update();
		Require(Context->GetFocus() == A, "reattached modal focus");
		Require(First->Dispose(Error), "dispose open modal");
		Update();
		Require(Context->GetFocus() == Background, "disposed modal restores focus");
		// Content suppression does not replace a visibility model.
		Require(C->SetContentVisible(false, Error) && C->Set(EProperty::Visibility, EVisibility::Visible, Error), "suppress content");
		Require(!C->IsContentVisible(), "visibility remains separately suppressed");
		Require(C->SetContentVisible(true, Error), "restore content");
		// Popup remains non-modal and anchor sized by default only when requested.
		auto Popup = Make(EControlKind::Popup, 70, 40);
		O = Popup->GetControlOptions();
		O.bOpen = true;
		O.bMatchAnchorWidth = true;
		Require(Popup->SetControlOptions(O, Error) && Background->AddChild(Popup, Error), "anchored popup");
		Update();
		Require(std::abs(Box(Popup).Border.Width - 100) < .1 && Context->SetFocus(Background, Error), "match width without modality");
		O.bMatchAnchorWidth = false;
		O.Placement = FControlOptions::EPlacement::Right;
		Require(Popup->SetControlOptions(O, Error), "right placement");
		Update();
		Require(std::abs(Box(Popup).Border.Width - 70) < .1 && std::abs(Box(Popup).Border.X - 100) < .1,
		    "restore authored width and right placement");
		auto Observed = FWidget::Create();
		Require(Root->AddChild(Observed, Error), "observed container");
		int Notifications = 0;
		Require(Observed->SetChildrenChangedCallback([&](bool Retry, std::string& Failure)
		{
			++Notifications;
			if (Notifications == 1)
			{
				Failure = "retry child notification";
				return false;
			}
			Require((Notifications == 2) == Retry, "child retry flag");
			return true;
		}, Error),
		    "child observer");
		auto Child = FWidget::Create();
		Require(!Observed->AddChild(Child, Error) && Error == "retry child notification" &&
		        Observed->GetChildCount() == 1,
		    "child callback failure keeps committed ownership");
		Error.clear();
		Update();
		Require(Notifications == 2, "deferred child callback retry");
		Require(Child->Dispose(Error) && Observed->GetChildCount() == 0 && Notifications == 3,
		    "native disposal notifies the owning container");
		auto Moved = FWidget::Create();
		auto NextContext = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 400, 1}, Error);
		Require(NextContext && Root->AddChild(Moved, Error), "movable observed container");
		int MovedNotifications = 0;
		Require(Moved->SetChildrenChangedCallback([&](bool Retry, std::string& Failure)
		{
			++MovedNotifications;
			if (!Retry)
			{
				Failure = "retry in new context";
				return false;
			}
			return true;
		}, Error),
		    "moved child observer");
		Require(!Moved->AddChild(FWidget::Create(), Error), "queue callback before moving context");
		Error.clear();
		Require(Root->RemoveChild(Moved, Error) && NextContext->SetRoot(Moved, Error) &&
		        NextContext->Update(0, Error) && MovedNotifications == 2,
		    "new context owns the pending retry");
		Update();
		Require(MovedNotifications == 2 && NextContext->Close(Error), "stale context does not repeat notification");
		auto Outer = Make(EControlKind::Popup, 200, 150), Inner = Make(EControlKind::Popup, 100, 100);
		auto OuterButton = Make(EControlKind::Button, 80, 28), InnerButton = Make(EControlKind::Button, 80, 28);
		O = Outer->GetControlOptions();
		O.bModal = true;
		O.Placement = FControlOptions::EPlacement::Center;
		Require(Outer->SetControlOptions(O, Error) && Inner->SetControlOptions(O, Error) &&
		        Inner->AddChild(InnerButton, Error) && Outer->SetChildren({OuterButton, Inner}, Error) &&
		        Root->SetChildren({Background, Outer}, Error),
		    "nested modal tree");
		Update();
		Require(Context->SetFocus(Background, Error) && Outer->SetFloatingOpen(true, Error), "open outer");
		Update();
		Require(Inner->SetFloatingOpen(true, Error), "open inner");
		Update();
		Require(Context->GetFocus() == InnerButton, "nested modal focus");
		Require(Outer->SetFloatingOpen(false, Error), "close outer with open child");
		Update();
		Require(Outer->SetFloatingOpen(true, Error), "reopen parent modal");
		Update();
		Require(Context->GetFocus() == InnerButton, "parent reopen preserves child priority");
		Require(Inner->SetFloatingOpen(false, Error), "close inner");
		Update();
		Require(Context->GetFocus() == OuterButton, "closing nested modal returns to parent");
		Require(Outer->SetFloatingOpen(false, Error), "close outer");
		Update();
		auto Late = Make(EControlKind::Popup, 100, 100);
		O.bOpen = true;
		Require(Late->SetControlOptions(O, Error) && Root->AddChild(Late, Error), "mount open modal before layout");
		int BackgroundClicks = 0;
		Require(Background->SetControlCallback([&](const FControlChange&, std::string&)
		{
			++BackgroundClicks;
			return true;
		}, Error),
		    "background callback");
		Send(EEventType::PointerDown);
		Send(EEventType::PointerUp);
		Require(BackgroundClicks == 0, "new modal prevents background click before its first layout");
		Update();
		Require(Context->Close(Error), "close");
		std::cout << "Modal focus, stacking, dismissal, suppression and popup placement passed\n";
		return 0;
	}
	catch (const std::exception& Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
