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
	void Require(bool Value, const std::string &Why)
	{
		if (!Value)
		{
			throw std::runtime_error(Why + ": " + Error);
		}
	}
	std::shared_ptr<FWidget> Make(EControlKind Kind, double Height = 30)
	{
		auto Widget = FWidget::Create();
		FControlOptions O;
		O.Kind = Kind;
		Require(Widget->SetControlOptions(O, Error) &&
					Widget->Set(EProperty::Height, FLength{ELengthUnit::Pixel, Height}, Error) &&
					Widget->Set(EProperty::Focusable,
								Kind == EControlKind::Button || Kind == EControlKind::RadioButton ||
									Kind == EControlKind::Splitter || IsTextControl(Kind),
								Error),
				"create");
		return Widget;
	}
	FLayoutBox Box(const std::shared_ptr<FWidget> &Widget)
	{
		FLayoutBox B;
		Require(Widget->GetLayout(B, Error), "geometry");
		return B;
	}
	void CheckFixedVerticalFlow()
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 400, 1}, Error);
		Require(Context != nullptr, "fixed vertical context");
		auto Root = FWidget::Create(), Panel = Make(EControlKind::None, 120);
		auto First = Make(EControlKind::None), Hidden = Make(EControlKind::None), Last = Make(EControlKind::None, 25);
		Require(Panel->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 200}, Error) &&
			Panel->Set(EProperty::Padding, FInsets{10, 10, 10, 10}, Error) &&
			Panel->Set(EProperty::Gap, 4.0, Error) &&
			First->Set(EProperty::Height, FLength{ELengthUnit::Percent, 50}, Error) &&
			Hidden->Set(EProperty::Visibility, EVisibility::Collapsed, Error) &&
			Panel->SetChildren({First, Hidden, Last}, Error) && Root->SetChildren({Panel}, Error) &&
			Context->SetRoot(Root, Error), "fixed vertical tree");
		Require(Context->Update(.01, Error), "fixed vertical layout");
		Require(std::abs(Box(First).Border.Width - 180) < .1 &&
			std::abs(Box(First).Border.Height - 50) < .1 &&
			std::abs(Box(Last).Border.Y - Box(First).Border.Y - 54) < .1,
			"fixed vertical flow preserves stretch, percentages, padding and collapsed gaps");
		Require(Panel->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 220}, Error) &&
			Context->Update(.02, Error), "fixed vertical resize");
		Require(std::abs(Box(First).Border.Height - 100) < .1 &&
			std::abs(Box(Last).Border.Y - Box(First).Border.Y - 104) < .1,
			"fixed vertical percentage follows the resized content box");
		Require(Panel->Set(EProperty::Height, FLength{ELengthUnit::Auto, 0}, Error) &&
			First->Set(EProperty::Height, FLength{ELengthUnit::Pixel, 50}, Error) &&
			Context->Update(.03, Error), "fixed to auto vertical layout");
		Require(std::abs(Box(Panel).Border.Height - 99) < .1,
			"switching fixed vertical flow to auto preserves content height");
		Require(Context->Close(Error), "fixed vertical close");
	}
	void CheckNestedManagedContainers()
	{
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {600, 600, 1.5}, Error);
		Require(Context != nullptr, "nested containers context");
		auto Split = Make(EControlKind::Splitter, 300), Grid = Make(EControlKind::Grid), Other = Make(EControlKind::None);
		Require(Split->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 320}, Error) &&
			Split->Set(EProperty::Padding, FInsets{10, 10, 10, 10}, Error) &&
			Split->Set(EProperty::BorderWidth, 2.0, Error) &&
			Grid->Set(EProperty::Padding, FInsets{2, 2, 2, 2}, Error) &&
			Grid->Set(EProperty::BorderWidth, 1.0, Error) &&
			Grid->Set(EProperty::Gap, 4.0, Error), "nested container properties");
		auto A = Make(EControlKind::None, 25), B = Make(EControlKind::None, 25);
		auto C = Make(EControlKind::None, 25), D = Make(EControlKind::None, 25);
		Require(Grid->SetChildren({A, B, C, D}, Error) && Split->SetChildren({Grid, Other}, Error) &&
			Context->SetRoot(Split, Error), "nested container tree");
		for (int Index = 0; Index < 9; ++Index)
		{
			auto Options = Split->GetControlOptions();
			Options.Step = 0;
			Options.Value = .15 + .08 * Index;
			Require(Split->SetControlOptions(Options, Error) && Context->Update(.01 * (Index + 1), Error),
				"nested splitter resize");
			const double Width = (Box(Grid).Content.Width - 4) / 2;
			Require(std::abs(Box(A).Border.Width - Width) < .1 &&
				std::abs(Box(B).Border.Width - Width) < .1 && Box(C).Border.Y > Box(A).Border.Y,
				"batched nested grid uses the current splitter pane size and edges: grid=" +
				std::to_string(Box(Grid).Content.Width) + " expected cell=" + std::to_string(Width) +
				" actual cell=" + std::to_string(Box(A).Border.Width) + " row delta=" +
				std::to_string(Box(C).Border.Y - Box(A).Border.Y));
		}
		Require(Context->Close(Error), "nested containers close");
	}
} // namespace

