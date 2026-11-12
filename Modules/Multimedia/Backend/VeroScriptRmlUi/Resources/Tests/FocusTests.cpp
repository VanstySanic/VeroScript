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

	void Require(bool Value, const std::string &Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message + ": " + Error);
		}
	}

	std::shared_ptr<FWidget> Make(EControlKind Kind, double Height)
	{
		auto Widget = FWidget::Create();
		FControlOptions Options;
		Options.Kind = Kind;
		Require(
		    Widget->SetControlOptions(Options, Error) &&
		        Widget->Set(EProperty::Height, FLength{ELengthUnit::Pixel, Height}, Error) &&
		        Widget->Set(EProperty::Focusable,
		            Kind == EControlKind::Button || Kind == EControlKind::RadioButton || IsTextControl(Kind),
		            Error),
		    "create widget");
		return Widget;
	}

	FLayoutBox Box(const std::shared_ptr<FWidget> &Widget)
	{
		FLayoutBox Result;
		Require(Widget->GetLayout(Result, Error), "read geometry");
		return Result;
	}
} // namespace

int main()
{
	try
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 300, 1}, Error);
		Require(Context != nullptr, "create context");
		double Time = 0;
		const auto Update = [&]
		{
			Require(Context->Update(Time += .01, Error), "update");
		};
		const auto Send = [&](Platform::EEventType Type, Platform::EKey Key, const std::string &Text = "")
		{
			Platform::FEvent Input;
			Input.WindowId = 1;
			Input.Type = Type;
			Input.Key = Key;
			Input.Text = Text;
			FEventResult Result;
			Require(Context->ProcessEvent(Input, Result, Error), "send input");
		};
		auto Root = FWidget::Create();
		auto Button = Make(EControlKind::Button, 25);
		auto Number = Make(EControlKind::NumberBox, 25);
		auto NumberOptions = Number->GetControlOptions();
		NumberOptions.Maximum = 100;
		Require(Number->SetControlOptions(NumberOptions, Error) && Root->SetChildren({Button, Number}, Error) &&
		        Context->SetRoot(Root, Error),
		    "initial tree");
		Update();

		for (bool bStandard : {true, false})
		{
			const auto Throw = [bStandard](const FControlChange &, std::string &) -> bool
			{
				if (bStandard)
				{
					throw std::runtime_error("test.control_exception");
				}
				throw 7;
			};
			const auto Expected =
			    bStandard ? "gui.callback_exception: test.control_exception" : "gui.callback_exception";
			Require(Button->SetControlCallback(Throw, Error), "install activation callback");
			Require(!Button->Activate(Error) && Error == Expected, "direct activation translates callback exceptions");
			Require(Button->SetControlCallback({}, Error) && Button->Activate(Error), "activation recovers");
			Require(Number->FocusControl(Error) && Number->SetTextSelection(0, 1, Error), "focus numeric draft");
			Send(Platform::EEventType::TextInput, Platform::EKey::Unknown, bStandard ? "8" : "9");
			Require(Number->SetControlCallback(Throw, Error), "install numeric callback");
			Require(!Context->SetFocus(Button, Error) && Error == Expected && Context->GetFocus() == Number,
			    "direct focus commit translates callback exceptions without "
			    "losing focus");
			Require(Number->SetControlCallback({}, Error) && Context->SetFocus(Button, Error), "focus commit recovers");
			Update();
		}

		auto Outer = Make(EControlKind::ScrollView, 80), OuterContent = Make(EControlKind::None, 300);
		auto Inner = Make(EControlKind::ScrollView, 60), InnerContent = Make(EControlKind::None, 200);
		auto First = Make(EControlKind::Button, 20), Last = Make(EControlKind::Button, 20);
		Require(InnerContent->SetChildren({First, Make(EControlKind::None, 120), Last}, Error) &&
		        Inner->AddChild(InnerContent, Error) &&
		        OuterContent->SetChildren({Make(EControlKind::None, 120), Inner}, Error) &&
		        Outer->AddChild(OuterContent, Error) && Root->SetChildren({Outer}, Error),
		    "nested scroll tree");
		Update();
		Require(First->FocusControl(Error), "focus first scroll child");
		Update();
		Send(Platform::EEventType::KeyDown, Platform::EKey::Tab);
		Require(Context->GetFocus() == Last && Inner->GetControlOptions().ScrollY > 0 &&
		        Outer->GetControlOptions().ScrollY > 0,
		    "tab reveals the focused child through nested scroll views");
		Update();
		for (const auto &Scroll : {Inner, Outer})
		{
			const auto View = Box(Scroll).Content, Target = Box(Last).Border;
			Require(Target.Y >= View.Y - .1 && Target.Y + Target.Height <= View.Y + View.Height + .1,
			    "focused child is inside each completed scroll viewport");
		}
		const auto InnerEnd = Inner->GetControlOptions().ScrollY, OuterEnd = Outer->GetControlOptions().ScrollY;
		Require(First->FocusControl(Error) && Last->FocusControl(Error) &&
		        std::abs(Inner->GetControlOptions().ScrollY - InnerEnd) < .1 &&
		        std::abs(Outer->GetControlOptions().ScrollY - OuterEnd) < .1,
		    "queued focus changes use pending offsets without overscrolling");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Tab);
		Update();
		Require(Context->GetFocus() == First && std::abs(Inner->GetControlOptions().ScrollY) < .1,
		    "tab wrap reveals the preceding child");

		auto Popup = Make(EControlKind::Popup, 50), PopupButton = Make(EControlKind::Button, 25);
		auto PopupOptions = Popup->GetControlOptions();
		PopupOptions.bOpen = true;
		Require(Popup->SetControlOptions(PopupOptions, Error) && Popup->AddChild(PopupButton, Error) &&
		        First->AddChild(Popup, Error),
		    "floating scroll descendant");
		Update();
		const auto InnerOffset = Inner->GetControlOptions().ScrollY, OuterOffset = Outer->GetControlOptions().ScrollY;
		Require(PopupButton->FocusControl(Error), "focus portalled child");
		Require(Inner->GetControlOptions().ScrollY == InnerOffset && Outer->GetControlOptions().ScrollY == OuterOffset,
		    "portalled focus does not scroll logical ancestors");

		auto Horizontal = Make(EControlKind::ScrollView, 40), Content = Make(EControlKind::None, 30);
		auto WideButton = Make(EControlKind::Button, 25);
		Require(Horizontal->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 100}, Error) &&
		        Content->Set(EProperty::Layout, ELayout::Horizontal, Error) &&
		        Content->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 400}, Error) &&
		        WideButton->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 200}, Error),
		    "horizontal focus geometry");
		auto Space = Make(EControlKind::None, 25);
		Require(Space->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 90}, Error) &&
		        Content->SetChildren({Space, WideButton}, Error) && Horizontal->AddChild(Content, Error) &&
		        Root->SetChildren({Horizontal}, Error),
		    "horizontal focus tree");
		Update();
		Platform::FEvent Press;
		Press.WindowId = 1;
		Press.Type = Platform::EEventType::PointerDown;
		Press.Button = 1;
		Press.X = 95;
		Press.Y = 5;
		FEventResult PressResult;
		Require(Context->ProcessEvent(Press, PressResult, Error) && Context->GetFocus() == WideButton &&
		        Horizontal->GetControlOptions().ScrollX == 0,
		    "pointer focus keeps a partially visible target under the pointer");
		Update();
		Press.Type = Platform::EEventType::PointerUp;
		Require(Context->ProcessEvent(Press, PressResult, Error), "release pointer focus");
		Require(WideButton->FocusControl(Error) && Horizontal->GetControlOptions().ScrollX > 0,
		    "horizontal focus reveals an offscreen control");
		Update();
		const auto Offset = Horizontal->GetControlOptions().ScrollX;
		Require(WideButton->FocusControl(Error) && WideButton->FocusControl(Error) &&
		        Horizontal->GetControlOptions().ScrollX == Offset,
		    "an oversized focus target does not oscillate between viewport edges");
		Require(Horizontal->SetControlCallback([&](const FControlChange &, std::string &Failure)
		{
			return Root->SetChildren({}, Failure);
		}, Error),
		    "install scroll callback that removes its focus target");
		Require(Context->SetFocus({}, Error), "clear focus before detaching callback");
		auto HorizontalOptions = Horizontal->GetControlOptions();
		HorizontalOptions.ScrollX = 0;
		Require(Horizontal->SetControlOptions(HorizontalOptions, Error), "reset horizontal request");
		Update();
		Require(WideButton->FocusControl(Error) && !Context->GetFocus(),
		    "a scroll callback may remove the new focus target safely");
		auto ChoiceScroll = Make(EControlKind::ScrollView, 25), Group = Make(EControlKind::RadioGroup, 100);
		auto ChoiceA = Make(EControlKind::RadioButton, 20), ChoiceB = Make(EControlKind::RadioButton, 20);
		auto ChoiceOptions = ChoiceA->GetControlOptions();
		ChoiceOptions.ChoiceKey = "a";
		Require(ChoiceA->SetControlOptions(ChoiceOptions, Error), "first scroll choice");
		ChoiceOptions.ChoiceKey = "b";
		Require(ChoiceB->SetControlOptions(ChoiceOptions, Error), "second scroll choice");
		auto GroupOptions = Group->GetControlOptions();
		GroupOptions.Text = "a";
		Require(Group->SetControlOptions(GroupOptions, Error) &&
		        Group->SetChildren({ChoiceA, Make(EControlKind::None, 60), ChoiceB}, Error) &&
		        ChoiceScroll->AddChild(Group, Error) && Root->SetChildren({ChoiceScroll}, Error),
		    "radio focus scroll tree");
		Update();
		Require(ChoiceA->FocusControl(Error) &&
		        ChoiceScroll->SetControlCallback([&](const FControlChange &, std::string &Failure)
		{
			return Group->SetChildren({ChoiceA}, Failure);
		}, Error),
		    "install scroll callback that removes the next choice");
		Send(Platform::EEventType::KeyDown, Platform::EKey::Right);
		Require(!Context->GetFocus() && Group->GetControlOptions().Text == "a",
		    "radio navigation revalidates its focus target after scroll callbacks");
		auto HoverScroll = Make(EControlKind::ScrollView, 25);
		auto HoverFirst = Make(EControlKind::Button, 20), HoverLast = Make(EControlKind::Button, 20);
		auto FirstTip = Make(EControlKind::ToolTip, 20), LastTip = Make(EControlKind::ToolTip, 20);
		auto TipOptions = FirstTip->GetControlOptions();
		TipOptions.Delay = 0;
		Require(FirstTip->SetControlOptions(TipOptions, Error) && LastTip->SetControlOptions(TipOptions, Error) &&
		        HoverFirst->AddChild(FirstTip, Error) && HoverLast->AddChild(LastTip, Error) &&
		        HoverScroll->SetChildren({HoverFirst, Make(EControlKind::None, 80), HoverLast}, Error) &&
		        Root->SetChildren({HoverScroll}, Error),
		    "hover scroll tree");
		Update();
		Send(Platform::EEventType::InputStateReset, Platform::EKey::Unknown);
		Press.Type = Platform::EEventType::PointerMove;
		Press.DeviceId = 31;
		Press.X = 5;
		Press.Y = 10;
		Require(Context->ProcessEvent(Press, PressResult, Error), "hover first control");
		Update();
		Require(FirstTip->GetControlOptions().bOpen, "first tooltip opens");
		Require(HoverLast->FocusControl(Error), "scroll under a stationary pointer");
		Update();
		Update();
		Require(!FirstTip->GetControlOptions().bOpen && LastTip->GetControlOptions().bOpen,
		    "layout changes re-hit-test a stationary pointer before refreshing tooltips");
		Send(Platform::EEventType::InputStateReset, Platform::EKey::Unknown);
		Update();
		Update();
		Require(!FirstTip->GetControlOptions().bOpen && !LastTip->GetControlOptions().bOpen,
		    "input reset does not resurrect stationary hover");
		Require(Context->ProcessEvent(Press, PressResult, Error), "restore stationary pointer");
		Update();
		Platform::FEvent Removed;
		Removed.Type = Platform::EEventType::InputDeviceRemoved;
		Removed.DeviceKind = Platform::EInputDeviceKind::Mouse;
		Removed.DeviceId = Press.DeviceId;
		Require(Context->ProcessEvent(Removed, PressResult, Error), "remove stationary pointer device");
		Update();
		Update();
		Require(!LastTip->GetControlOptions().bOpen, "device removal discards its retained mouse position");
		Require(Context->ProcessEvent(Press, PressResult, Error), "restore pointer after device removal");
		Update();
		Press.Type = Platform::EEventType::PointerCancelled;
		Require(Context->ProcessEvent(Press, PressResult, Error), "cancel stationary pointer");
		Update();
		Update();
		Require(!LastTip->GetControlOptions().bOpen, "pointer cancellation does not resurrect stationary hover");
		Require(Context->Close(Error), "close context");
		std::cout << "GUI focus visibility and direct callback exception checks passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
