// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiTestResources.h"
#include "VeroScriptRmlUi.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
	using namespace VeroScript;
	using namespace Gui;
	void Require(bool V, const std::string &Why)
	{
		if (!V)
		{
			throw std::runtime_error(Why);
		}
	}
	std::string Error;
	std::shared_ptr<FWidget> Make(EControlKind Kind, double Height = 32)
	{
		auto W = FWidget::Create();
		FControlOptions O;
		O.Kind = Kind;
		Require(W->SetControlOptions(O, Error) &&
		        W->Set(EProperty::Height, FLength{ELengthUnit::Pixel, Height}, Error) &&
		        W->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 200}, Error) &&
		        W->Set(EProperty::Focusable, true, Error),
		    Error);
		return W;
	}
} // namespace

int main()
{
	try
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 600, 1}, Error);
		Require(Context != nullptr, Error);
		auto Root = FWidget::Create();
		auto Button = Make(EControlKind::Button);
		auto Label = FWidget::Create();
		Require(Label->Set(EProperty::Text, std::string("Child label"), Error) && Button->AddChild(Label, Error),
		    Error);
		auto Check = Make(EControlKind::CheckBox);
		auto Slider = Make(EControlKind::Slider);
		auto Scroll = Make(EControlKind::ScrollView, 60);
		auto Tall = Make(EControlKind::None, 240);
		Require(Scroll->AddChild(Tall, Error), Error);
		auto Text = Make(EControlKind::TextBox);
		auto Area = Make(EControlKind::TextArea, 80);
		Require(Root->SetChildren({Button, Check, Slider, Scroll, Text, Area}, Error) && Context->SetRoot(Root, Error),
		    Error);
		double Time = 0;
		const auto Update = [&]
		{
			Require(Context->Update(Time += .01, Error), Error);
		};
		Update();
		int Clicks = 0, Checks = 0, TextChanges = 0;
		Require(Button->SetControlCallback(
		            [&](const auto &C, auto &)
		{
			if (C.Action == EControlAction::Clicked)
			{
				++Clicks;
			}
			return true;
		},
		            Error),
		    Error);
		Require(Check->SetControlCallback(
		            [&](const auto &C, auto &)
		{
			if (C.Action == EControlAction::CheckedChanged)
			{
				++Checks;
			}
			return true;
		},
		            Error),
		    Error);
		Require(Text->SetControlCallback(
		            [&](const auto &C, auto &)
		{
			if (C.Action == EControlAction::TextChanged)
			{
				++TextChanges;
			}
			return true;
		},
		            Error),
		    Error);
		const auto Send = [&](Platform::FEvent I)
		{
			FEventResult Result;
			Require(Context->ProcessEvent(I, Result, Error),
			    "event " + std::to_string(static_cast<int>(I.Type)) + ": " + Error);
			return Result;
		};
		const auto Pointer = [&](const auto &W, Platform::EEventType Type, double XOffset = 5, std::uint64_t Device = 0)
		{
			FLayoutBox B;
			Require(W->GetLayout(B, Error), Error);
			Platform::FEvent I;
			I.WindowId = 1;
			I.Type = Type;
			I.X = B.Content.X + XOffset;
			I.Y = B.Content.Y + 5;
			I.Button = 1;
			I.DeviceId = Device;
			return I;
		};
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		Send(Pointer(Button, Platform::EEventType::PointerUp));
		Require(Clicks == 1, "child click did not activate its button");
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		Send(Pointer(Button, Platform::EEventType::PointerUp, 350));
		Require(Clicks == 1, "release outside activated button");
		auto Cancel =
		    Button->Subscribe(Platform::EEventType::PointerDown, false, [](auto &E)
		{
			E.PreventDefault();
		}, Error);
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		Send(Pointer(Button, Platform::EEventType::PointerUp));
		Require(Clicks == 1 && Cancel.Disconnect(Error), "preventDefault did not cancel activation");
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		Send(Pointer(Check, Platform::EEventType::PointerDown, 5, 2));
		Send(Pointer(Check, Platform::EEventType::PointerUp, 5, 2));
		Send(Pointer(Button, Platform::EEventType::PointerUp));
		Require(Clicks == 2 && Checks == 1 && Check->GetControlOptions().bChecked,
		    "independent pointer presses interfered");
		Require(Context->SetFocus(Button, Error), Error);
		Platform::FEvent Key;
		Key.WindowId = 1;
		Key.Type = Platform::EEventType::KeyDown;
		Key.Key = Platform::EKey::Space;
		Send(Key);
		Key.bRepeat = true;
		Send(Key);
		Key.Type = Platform::EEventType::KeyUp;
		Key.bRepeat = false;
		Send(Key);
		Require(Clicks == 3, "keyboard activation or repeat handling");
		Key.Type = Platform::EEventType::KeyDown;
		Key.Key = Platform::EKey::Tab;
		Key.bRepeat = false;
		Send(Key);
		Require(Context->GetFocus() == Check, "Tab navigation order");
		Send(Pointer(Slider, Platform::EEventType::PointerDown, 150));
		Send(Pointer(Slider, Platform::EEventType::PointerUp, 150));
		Require(std::abs(Slider->GetControlOptions().Value - .76) < .011, "slider position mapping");
		Key.Key = Platform::EKey::Home;
		Send(Key);
		Require(Slider->GetControlOptions().Value == 0, "range Home");
		Key.Key = Platform::EKey::End;
		Send(Key);
		Require(Slider->GetControlOptions().Value == 1, "range End");
		auto Uneven = Slider->GetControlOptions();
		Uneven.Maximum = .9;
		Uneven.Step = .4;
		Uneven.Value = 0;
		Require(Slider->SetControlOptions(Uneven, Error), Error);
		Send(Key);
		Require(Slider->GetControlOptions().Value == .9, "range End did not reach an unaligned maximum");
		Uneven.Minimum = std::numeric_limits<double>::max() / 4;
		Uneven.Maximum = std::numeric_limits<double>::max() / 2;
		Uneven.Value = Uneven.Minimum;
		Uneven.Step = std::numeric_limits<double>::max();
		Require(Slider->SetControlOptions(Uneven, Error), Error);
		Key.Key = Platform::EKey::Right;
		Send(Key);
		Require(Slider->GetControlOptions().Value == Uneven.Maximum, "range increment overflowed");
		auto RangeOptions = Slider->GetControlOptions();
		RangeOptions.Kind = EControlKind::ScrollBar;
		RangeOptions.Minimum = 0;
		RangeOptions.Maximum = 1;
		RangeOptions.Step = .01;
		RangeOptions.Value = .5;
		RangeOptions.PageSize = .2;
		Require(Slider->SetControlOptions(RangeOptions, Error), Error);
		Update();
		Key.Key = Platform::EKey::PageDown;
		Send(Key);
		Require(std::abs(Slider->GetControlOptions().Value - .7) < 1e-9, "scrollbar PageDown direction");
		Key.Key = Platform::EKey::PageUp;
		Send(Key);
		Require(std::abs(Slider->GetControlOptions().Value - .5) < 1e-9, "scrollbar PageUp direction");
		RangeOptions.Kind = EControlKind::Slider;
		Require(Slider->SetControlOptions(RangeOptions, Error), Error);
		auto Wheel = Pointer(Scroll, Platform::EEventType::Wheel);
		Wheel.DeltaY = -1;
		Send(Wheel);
		Update();
		FLayoutBox ScrollBox, TallBox;
		Require(Scroll->GetLayout(ScrollBox, Error) && Tall->GetLayout(TallBox, Error), Error);
		Require(ScrollBox.ScrollY == 40 && TallBox.Border.Y < ScrollBox.Border.Y, "scroll geometry not applied");
		auto Touch = Pointer(Scroll, Platform::EEventType::PointerDown, 50);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 3;
		Touch.Button = 0;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerMove;
		Touch.Y -= 20;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerUp;
		Send(Touch);
		Update();
		Require(Scroll->GetControlOptions().ScrollY == 60, "touch panning or touch release");
		Touch = Pointer(Button, Platform::EEventType::PointerDown);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 4;
		Touch.Button = 0;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerUp;
		Send(Touch);
		Require(Clicks == 4, "touch button activation");
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		Require(Button->Set(EProperty::Enabled, false, Error), Error);
		Send(Pointer(Button, Platform::EEventType::PointerUp));
		Require(Clicks == 4 && Button->Set(EProperty::Enabled, true, Error), "disabled press activated");
		Update();
		Send(Pointer(Button, Platform::EEventType::PointerDown));
		auto Secondary = Pointer(Button, Platform::EEventType::PointerDown);
		Secondary.Button = 2;
		Send(Secondary);
		Secondary.Type = Platform::EEventType::PointerUp;
		Send(Secondary);
		Send(Pointer(Button, Platform::EEventType::PointerUp));
		Require(Clicks == 5, "secondary button release cancelled the primary press");
		std::string Clipboard;
		bool InputEnabled = false;
		int AreaUpdates = 0;
		FRect InputArea;
		std::int64_t InputCursorOffset = 0;
		FTextInputServices Services;
		Services.ReadClipboard = [&](auto &Value, auto &)
		{
			Value = Clipboard;
			return true;
		};
		Services.WriteClipboard = [&](const auto &Value, auto &)
		{
			Clipboard = Value;
			return true;
		};
		Services.SetInputArea = [&](bool Enabled, const auto &Rect, auto Cursor, auto &)
		{
			InputEnabled = Enabled;
			InputArea = Rect;
			InputCursorOffset = Cursor;
			++AreaUpdates;
			return true;
		};
		Require(Context->SetTextInputServices(Services, Error) && Context->SetFocus(Text, Error), Error);
		Platform::FEvent Input;
		Input.WindowId = 1;
		Input.Type = Platform::EEventType::TextInput;
		Input.Text = "a\xc3\xa9\xf0\x9f\x98\x80";
		Send(Input);
		Require(Text->GetControlOptions().Text == Input.Text && TextChanges == 1 && InputEnabled && AreaUpdates > 0,
		    "UTF-8 text input or platform services");
		Require(Text->SetTextSelection(1, 2, Error), Error);
		Secondary = Pointer(Text, Platform::EEventType::PointerDown, 150);
		Secondary.Button = 2;
		Send(Secondary);
		Secondary.Type = Platform::EEventType::PointerUp;
		Send(Secondary);
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Send(Key);
		Require(Clipboard == "\xc3\xa9", "copy selection counted bytes");
		Key.Key = Platform::EKey::X;
		Send(Key);
		Require(Text->GetControlOptions().Text == "a\xf0\x9f\x98\x80", "cut selection");
		Key.Key = Platform::EKey::Z;
		Send(Key);
		Require(Text->GetControlOptions().Text == Input.Text, "undo");
		Key.Key = Platform::EKey::Y;
		Send(Key);
		Require(Text->GetControlOptions().Text == "a\xf0\x9f\x98\x80", "redo");
		Key.Key = Platform::EKey::V;
		Send(Key);
		Require(Text->GetControlOptions().Text == Input.Text, "paste");
		Update();
		Require(Context->SetFocus(Area, Error) && Text->SetTextSelection(0, 1, Error), Error);
		auto BeforeFocus = Make(EControlKind::None, 1);
		Require(Root->AddChild(BeforeFocus, Error), Error);
		Update();
		Require(Context->SetFocus(Text, Error), Error);
		Clipboard.clear();
		Key.Key = Platform::EKey::C;
		Send(Key);
		Require(Clipboard == "a", "unfocused selection was not restored on focus after rebuilding");
		Require(Text->SetTextSelection(0, 1, Error), Error);
		const auto Original = Text->GetControlOptions().Text;
		Platform::FEvent Compose;
		Compose.WindowId = 1;
		Compose.Type = Platform::EEventType::TextEditing;
		Compose.Text = "xy";
		Compose.SelectionStart = 1;
		Compose.SelectionLength = 0;
		Send(Compose);
		Update();
		Require(Text->GetControlOptions().Text == Original, "preedit was committed to the property");
		Require(!Text->SetTextSelection(0, 4, Error), "selection accepted an offset in uncommitted preedit text");
		Require(Text->SetTextSelection(0, 3, Error) && Text->SetTextSelection(0, 1, Error), Error);
		Require(Text->SetTextSelection(0, 3, Error), Error);
		Compose.Text = "x";
		Send(Compose);
		Require(Text->SetTextSelection(0, 3, Error) && Text->SetTextSelection(0, 1, Error), Error);
		Compose.Text = "xy";
		Send(Compose);
		Key.Key = Platform::EKey::Enter;
		Key.Modifiers = 0;
		Send(Key);
		Key.Key = Platform::EKey::Tab;
		Send(Key);
		Require(Context->GetFocus() == Text && Text->GetControlOptions().Text == Original,
		    "IME Enter/Tab changed document or focus");
		auto RejectCommit =
		    Text->Subscribe(Platform::EEventType::TextInput, false, [](auto &E)
		{
			E.PreventDefault();
		}, Error);
		Input.Text = "rejected";
		Send(Input);
		Require(Text->GetControlOptions().Text == Original && RejectCommit.Disconnect(Error),
		    "rejected IME commit changed the property");
		Send(Compose);
		Input.Text = "b";
		Send(Input);
		Require(Text->GetControlOptions().Text == "b\xc3\xa9\xf0\x9f\x98\x80", "IME commit replaced the wrong range");
		Update();
		auto Extra = Make(EControlKind::None, 1);
		Require(Root->AddChild(Extra, Error), Error);
		Update();
		Input.Text = "c";
		Send(Input);
		Require(Text->GetControlOptions().Text == "bc\xc3\xa9\xf0\x9f\x98\x80",
		    "tree rebuild lost editor selection: " + Text->GetControlOptions().Text);
		Require(Text->SetTextSelection(0, 1, Error), Error);
		FTextInputServices Failing = Services;
		Failing.WriteClipboard = [](const auto &, auto &Failure)
		{
			Failure = "test.clipboard_failure";
			return false;
		};
		Require(Context->SetTextInputServices(Failing, Error), Error);
		Require(Text->SetTextSelection(0, 1, Error), Error);
		Key.Key = Platform::EKey::X;
		Key.Modifiers = 2;
		FEventResult Failed;
		const auto BeforeFailure = Text->GetControlOptions().Text;
		Require(!Context->ProcessEvent(Key, Failed, Error) && Error == "test.clipboard_failure" &&
		        Text->GetControlOptions().Text == BeforeFailure,
		    "clipboard failure was accepted");
		Require(Context->SetTextInputServices(Services, Error), Error);
		auto Options = Text->GetControlOptions();
		Options.bReadOnly = true;
		Require(Text->SetControlOptions(Options, Error), Error);
		Update();
		Input.Text = "blocked";
		Send(Input);
		Require(Text->GetControlOptions().Text == Options.Text && !InputEnabled, "read-only edit accepted");
		Require(Context->SetFocus(Area, Error), Error);
		Input.Text = "one\ntwo";
		Send(Input);
		Require(Area->GetControlOptions().Text == Input.Text, "multiline input");
		auto Border = Make(EControlKind::None);
		Require(Border->Set(EProperty::BorderWidth, 2.0, Error) && Border->Set(EProperty::CornerRadius, 8.0, Error) &&
		        Border->Set(EProperty::Padding, FInsets{4, 4, 4, 4}, Error) && Root->AddChild(Border, Error),
		    Error);
		Update();
		FLayoutBox BorderBox;
		Require(Border->GetLayout(BorderBox, Error), Error);
		Require(std::abs(BorderBox.Content.X - BorderBox.Border.X - 6) < .01 &&
		        std::abs(BorderBox.Content.Width - 188) < .01,
		    "border did not participate in layout");
		auto NestedText = Make(EControlKind::TextBox);
		auto EditorChild = Make(EControlKind::None, 8);
		auto NestedOptions = NestedText->GetControlOptions();
		NestedOptions.Text = "abc";
		auto ScrollOptions = Scroll->GetControlOptions();
		ScrollOptions.ScrollY = 0;
		Require(NestedText->SetControlOptions(NestedOptions, Error) && NestedText->AddChild(EditorChild, Error) &&
		        Tall->AddChild(NestedText, Error) && Scroll->SetControlOptions(ScrollOptions, Error),
		    Error);
		Update();
		FLayoutBox EditorBox, ChildBox;
		Require(NestedText->GetLayout(EditorBox, Error) && EditorChild->GetLayout(ChildBox, Error), Error);
		Require(ChildBox.Border.Y + ChildBox.Border.Height <= EditorBox.Content.Y + EditorBox.Content.Height + .01,
		    "text editor displaced authored children outside its content area");
		Touch = Pointer(NestedText, Platform::EEventType::PointerDown, 10);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 5;
		Touch.Button = 0;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerMove;
		Touch.Y -= 20;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerUp;
		Send(Touch);
		Require(Scroll->SetControlOptions(ScrollOptions, Error), Error);
		Update();
		Require(NestedText->SetTextSelection(0, 0, Error), Error);
		Send(Pointer(NestedText, Platform::EEventType::PointerMove, 180));
		Input.Text = "z";
		Send(Input);
		Require(NestedText->GetControlOptions().Text == "zabc", "touch scrolling left the text selection dragging");
		auto NestedSlider = Make(EControlKind::Slider, 24);
		Require(Tall->AddChild(NestedSlider, Error) && Scroll->SetControlOptions(ScrollOptions, Error), Error);
		Update();
		Touch = Pointer(NestedSlider, Platform::EEventType::PointerDown, 25);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 6;
		Touch.Button = 0;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerMove;
		Touch.X += 150;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerUp;
		Send(Touch);
		Require(NestedSlider->GetControlOptions().Value > .8 && Scroll->GetControlOptions().ScrollY == 0,
		    "scroll container stole the slider touch gesture");
		NestedOptions = NestedText->GetControlOptions();
		NestedOptions.Text = "reset";
		Require(NestedText->SetControlOptions(NestedOptions, Error) &&
		        Root->AddChild(Make(EControlKind::None, 1), Error),
		    Error);
		Update();
		Require(Context->SetFocus(NestedText, Error), Error);
		Key.Key = Platform::EKey::Z;
		Key.Modifiers = 2;
		Send(Key);
		Require(NestedText->GetControlOptions().Text == "reset", "rebuild retained undo history after replacing text");
		NestedOptions.Kind = EControlKind::TextArea;
		NestedOptions.Text = "multi\nline";
		Require(NestedText->SetControlOptions(NestedOptions, Error), Error);
		Update();
		Require(NestedText->SetTextSelection(10, 10, Error), Error);
		Input.Text = "\nmore";
		Send(Input);
		NestedOptions = NestedText->GetControlOptions();
		NestedOptions.Kind = EControlKind::TextBox;
		Require(NestedText->SetControlOptions(NestedOptions, Error), Error);
		Update();
		Send(Key);
		Require(NestedText->GetControlOptions().Text == "multilinemore",
		    "editor kind change restored multiline history");
		auto Padded = Make(EControlKind::ScrollView, 60);
		auto Small = Make(EControlKind::None, 20);
		Require(Padded->Set(EProperty::Padding, FInsets{10, 10, 10, 10}, Error) &&
		        Small->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 150}, Error) &&
		        Padded->AddChild(Small, Error) && Root->AddChild(Padded, Error),
		    Error);
		Update();
		FLayoutBox PaddedBox;
		Require(Padded->GetLayout(PaddedBox, Error), Error);
		Require(std::abs(PaddedBox.ScrollWidth - PaddedBox.Content.Width) < .01 &&
		        std::abs(PaddedBox.ScrollHeight - PaddedBox.Content.Height) < .01,
		    "padding was counted as scrollable overflow");
		Require(Scroll->Set(EProperty::Padding, FInsets{10, 10, 10, 10}, Error) && Context->SetFocus(Scroll, Error),
		    Error);
		Update();
		Key.Key = Platform::EKey::End;
		Key.Modifiers = 0;
		Send(Key);
		Update();
		Require(Scroll->GetLayout(ScrollBox, Error), Error);
		Require(std::abs(ScrollBox.ScrollY - Scroll->GetControlOptions().ScrollY) < .01,
		    "padded scrolling used a different range from the backend");
		auto OversizedScroll = Scroll->GetControlOptions();
		OversizedScroll.ScrollX = 1000000;
		OversizedScroll.ScrollY = 1000000;
		Require(Scroll->SetControlOptions(OversizedScroll, Error), Error);
		Update();
		Require(Scroll->GetLayout(ScrollBox, Error), Error);
		Wheel = Pointer(Scroll, Platform::EEventType::Wheel);
		Wheel.DeltaX = .25;
		Wheel.DeltaY = 1;
		Send(Wheel);
		Require(ScrollBox.ScrollX > 0 &&
		        std::abs(Scroll->GetControlOptions().ScrollX - std::max(0.0, ScrollBox.ScrollX - 10)) < .01 &&
		        std::abs(Scroll->GetControlOptions().ScrollY - std::max(0.0, ScrollBox.ScrollY - 40)) < .01,
		    "reverse wheel input started from an unclamped scroll request");
		Send(Wheel);
		Require(std::abs(Scroll->GetControlOptions().ScrollY - std::max(0.0, ScrollBox.ScrollY - 80)) < .01,
		    "queued wheel inputs did not accumulate");
		Require(Scroll->SetControlOptions(OversizedScroll, Error), Error);
		Update();
		Key.Key = Platform::EKey::Up;
		Send(Key);
		Require(std::abs(Scroll->GetControlOptions().ScrollY - std::max(0.0, ScrollBox.ScrollY - 40)) < .01,
		    "reverse keyboard input started from an unclamped scroll request");
		Require(Scroll->SetControlOptions(OversizedScroll, Error), Error);
		Update();
		Touch = Pointer(Scroll, Platform::EEventType::PointerDown, 50);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 7;
		Touch.Button = 0;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerMove;
		Touch.Y += 20;
		Send(Touch);
		Touch.Type = Platform::EEventType::PointerUp;
		Send(Touch);
		Require(std::abs(Scroll->GetControlOptions().ScrollY - std::max(0.0, ScrollBox.ScrollY - 20)) < .01,
		    "reverse touch input started from an unclamped scroll request");
		auto OuterScroll = Make(EControlKind::ScrollView, 60);
		auto OuterContent = Make(EControlKind::None, 240);
		auto InnerScroll = Make(EControlKind::ScrollView, 30);
		auto InnerOptions = InnerScroll->GetControlOptions();
		InnerOptions.ScrollY = 1000000;
		Require(InnerScroll->SetControlOptions(InnerOptions, Error) &&
		        InnerScroll->AddChild(Make(EControlKind::None, 120), Error) &&
		        OuterContent->AddChild(InnerScroll, Error) && OuterScroll->AddChild(OuterContent, Error) &&
		        Root->SetChildren({OuterScroll}, Error),
		    Error);
		Update();
		Wheel = Pointer(InnerScroll, Platform::EEventType::Wheel);
		Wheel.DeltaY = -1;
		Send(Wheel);
		Require(OuterScroll->GetControlOptions().ScrollY == 40 && InnerScroll->GetControlOptions().ScrollY == 1000000,
		    "a clamped inner scroll request swallowed wheel input at its boundary");
		Send(Wheel);
		Require(OuterScroll->GetControlOptions().ScrollY == 80,
		    "queued wheel input did not continue scrolling the ancestor");
		Wheel.DeltaY = 0;
		Require(!Send(Wheel).bHandled && InnerScroll->GetControlOptions().ScrollY == 1000000,
		    "a zero wheel delta consumed or normalized the inner request");
		auto OuterOptions = OuterScroll->GetControlOptions();
		OuterOptions.ScrollY = 0;
		Require(OuterScroll->SetControlOptions(OuterOptions, Error), Error);
		Update();
		Wheel = Pointer(InnerScroll, Platform::EEventType::Wheel);
		Wheel.DeltaY = 1;
		Send(Wheel);
		Require(InnerScroll->GetControlOptions().ScrollY == 50 && OuterScroll->GetControlOptions().ScrollY == 0,
		    "reverse wheel input failed to scroll the inner container");
		auto DeviceText = Make(EControlKind::TextBox);
		auto DeviceTextOptions = DeviceText->GetControlOptions();
		DeviceTextOptions.Text = "abc";
		Require(DeviceText->SetControlOptions(DeviceTextOptions, Error) && Root->SetChildren({DeviceText}, Error),
		    Error);
		Update();
		Send(Pointer(DeviceText, Platform::EEventType::PointerDown, 0, 17));
		Platform::FEvent Removed;
		Removed.Type = Platform::EEventType::InputDeviceRemoved;
		Removed.DeviceKind = Platform::EInputDeviceKind::Mouse;
		Removed.DeviceId = 18;
		Send(Removed);
		Require(DeviceText->SetTextSelection(0, 0, Error), Error);
		Send(Pointer(DeviceText, Platform::EEventType::PointerMove, 180, 17));
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Clipboard.clear();
		Send(Key);
		Require(Clipboard == "abc", "removing another input device cancelled the active selection drag");
		Removed.DeviceId = 17;
		Send(Removed);
		Require(DeviceText->SetTextSelection(0, 0, Error), Error);
		Send(Pointer(DeviceText, Platform::EEventType::PointerMove, 180, 18));
		Input.Text = "z";
		Send(Input);
		Require(DeviceText->GetControlOptions().Text == "zabc",
		    "removed input device left the text selection dragging");
		Touch = Pointer(DeviceText, Platform::EEventType::PointerDown, 0, 19);
		Touch.PointerKind = Platform::EPointerKind::Touch;
		Touch.PointerId = 8;
		Touch.Button = 0;
		Send(Touch);
		Removed.DeviceKind = Platform::EInputDeviceKind::Touch;
		Removed.DeviceId = 19;
		Send(Removed);
		Require(DeviceText->SetTextSelection(0, 0, Error), Error);
		Send(Pointer(DeviceText, Platform::EEventType::PointerMove, 180, 18));
		Input.Text = "q";
		Send(Input);
		Require(DeviceText->GetControlOptions().Text == "qzabc",
		    "removed touch device left the text selection dragging");
		auto OtherTarget = Make(EControlKind::None);
		Require(Root->AddChild(OtherTarget, Error), Error);
		Update();
		int InputAreaFailure = 0;
		auto BrokenArea = Services;
		BrokenArea.SetInputArea = [&](bool Enabled, const auto &, auto, auto &Failure)
		{
			if (InputAreaFailure == 1)
			{
				Failure = "test.input_area_failure";
				return false;
			}
			if (InputAreaFailure == 2)
			{
				throw std::runtime_error("test.input_area_exception");
			}
			if (InputAreaFailure == 3)
			{
				throw 7;
			}
			InputEnabled = Enabled;
			return true;
		};
		Require(Context->SetTextInputServices(BrokenArea, Error), Error);
		int CapturedMoves = 0;
		auto ObserveMoves =
		    DeviceText->Subscribe(Platform::EEventType::PointerMove, false, [&](auto &)
		{
			++CapturedMoves;
		}, Error);
		for (int Mode : {1, 2, 3})
		{
			const auto Device = static_cast<std::uint64_t>(29 + Mode);
			Send(Pointer(DeviceText, Platform::EEventType::PointerDown, 0, Device));
			Removed.DeviceKind = Platform::EInputDeviceKind::Mouse;
			Removed.DeviceId = Device;
			InputAreaFailure = Mode;
			const auto Expected = Mode == 1 ? "test.input_area_failure"
			    : Mode == 2                 ? "gui.callback_exception: test.input_area_exception"
			                                : "gui.callback_exception";
			Require(!Context->ProcessEvent(Removed, Failed, Error) && Error == Expected,
			    "device removal lost the text-input service error");
			InputAreaFailure = 0;
			Send(Pointer(OtherTarget, Platform::EEventType::PointerMove, 5, Device));
			Require(CapturedMoves == 0, "failed device cancellation retained a stale pointer capture");
		}
		Require(ObserveMoves.Disconnect(Error), Error);

		// Logical UTF-8 values and code-point selections remain stable while navigation follows visual order.
		auto BidiRoot = FWidget::Create();
		auto BidiText = Make(EControlKind::TextBox, 40);
		auto BidiOptions = BidiText->GetControlOptions();
		BidiOptions.Text = "אבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Direction, std::string("rtl"), Error) &&
		        BidiRoot->AddChild(BidiText, Error) && Context->SetRoot(BidiRoot, Error) &&
		        Context->SetTextInputServices(Services, Error),
		    Error);
		Update();
		Require(Context->SetFocus(BidiText, Error) && BidiText->SetTextSelection(0, 0, Error), Error);
		Update();
		const auto RtlStartX = InputArea.X;
		Require(InputArea.Width > 0 && InputArea.Width <= 2 && InputArea.Height > 0 && InputCursorOffset == 0,
		    "IME area is not the laid-out caret rectangle");
		Key.Type = Platform::EEventType::KeyDown;
		Key.Key = Platform::EKey::Left;
		Key.Modifiers = 0;
		Send(Key);
		Require(InputArea.X < RtlStartX, "RTL Left did not move the caret visually left");
		Key.Key = Platform::EKey::Right;
		Key.Modifiers = 1;
		Send(Key);
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Clipboard.clear();
		Send(Key);
		Require(Clipboard == "א", "visual RTL navigation changed the logical selection range");
		Require(BidiRoot->AddChild(Make(EControlKind::None, 1), Error), Error);
		Update();
		Key.Key = Platform::EKey::Left;
		Key.Modifiers = 1;
		Send(Key);
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Clipboard.clear();
		Send(Key);
		Require(Clipboard.empty(), "rebuild lost the active end of a backward RTL selection");
		Require(BidiText->SetTextSelection(1, 1, Error), Error);
		Key.Key = Platform::EKey::Backspace;
		Key.Modifiers = 0;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == "בג", "RTL Backspace deleted in visual rather than logical order");
		BidiOptions.Text = "אבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(BidiText->SetTextSelection(1, 1, Error), Error);
		Key.Key = Platform::EKey::Delete;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == "אג", "RTL Delete deleted the wrong logical character");
		BidiOptions.Text = "אבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(BidiText->SetTextSelection(0, 0, Error), Error);
		Update();
		const auto StartCaret = InputArea;
		Require(BidiText->SetTextSelection(3, 3, Error), Error);
		auto HitCaret = Pointer(BidiText, Platform::EEventType::PointerDown);
		HitCaret.X = StartCaret.X;
		HitCaret.Y = StartCaret.Y + StartCaret.Height * .5;
		Send(HitCaret);
		HitCaret.Type = Platform::EEventType::PointerUp;
		Send(HitCaret);
		Input.Text = "X";
		Send(Input);
		Require(BidiText->GetControlOptions().Text == "Xאבג", "RTL pointer hit-testing used logical prefix widths");

		BidiOptions.Text = "abc אבג 12";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Direction, std::string("auto"), Error) &&
		        BidiText->Set(EProperty::Language, std::string("ar"), Error),
		    Error);
		Update();
		Require(BidiText->SetTextSelection(4, 7, Error), Error);
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Send(Key);
		Require(Clipboard == "אבג", "mixed selection did not retain original logical text");
		Compose.Text = "ד";
		Compose.SelectionStart = 0;
		Compose.SelectionLength = 0;
		Send(Compose);
		Require(BidiText->GetControlOptions().Text == BidiOptions.Text, "mixed preedit leaked into the committed value");
		Input.Text = "דה";
		Send(Input);
		Require(BidiText->GetControlOptions().Text == "abc דה 12", "mixed IME commit replaced the wrong logical range");
		Key.Key = Platform::EKey::Z;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == BidiOptions.Text, "mixed IME undo lost logical text");
		Key.Key = Platform::EKey::C;
		Send(Key);
		Require(Clipboard == "אבג", "mixed IME undo lost its logical selection");

		BidiOptions.Kind = EControlKind::TextArea;
		BidiOptions.Text = "abc\nאבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 80}, Error),
		    Error);
		Update();
		Require(BidiText->SetTextSelection(0, 0, Error), Error);
		Update();
		const auto FirstCaretY = InputArea.Y;
		Require(BidiText->SetTextSelection(4, 4, Error), Error);
		Update();
		Require(InputArea.Y > FirstCaretY && InputCursorOffset == 0,
		    "multiline IME area did not follow the current visual line");
		BidiOptions.Text = "אבג אבג אבג אבג אבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Direction, std::string("rtl"), Error) &&
		        BidiText->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 70}, Error),
		    Error);
		Update();
		Require(BidiText->SetTextSelection(0, 0, Error), Error);
		Update();
		const auto WrapStartY = InputArea.Y;
		Key.Key = Platform::EKey::Home;
		Key.Modifiers = 0;
		Send(Key);
		Key.Key = Platform::EKey::Left;
		Send(Key);
		Require(InputArea.Y > WrapStartY, "RTL Left at a soft-wrap edge did not enter the next logical line");
		Key.Key = Platform::EKey::Right;
		Send(Key);
		Require(std::abs(InputArea.Y - WrapStartY) < .01, "RTL Right did not return across the soft-wrap edge");
		Require(BidiText->SetTextSelection(19, 19, Error), Error);
		Update();
		const auto WrapEndY = InputArea.Y;
		Require(WrapEndY > WrapStartY, "RTL soft wrap did not retain a visual line for its end caret");
		Key.Key = Platform::EKey::Up;
		Key.Modifiers = 0;
		Send(Key);
		Require(InputArea.Y < WrapEndY, "RTL Up did not navigate to the previous visual line");
		Key.Key = Platform::EKey::Down;
		Send(Key);
		Require(std::abs(InputArea.Y - WrapEndY) < .01, "RTL Down did not restore the visual caret column");

		BidiOptions.Kind = EControlKind::TextBox;
		BidiOptions.Text.clear();
		BidiOptions.Placeholder = "abc\tאבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 200}, Error),
		    Error);
		Update();
		Require(BidiText->GetControlOptions().Text.empty(), "placeholder normalization changed the logical text");
		BidiOptions.Placeholder.clear();
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::TextAlign, std::string("right"), Error),
		    Error);
		Update();
		const auto EmptyRightCaret = InputArea;
		BidiOptions.Placeholder = "אבג";
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(std::abs(InputArea.X - EmptyRightCaret.X) < .01 && std::abs(InputArea.Y - EmptyRightCaret.Y) < .01 &&
		        BidiText->GetControlOptions().Text.empty(),
		    "RTL placeholder moved the empty field's caret or IME area");
		Require(BidiText->Set(EProperty::TextAlign, std::string("left"), Error), Error);
		Update();
		const auto EmptyLeftCaret = InputArea;
		BidiOptions.Placeholder.clear();
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(std::abs(InputArea.X - EmptyLeftCaret.X) < .01 && std::abs(InputArea.Y - EmptyLeftCaret.Y) < .01,
		    "RTL placeholder moved the left-aligned empty field's caret");
		Clipboard = "a\tb";
		Key.Key = Platform::EKey::V;
		Key.Modifiers = 2;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == "ab", "tab paste changed existing single-line sanitization");
		BidiOptions.Text = "a\r\nb\u0085c\u2028d\u2029e";
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(BidiText->GetControlOptions().Text == "abcde", "single-line options retained a Unicode line separator");
		Require(BidiText->SetTextSelection(5, 5, Error), Error);
		Input.Text = "f\u0085g\u2028h\u2029i";
		Send(Input);
		Require(BidiText->GetControlOptions().Text == "abcdefghi", "single-line input retained a Unicode line separator");

		BidiOptions.Kind = EControlKind::TextArea;
		BidiOptions.Text = "a\r\nאב\u2028cd\u2029ef\u0085gh";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 200}, Error),
		    Error);
		Update();
		Require(BidiText->GetControlOptions().Text == BidiOptions.Text, "multiline normalization changed logical separators");
		double PreviousLineY = 0;
		for (const auto Offset : {0, 3, 6, 9, 12})
		{
			Require(BidiText->SetTextSelection(Offset, Offset, Error), Error);
			Update();
			if (Offset)
			{
				Require(std::abs(InputArea.Y - PreviousLineY - InputArea.Height) < .01,
				    "Unicode line separator was not displayed as exactly one hard break");
			}
			PreviousLineY = InputArea.Y;
		}
		Require(BidiText->SetTextSelection(0, 14, Error), Error);
		Key.Key = Platform::EKey::C;
		Key.Modifiers = 2;
		Send(Key);
		Require(Clipboard == BidiOptions.Text, "multiline copy changed original CRLF or Unicode separator bytes");
		BidiOptions.Text = "a\r\nב";
		Require(BidiText->SetControlOptions(BidiOptions, Error), Error);
		Update();
		Require(BidiText->SetTextSelection(3, 3, Error), Error);
		Key.Key = Platform::EKey::Backspace;
		Key.Modifiers = 0;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == "aב", "Backspace split a CRLF hard break");
		Key.Key = Platform::EKey::Z;
		Key.Modifiers = 2;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == BidiOptions.Text, "undo did not preserve original CRLF bytes");
		Require(BidiText->SetTextSelection(1, 1, Error), Error);
		Key.Key = Platform::EKey::Delete;
		Key.Modifiers = 0;
		Send(Key);
		Require(BidiText->GetControlOptions().Text == "aב", "Delete split a CRLF hard break");
		BidiOptions.Text = "אבג abcdefghijklmnop";
		Require(BidiText->SetControlOptions(BidiOptions, Error) &&
		        BidiText->Set(EProperty::Direction, std::string("auto"), Error) &&
		        BidiText->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 70}, Error),
		    Error);
		Update();
		Require(BidiText->SetTextSelection(0, 0, Error), Error);
		Update();
		const auto RtlParagraphFirstY = InputArea.Y;
		Key.Key = Platform::EKey::Home;
		Send(Key);
		Key.Key = Platform::EKey::Left;
		Send(Key);
		const auto LatinEntry = InputArea;
		Require(LatinEntry.Y > RtlParagraphFirstY, "RTL paragraph did not enter its Latin soft line");
		Send(Key);
		Require(std::abs(InputArea.Y - LatinEntry.Y) < .01 && InputArea.X < LatinEntry.X,
		    "RTL visual-edge navigation skipped a pure Latin soft line");
		Key.Key = Platform::EKey::Right;
		Send(Key);
		Require(std::abs(InputArea.Y - LatinEntry.Y) < .01 && std::abs(InputArea.X - LatinEntry.X) < .01,
		    "Latin soft line did not preserve visual caret traversal");
		Send(Key);
		Require(std::abs(InputArea.Y - RtlParagraphFirstY) < .01,
		    "visual Right did not return from a Latin soft line to its RTL predecessor");
		Require(Context->Close(Error) && !InputEnabled, "closing did not stop text input: " + Error);
		std::cout << "GUI controls: activation, focus, range, scroll, Unicode editing, clipboard, undo, IME and "
		             "rebuild passed\n";
		return 0;
	}
	catch (const std::exception &E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