int main()
{
	try
	{
		CheckFixedVerticalFlow();
		CheckNestedManagedContainers();
		double Parsed = 0;
		Require(ParseControlNumber("+1.25e2", Parsed) && Parsed == 125 && !ParseControlNumber("+-1", Parsed) &&
					!ParseControlNumber("++1", Parsed) && !ParseControlNumber("+", Parsed),
				"numeric drafts accept a single leading sign");
		auto Context = FContext::Create(RmlUi::CreateBackend(GuiTestResourceOptions(), Error), {400, 600, 1}, Error);
		Require(Context != nullptr, "context");
		double Time = 0;
		const auto Update = [&]
		{
			Require(Context->Update(Time += .01, Error), "update");
		};
		const auto Send = [&](Platform::EEventType Type, double X = 0, double Y = 0,
							  Platform::EKey Key = Platform::EKey::Unknown, std::string Text = "")
		{
			Platform::FEvent Event;
			Event.WindowId = 1;
			Event.Type = Type;
			Event.X = X;
			Event.Y = Y;
			Event.Button = 1;
			Event.Key = Key;
			Event.Text = std::move(Text);
			FEventResult Result;
			Require(Context->ProcessEvent(Event, Result, Error), "input");
			return Result.bHandled;
		};
		using Platform::EEventType;
		using Platform::EKey;
		auto Root = FWidget::Create();
		auto Grid = Make(EControlKind::Grid, 60);
		Require(Grid->Set(EProperty::Gap, 10.0, Error), "grid gap");
		auto A = Make(EControlKind::None, 20), B = Make(EControlKind::None, 20), C = Make(EControlKind::None, 20);
		Require(Grid->SetChildren({A, B, C}, Error) && Root->AddChild(Grid, Error) && Context->SetRoot(Root, Error),
				"grid tree");
		Update();
		Require(std::abs(Box(A).Border.Width - 195) < .1 && Box(B).Border.X > Box(A).Border.X &&
					Box(C).Border.Y > Box(A).Border.Y,
				"equal columns and wrapping rows");
		Require(Context->SetViewport({600, 900, 1.5}, Error), "scale");
		Update();
		Require(std::abs(Box(A).Border.Width - 195) < .1, "logical grid geometry at high DPI");
		auto D = Make(EControlKind::None, 20);
		auto GridOptions = Grid->GetControlOptions();
		GridOptions.Columns = 3;
		Require(Grid->SetControlOptions(GridOptions, Error) && Grid->SetChildren({A, B, C, D}, Error), "three columns");
		Update();
		Require(Box(C).Border.Y == Box(A).Border.Y && Box(D).Border.Y > Box(A).Border.Y,
				"fractional column width retains the third cell");
		Require(Grid->Set(EProperty::Text, std::string("Cell content"), Error), "grid literal content");
		Update();
		Require(Box(B).Border.Y == Box(A).Border.Y && Box(C).Border.Y > Box(A).Border.Y,
				"literal grid content occupies one equal cell");
		auto Leaf = Make(EControlKind::None, 20);
		auto Nested = Leaf;
		for (int Depth = 0; Depth < 4; ++Depth)
		{
			auto Parent = Make(EControlKind::Grid, 40);
			Require(Parent->SetChildren({Nested, Make(EControlKind::None, 20)}, Error), "nested grid");
			Nested = Parent;
		}
		Require(Root->SetChildren({Nested}, Error), "nested root");
		Update();
		Require(std::abs(Box(Leaf).Border.Width - 25) < .1, "nested containers settle in one update");
		auto Wrap = Make(EControlKind::Wrap, 70);
		Require(Wrap->Set(EProperty::Layout, ELayout::Horizontal, Error), "wrap direction");
		for (const auto &W : {A, B, C})
		{
			Require(W->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 180}, Error), "wrap item width");
		}
		Require(Grid->SetChildren({}, Error) && Wrap->SetChildren({A, B, C}, Error) && Root->SetChildren({Wrap}, Error),
				"wrap tree");
		Update();
		Require(Box(B).Border.Y == Box(A).Border.Y && Box(C).Border.Y > Box(A).Border.Y, "wrap flow");
		auto Split = Make(EControlKind::Splitter, 100);
		auto O = Split->GetControlOptions();
		O.Value = .25;
		Require(Split->SetControlOptions(O, Error) && Wrap->SetChildren({}, Error) &&
					Split->SetChildren({A, B}, Error) && Root->SetChildren({Split}, Error),
				"split tree");
		Require(Split->Set(EProperty::Text, std::string("Background caption"), Error), "split literal content");
		Update();
		Require(std::abs(Box(A).Border.Width - 98.5) < .1 && std::abs(Box(B).Border.Width - 295.5) < .1, "split ratio");
		const auto SB = Box(Split).Content;
		Require(Box(A).Border.X == SB.X && Box(A).Border.Y == SB.Y,
				"split panes stay anchored to their content viewport");
		// Window coordinates are converted to logical units by the context's display scale.
		Send(EEventType::PointerDown, (SB.X + 101) * 1.5, (SB.Y + 5) * 1.5);
		Send(EEventType::PointerMove, (SB.X + 300) * 1.5, (SB.Y + 5) * 1.5);
		Send(EEventType::PointerUp, (SB.X + 300) * 1.5, (SB.Y + 5) * 1.5);
		Update();
		Require(Split->GetControlOptions().Value > .7 && Box(A).Border.Width > 275, "captured splitter drag");
		O = Split->GetControlOptions();
		O.bVertical = true;
		Require(Split->SetControlOptions(O, Error), "vertical split");
		Update();
		Require(Box(B).Border.Y > Box(A).Border.Y && Box(A).Border.Width == 400, "vertical panes");
		Require(Split->Set(EProperty::Padding, FInsets{5, 5, 5, 5}, Error) &&
					Split->Set(EProperty::BorderWidth, 1.0, Error),
				"padded split");
		Update();
		Require(Box(A).Border.X == Box(Split).Content.X && Box(A).Border.Y == Box(Split).Content.Y &&
					std::abs(Box(B).Border.Y + Box(B).Border.Height - Box(Split).Content.Y -
							 Box(Split).Content.Height) < .1,
				"split panes respect padding and border");
		O = Split->GetControlOptions();
		O.DividerSize = 1000;
		Require(Split->SetControlOptions(O, Error), "oversized divider");
		Update();
		Require(Box(A).Border.Height == 0 && Box(B).Border.Height == 0 &&
					std::abs(Box(B).Border.Y - Box(Split).Content.Y - Box(Split).Content.Height) < .1,
				"oversized divider remains inside the content viewport");
		auto Group = Make(EControlKind::RadioGroup, 100);
		auto First = Make(EControlKind::RadioButton), Second = Make(EControlKind::RadioButton);
		O = First->GetControlOptions();
		O.ChoiceKey = "a";
		Require(First->SetControlOptions(O, Error), "first choice");
		O.ChoiceKey = "b";
		Require(Second->SetControlOptions(O, Error) && Group->SetChildren({First, Second}, Error) &&
					Root->SetChildren({Group}, Error),
				"radio tree");
		auto NestedGroup = Make(EControlKind::RadioGroup), NestedChoice = Make(EControlKind::RadioButton);
		O = NestedChoice->GetControlOptions();
		O.ChoiceKey = "a";
		Require(NestedChoice->SetControlOptions(O, Error), "nested radio choice");
		O = NestedGroup->GetControlOptions();
		O.Text = "a";
		Require(NestedGroup->SetControlOptions(O, Error) && NestedGroup->AddChild(NestedChoice, Error) &&
					Group->AddChild(NestedGroup, Error),
				"nested radio group");
		Update();
		Require(First->Activate(Error), "radio select");
		Require(Group->GetControlOptions().Text == "a" && First->GetControlOptions().bChecked &&
					!Second->GetControlOptions().bChecked,
				"radio exclusivity");
		Require(Context->SetFocus(First, Error), "radio focus");
		int RedirectedSelections = 0;
		Require(Second->SetControlCallback(
					[&](const auto &, auto &)
					{
						++RedirectedSelections;
						return true;
					},
					Error) &&
					Group->SetControlCallback(
						[&](const auto &Change, auto &Failure)
						{
							Require(First->GetControlOptions().bChecked == (Change.Text == "a") &&
										Second->GetControlOptions().bChecked == (Change.Text == "b"),
									"radio callback observes a consistent selection");
							return Change.Text != "b" || First->Activate(Failure);
						},
						Error),
				"radio redirect callbacks");
		Require(Second->Activate(Error) && Group->GetControlOptions().Text == "a" &&
					First->GetControlOptions().bChecked && !Second->GetControlOptions().bChecked &&
					RedirectedSelections == 0 && NestedChoice->GetControlOptions().bChecked,
				"radio callback can redirect selection without a stale selected event");
		Require(Group->SetControlCallback({}, Error) && Second->SetControlCallback({}, Error), "clear radio callbacks");
		Send(EEventType::KeyDown, 0, 0, EKey::Right);
		Require(Context->GetFocus() == Second && Group->GetControlOptions().Text == "b" &&
					!First->GetControlOptions().bChecked,
				"radio arrows");
		Require(Group->SetControlCallback(
					[](const auto &, auto &Failure)
					{
						Failure = "test.radio_selection_failure";
						return false;
					},
					Error),
				"radio failure callback");
		Require(!First->Activate(Error) && Error == "test.radio_selection_failure" &&
					Group->GetControlOptions().Text == "a" && First->GetControlOptions().bChecked &&
					!Second->GetControlOptions().bChecked && NestedChoice->GetControlOptions().bChecked,
				"radio callback failure preserves a consistent group state");
		Require(Group->SetControlCallback({}, Error), "clear radio failure callback");
		O = Second->GetControlOptions();
		O.ChoiceKey = "a";
		Require(Second->SetControlOptions(O, Error) && !Context->Update(Time += .01, Error),
				"duplicate radio key rejected");
		O.ChoiceKey = "b";
		Require(Second->SetControlOptions(O, Error), "restore radio key");
		auto Number = Make(EControlKind::NumberBox), Password = Make(EControlKind::PasswordBox),
			 Button = Make(EControlKind::Button);
		O = Number->GetControlOptions();
		O.Maximum = 100;
		O.Value = 12.5;
		O.Step = .5;
		Require(Number->SetControlOptions(O, Error), "number options");
		O = Password->GetControlOptions();
		O.Text = "secret";
		Require(Password->SetControlOptions(O, Error) && Root->SetChildren({Number, Password, Button}, Error),
				"editor tree");
		Update();
		Require(Number->FocusControl(Error) && Number->SetTextSelection(0, 4, Error), "number selection");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "-");
		Update();
		Require(Number->GetControlOptions().Text == "-" && Number->GetControlOptions().Value == 12.5,
				"intermediate numeric draft");
		Send(EEventType::KeyDown, 0, 0, EKey::Enter);
		Update();
		Require(Number->GetControlOptions().Text == "12.5", "invalid draft restored");
		Require(Number->SetTextSelection(0, 4, Error), "number select");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "145.2");
		Send(EEventType::KeyDown, 0, 0, EKey::Enter);
		Update();
		Require(Number->GetControlOptions().Value == 100 && Number->GetControlOptions().Text == "100",
				"numeric range on commit");
		Require(Number->SetTextSelection(0, 3, Error), "number select again");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "2.2");
		Require(Context->SetFocus(Button, Error), "blur commit");
		Update();
		Require(Number->GetControlOptions().Value == 2 && Number->GetControlOptions().Text == "2",
				"number step and blur");
		Require(Number->FocusControl(Error) && Number->SetTextSelection(0, 1, Error), "number window blur selection");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "7.2");
		Send(EEventType::FocusLost);
		Require(!Context->GetFocus() && Number->GetControlOptions().Value == 7 &&
					Number->GetControlOptions().Text == "7",
				"window focus loss commits the numeric draft");
		bool InputEnabled = false;
		int CleanupFailure = 0;
		FTextInputServices TextServices;
		TextServices.SetInputArea = [&](bool Enabled, const auto &, auto, auto &Failure)
		{
			if (!Enabled && CleanupFailure)
			{
				if (CleanupFailure == 2)
				{
					throw std::runtime_error("test.input_cleanup_exception");
				}
				if (CleanupFailure == 3)
				{
					throw 7;
				}
				Failure = "test.input_cleanup_failure";
				return false;
			}
			InputEnabled = Enabled;
			return true;
		};
		Require(Context->SetTextInputServices(TextServices, Error), "numeric input service");
		for (bool bFailCleanup : {false, true})
		{
			for (int Mode : {1, 2, 3})
			{
				Require(Number->SetControlCallback(
							[Mode](const auto &, auto &Failure) -> bool
							{
								if (Mode == 2)
								{
									throw std::runtime_error("test.number_commit_exception");
								}
								if (Mode == 3)
								{
									throw 7;
								}
								Failure = "test.number_commit_failure";
								return false;
							},
							Error) &&
							Number->FocusControl(Error) && InputEnabled &&
							Number->SetTextSelection(0, Number->GetControlOptions().Text.size(), Error),
						"failing numeric commit setup");
				Send(EEventType::TextInput, 0, 0, EKey::Unknown, std::to_string(7 + Mode));
				Platform::FEvent Lost;
				Lost.WindowId = 1;
				Lost.Type = EEventType::FocusLost;
				FEventResult Result;
				CleanupFailure = bFailCleanup ? Mode : 0;
				const auto Expected = Mode == 1	  ? "test.number_commit_failure"
									  : Mode == 2 ? "gui.callback_exception: test.number_commit_exception"
												  : "gui.callback_exception";
				Require(!Context->ProcessEvent(Lost, Result, Error) && Error == Expected && !Context->GetFocus() &&
							InputEnabled == bFailCleanup,
						"input cleanup preserves the original numeric commit error");
				CleanupFailure = 0;
				Update();
				Require(!InputEnabled, "failed text input cleanup is retried after host recovery");
			}
		}
		Require(Number->SetControlCallback({}, Error), "clear numeric commit callback");
		for (int Mode : {1, 2, 3})
		{
			Require(Number->FocusControl(Error) && InputEnabled, "input cleanup failure setup");
			CleanupFailure = Mode;
			Platform::FEvent Lost;
			Lost.WindowId = 1;
			Lost.Type = EEventType::FocusLost;
			FEventResult Result;
			const auto Expected = Mode == 1	  ? "test.input_cleanup_failure"
								  : Mode == 2 ? "gui.callback_exception: test.input_cleanup_exception"
											  : "gui.callback_exception";
			Require(!Context->ProcessEvent(Lost, Result, Error) && Error == Expected && !Context->GetFocus() &&
						InputEnabled,
					"input cleanup failure is reported when the numeric commit succeeds");
			CleanupFailure = 0;
			Update();
			Require(!InputEnabled, "text input cleanup recovers after a host-only failure");
		}
		bool ReplacementEnabled = false;
		FTextInputServices ReplacementServices;
		ReplacementServices.SetInputArea = [&](bool Enabled, const auto &, auto, auto &)
		{
			ReplacementEnabled = Enabled;
			return true;
		};
		for (int Mode : {1, 2, 3})
		{
			Require(Number->FocusControl(Error) && InputEnabled, "input service replacement setup");
			CleanupFailure = Mode;
			const auto Expected = Mode == 1	  ? "test.input_cleanup_failure"
								  : Mode == 2 ? "gui.callback_exception: test.input_cleanup_exception"
											  : "gui.callback_exception";
			Require(!Context->SetTextInputServices(ReplacementServices, Error) && Error == Expected && InputEnabled &&
						!ReplacementEnabled && Context->GetFocus() == Number,
					"failed service replacement preserves the old service and logical focus");
			CleanupFailure = 0;
			Require(Context->SetTextInputServices(ReplacementServices, Error) && !InputEnabled && ReplacementEnabled &&
						Context->GetFocus() == Number,
					"input service replacement succeeds after host recovery");
			Require(Context->SetTextInputServices(TextServices, Error) && InputEnabled && !ReplacementEnabled,
					"restore original text input service");
		}
		O = Number->GetControlOptions();
		O.Value = 2;
		Require(Number->SetControlOptions(O, Error), "restore numeric value");
		int Submissions = 0;
		Require(Number->SetControlCallback(
					[&](const auto &Change, auto &)
					{
						Submissions += Change.Action == EControlAction::Submitted ? 1 : 0;
						return true;
					},
					Error),
				"numeric submission callback");
		O = Number->GetControlOptions();
		O.bReadOnly = true;
		Require(Number->SetControlOptions(O, Error), "numeric read-only mode");
		Update();
		Require(Number->FocusControl(Error), "read-only numeric focus");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "99");
		Send(EEventType::KeyDown, 0, 0, EKey::Enter);
		Require(Submissions == 1 && Number->GetControlOptions().Value == 2,
				"read-only submission preserves the numeric value");
		Require(Password->FocusControl(Error) && Password->SetTextSelection(0, 6, Error), "password select");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "abc\n\t");
		Update();
		Require(Password->GetControlOptions().Text == "abc", "password uses single-line editing");

		auto Anchor = Make(EControlKind::Button, 25), Popup = Make(EControlKind::Popup, 80),
			 Tip = Make(EControlKind::ToolTip, 25);
		auto Inner = Make(EControlKind::Button, 30), Behind = Make(EControlKind::Button, 100);
		O = Popup->GetControlOptions();
		O.bOpen = true;
		Require(Popup->SetControlOptions(O, Error) &&
					Popup->Set(EProperty::Width, FLength{ELengthUnit::Pixel, 160}, Error) &&
					Popup->AddChild(Inner, Error) && Anchor->Set(EProperty::ClipContent, true, Error) &&
					Anchor->SetChildren({Popup, Tip}, Error) && Root->SetChildren({Anchor, Behind}, Error),
				"floating tree");
		Update();
		Require(Box(Popup).Border.Y == Box(Anchor).Border.Y + 25 && Box(Behind).Border.Y == 25,
				"popup leaves ordinary flow and anchor clipping");
		int Clicks = 0;
		Require(Inner->SetControlCallback(
					[&](const auto &, auto &)
					{
						++Clicks;
						return true;
					},
					Error),
				"popup callback");
		const auto PB = Box(Inner).Border;
		Send(EEventType::PointerDown, (PB.X + 5) * 1.5, (PB.Y + 5) * 1.5);
		Send(EEventType::PointerUp, (PB.X + 5) * 1.5, (PB.Y + 5) * 1.5);
		Require(Clicks == 1, "popup occludes later ordinary sibling and ignores anchor clip");
		Send(EEventType::KeyDown, 0, 0, EKey::Escape);
		Update();
		Require(!Popup->GetControlOptions().bOpen && Box(Popup).Border.Height == 0 && Context->GetFocus() != Inner,
				"popup dismiss and focus cleanup");
		O = Popup->GetControlOptions();
		O.bOpen = true;
		Require(Popup->SetControlOptions(O, Error), "reopen");
		Update();
		Require(Send(EEventType::PointerDown, 390 * 1.5, 400 * 1.5), "outside dismissal handled");
		Update();
		Require(!Popup->GetControlOptions().bOpen, "outside dismissal");
		Require(Anchor->FocusControl(Error), "anchor focus");
		O = Popup->GetControlOptions();
		O.bOpen = true;
		Require(Popup->SetControlOptions(O, Error), "open with focus");
		Update();
		Require(Inner->FocusControl(Error), "floating focus");
		Send(EEventType::KeyDown, 0, 0, EKey::Escape);
		Update();
		Require(Context->GetFocus() == Anchor, "closing restores the eligible preceding focus");
		Send(EEventType::PointerMove, 5 * 1.5, 5 * 1.5);
		Update();
		Require(!Tip->GetControlOptions().bOpen, "tooltip delay");
		Require(Context->Update(Time += .6, Error), "tooltip time");
		Require(Tip->GetControlOptions().bOpen, "tooltip automatic open");
		O = Popup->GetControlOptions();
		O.bOpen = true;
		Require(Popup->SetControlOptions(O, Error), "open adjacent popup");
		Update();
		const auto FloatingChild = Box(Inner).Border;
		Send(EEventType::PointerMove, (FloatingChild.X + 5) * 1.5, (FloatingChild.Y + 5) * 1.5);
		Require(Context->Update(Time += .6, Error), "hover floating descendant");
		Require(!Tip->GetControlOptions().bOpen, "a portal does not keep its anchor tooltip open");
		Send(EEventType::PointerMove, 390 * 1.5, 400 * 1.5);
		Update();
		Require(!Tip->GetControlOptions().bOpen, "tooltip automatic close");
		O = Number->GetControlOptions();
		O.bReadOnly = false;
		Require(Number->SetControlOptions(O, Error) && Root->SetChildren({Number, Button}, Error),
				"numeric focus callback tree");
		Update();
		Require(Number->FocusControl(Error) && Number->SetTextSelection(0, 1, Error), "numeric detach selection");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "5");
		Require(Number->SetControlCallback(
					[&](const auto &, auto &Failure)
					{
						return Root->RemoveChild(Button, Failure);
					},
					Error),
				"numeric callback detaches requested focus target");
		Require(!Context->SetFocus(Button, Error) && Error == "gui.invalid_focus_target" &&
					Context->GetFocus() == Number,
				"numeric blur revalidates a focus target removed by the commit callback");
		Require(Root->AddChild(Button, Error), "restore numeric focus target");
		Update();
		Require(Number->SetTextSelection(0, 1, Error), "numeric close selection");
		Send(EEventType::TextInput, 0, 0, EKey::Unknown, "6");
		Require(Number->SetControlCallback(
					[&](const auto &, auto &Failure)
					{
						return Context->Close(Failure);
					},
					Error),
				"numeric callback closes context");
		Require(!Context->SetFocus(Button, Error) && Error == "gui.context_closed" && !Context->IsOpen() &&
					!Context->GetFocus(),
				"numeric blur revalidates a context closed by the commit callback");
		Require(Context->Close(Error), "close");
		std::cout << "Grid, Wrap, Splitter, radio groups, numeric/password editing and floating layers passed\n";
		return 0;
	}
	catch (const std::exception &Exception)
	{
		std::cerr << Exception.what() << '\n';
		return 1;
	}
}
