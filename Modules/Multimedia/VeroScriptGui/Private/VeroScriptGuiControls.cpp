// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiControls.h"
#include "VeroScriptGuiContext.h"
#include "VeroScriptFloatParsing.h"
#include "VeroScriptFloatFormatting.h"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <exception>
#include <new>

namespace VeroScript::Gui
{

	bool FControlOptions::operator==(const FControlOptions &B) const noexcept
	{
		return Kind == B.Kind && bChecked == B.bChecked && bVertical == B.bVertical && bReadOnly == B.bReadOnly &&
		    Value == B.Value && Minimum == B.Minimum && Maximum == B.Maximum && Step == B.Step &&
		    ScrollX == B.ScrollX && ScrollY == B.ScrollY && PageSize == B.PageSize && Text == B.Text &&
		    Placeholder == B.Placeholder && Columns == B.Columns && DividerSize == B.DividerSize &&
		    ChoiceKey == B.ChoiceKey && bOpen == B.bOpen && bDismissOnOutside == B.bDismissOnOutside &&
		    bAutoOpen == B.bAutoOpen && OffsetX == B.OffsetX && OffsetY == B.OffsetY && Delay == B.Delay &&
		    bModal == B.bModal && bDismissOnEscape == B.bDismissOnEscape &&
		    bMatchAnchorWidth == B.bMatchAnchorWidth && Placement == B.Placement &&
		    bSelected == B.bSelected && bExpandable == B.bExpandable && bExpanded == B.bExpanded &&
		    bItemIndicator == B.bItemIndicator &&
		    PropertyValuesEqual(SelectionColor, B.SelectionColor) && ModalOrder == B.ModalOrder;
	}
	bool IsTextControl(EControlKind Kind) noexcept
	{
		return Kind == EControlKind::TextBox || Kind == EControlKind::TextArea || Kind == EControlKind::PasswordBox ||
		    Kind == EControlKind::NumberBox;
	}
	bool IsFloatingControl(EControlKind Kind) noexcept
	{
		return Kind == EControlKind::Popup || Kind == EControlKind::ToolTip;
	}
	bool ParseControlNumber(const std::string &Text, double &Value) noexcept
	{
		if (Text.empty())
		{
			return false;
		}
		const char *Begin = Text.data(), *End = Begin + Text.size();
		if (*Begin == '+')
		{
			++Begin;
			if (Begin == End || *Begin == '-' || *Begin == '+')
			{
				return false;
			}
		}
		try
		{
			const auto Parsed = FloatParsingDetail::FromCharsDecimal(
			    std::string_view(Begin, static_cast<std::size_t>(End - Begin)), Value);
			return Parsed.ec == std::errc() && Parsed.ptr == End && std::isfinite(Value);
		}
		catch (const std::bad_alloc &)
		{
			return false;
		}
	}
	std::string FormatControlNumber(double Value)
	{
		char Buffer[64];
		const auto Result = FloatFormattingDetail::ToCharsGeneral(Buffer, Buffer + sizeof(Buffer), Value);
		return std::string(Buffer, Result.ptr);
	}
	bool ValidateControlOptions(const FControlOptions &V, std::string &Error)
	{
		Error.clear();
		if (V.Kind > EControlKind::ToolTip || !std::isfinite(V.Minimum) || !std::isfinite(V.Maximum) ||
		    !std::isfinite(V.Maximum - V.Minimum) || V.Maximum < V.Minimum || !std::isfinite(V.Value) ||
		    !std::isfinite(V.Step) || V.Step < 0 || !std::isfinite(V.PageSize) || V.PageSize < 0 ||
		    !std::isfinite(V.ScrollX) || !std::isfinite(V.ScrollY) || V.ScrollX < 0 || V.ScrollY < 0 ||
		    !ValidateGuiText(V.Text) || !ValidateGuiText(V.Placeholder) || V.Text.size() > 1024 * 1024 ||
		    V.Placeholder.size() > 1024 * 1024 || V.Columns < 1 || V.Columns > 4096 || !std::isfinite(V.DividerSize) ||
		    V.DividerSize < 0 || V.DividerSize > 1000000 || !ValidateGuiText(V.ChoiceKey) ||
		    V.ChoiceKey.size() > 1024 || !std::isfinite(V.OffsetX) || !std::isfinite(V.OffsetY) ||
		    std::abs(V.OffsetX) > 1000000 || std::abs(V.OffsetY) > 1000000 || !std::isfinite(V.Delay) || V.Delay < 0 ||
		    (V.Kind == EControlKind::Splitter && (V.Minimum < 0 || V.Maximum > 1)) ||
		    V.Placement > FControlOptions::EPlacement::Center ||
		    !Graphics::ValidateColor(V.SelectionColor, Error) ||
		    (V.bModal && V.Kind != EControlKind::Popup))
		{
			Error = "gui.invalid_control_options";
			return false;
		}
		return true;
	}
	double NormalizeControlValue(const FControlOptions &O, double Value) noexcept
	{
		Value = std::clamp(Value, O.Minimum, O.Maximum);
		if (Value == O.Minimum || Value == O.Maximum)
		{
			return Value;
		}
		if (O.Step > 0)
		{
			const auto Units = (Value - O.Minimum) / O.Step;
			if (std::isfinite(Units))
			{
				const auto Stepped = O.Minimum + std::round(Units) * O.Step;
				if (std::isfinite(Stepped))
				{
					Value = std::clamp(Stepped, O.Minimum, O.Maximum);
				}
			}
		}
		return Value;
	}
	bool FWidget::SetControlOptions(FControlOptions Value, std::string &Error)
	{
		if (!Check(Error, true) || !ValidateControlOptions(Value, Error))
		{
			return false;
		}
		if (IsTextControl(Value.Kind))
		{
			// Keep the service contract independent of a GUI backend. Multi-line text retains its
			// logical separators (including CRLF); single-line controls reject every hard separator.
			std::size_t Written = 0;
			for (std::size_t Read = 0; Read < Value.Text.size();)
			{
				const auto First = static_cast<unsigned char>(Value.Text[Read]);
				std::size_t Separator = 0;
				if (Value.Kind != EControlKind::TextArea)
				{
					if (First == '\r' || First == '\n' || (First >= 0x1c && First <= 0x1e))
					{
						Separator = 1;
					}
					else if (First == 0xc2 && Value.Text.size() - Read >= 2 && static_cast<unsigned char>(Value.Text[Read + 1]) == 0x85)
					{
						Separator = 2;
					}
					else if (First == 0xe2 && Value.Text.size() - Read >= 3 && static_cast<unsigned char>(Value.Text[Read + 1]) == 0x80 &&
					    (static_cast<unsigned char>(Value.Text[Read + 2]) == 0xa8 || static_cast<unsigned char>(Value.Text[Read + 2]) == 0xa9))
					{
						Separator = 3;
					}
				}
				if (Separator || First == '\t')
				{
					Read += Separator ? Separator : 1;
				}
				else
				{
					Value.Text[Written++] = Value.Text[Read++];
				}
			}
			Value.Text.resize(Written);
		}
		Value.Value = NormalizeControlValue(Value, Value.Value);
		if (Value.Kind == EControlKind::NumberBox)
		{
			Value.Text = Control.Kind == Value.Kind && Control.Value == Value.Value &&
			        (!Value.bReadOnly || Control.bReadOnly) && Control.Minimum == Value.Minimum &&
			        Control.Maximum == Value.Maximum && Control.Step == Value.Step
			    ? Control.Text
			    : FormatControlNumber(Value.Value);
		}
		if (!(Control == Value))
		{
			if (const auto Owner = Context.lock(); Owner && Value.bModal && Value.bOpen)
			{
				Owner->bHasModalControls = true;
				if (!Control.bModal || !Control.bOpen)
				{
					if (Owner->NextModalOrder == UINT64_MAX)
					{
						Error = "gui.invalid_control_options";
						return false;
					}
					auto &State = Owner->FloatingStates[Identity];
					State.Widget = shared_from_this();
					if (!State.bOpen)
					{
						Owner->RememberFloatingFocus(State);
					}
					State.ModalOrder = ++Owner->NextModalOrder;
				}
			}
			Control = std::move(Value);
			MarkDirty();
		}
		return true;
	}

	bool FWidget::SetFloatingOpen(bool bOpen, std::string &Error)
	{
		if (!Check(Error, true) || !IsFloatingControl(Control.Kind))
		{
			if (Error.empty())
			{
				Error = "gui.invalid_control_kind";
			}
			return false;
		}
		if (Control.bOpen == bOpen) return true;
		const auto Owner = Context.lock();
		if (Owner && bOpen)
		{
			auto &State = Owner->FloatingStates[Identity];
			State.Widget = shared_from_this();
			Owner->RememberFloatingFocus(State);
		}
		auto Options = Control;
		Options.bOpen = bOpen;
		if (!SetControlOptions(std::move(Options), Error)) return false;
		if (Owner)
		{
			auto &State = Owner->FloatingStates[Identity];
			State.Widget = shared_from_this();
			State.bModal = Control.bModal;
			if (!bOpen)
			{
				Owner->RetireFloatingFocus(Identity, State);
			}
			State.bOpen = bOpen;
		}
		return true;
	}

	const FControlOptions &FWidget::GetControlOptions() const noexcept
	{
		return Control;
	}

	bool FWidget::SetControlCallback(FControlCallback Callback, std::string &Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		ControlCallback = std::move(Callback);
		return true;
	}

	bool FWidget::Activate(std::string &Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		const auto Owner = Context.lock();
		if (!Owner || !Owner->Eligible(shared_from_this(), false, false))
		{
			Error = "gui.control_not_interactive";
			return false;
		}
		FControlChange Change;
		if (Control.Kind == EControlKind::RadioButton)
		{
			return Owner->SelectRadio(shared_from_this(), Error);
		}
		if (Control.Kind == EControlKind::CheckBox || Control.Kind == EControlKind::ToggleSwitch)
		{
			Change.Action = EControlAction::CheckedChanged;
			Change.bChecked = !Control.bChecked;
		}
		else if (Control.Kind != EControlKind::Button)
		{
			Error = "gui.control_not_activatable";
			return false;
		}
		return Owner->NotifyControl(shared_from_this(), std::move(Change), Error);
	}

	bool FWidget::FocusControl(std::string &Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		const auto Owner = Context.lock();
		if (!Owner)
		{
			Error = "gui.widget_not_mounted";
			return false;
		}
		return Owner->SetFocus(shared_from_this(), Error);
	}

	bool FWidget::SetTextSelection(std::int64_t Start, std::int64_t End, std::string &Error)
	{
		if (!Check(Error, true))
		{
			return false;
		}
		const auto Owner = Context.lock();
		if (!Owner || !bHasLayout)
		{
			Error = "gui.update_required";
			return false;
		}
		if (!IsTextControl(Control.Kind) || Start < 0 || End < Start || End > INT32_MAX)
		{
			Error = "gui.invalid_text_selection";
			return false;
		}
		return Owner->Backend->SetTextSelection(Identity, Start, End, Error);
	}

	bool FContext::SetTextInputServices(FTextInputServices Services, std::string &Error)
	{
		const auto KeepAlive = weak_from_this().lock();
		if (!Check(Error))
		{
			return false;
		}
		if (bUpdating || bDispatching || bDraining || FWidget::IsInLifecycle())
		{
			Error = "gui.reentrant_operation";
			return false;
		}
		if (TextFocus && !FocusBackendTextControl(0, TextInputServices, Error))
		{
			return false;
		}
		TextFocus = 0;
		{
			struct FReplacementGuard
			{
				~FReplacementGuard()
				{
					Flag = false;
				}
				bool& Flag;
			} Guard{bRefreshingTextServices};
			bRefreshingTextServices = true;
			TextInputServices = std::move(Services);
		}
		return RefreshTextFocus(Error);
	}

	bool FContext::NotifyControl(const std::shared_ptr<FWidget> &Widget, FControlChange Change, std::string &Error)
	{
		if (!Eligible(Widget, false, false))
		{
			return true;
		}
		if (!std::isfinite(Change.Value) || !std::isfinite(Change.X) || !std::isfinite(Change.Y) ||
		    !ValidateGuiText(Change.Text) || Change.Text.size() > 1024 * 1024)
		{
			Error = "gui.invalid_control_change";
			return false;
		}
		auto &O = Widget->Control;
		switch (Change.Action)
		{
		case EControlAction::CheckedChanged:
			{
				O.bChecked = Change.bChecked;
				break;
			}
		case EControlAction::ValueChanged:
			{
				Change.Value = NormalizeControlValue(O, Change.Value);
				if (O.Value == Change.Value)
				{
					return true;
				}
				O.Value = Change.Value;
				break;
			}
		case EControlAction::TextChanged:
			{
				if (O.Text == Change.Text)
				{
					return true;
				}
				O.Text = Change.Text;
				break;
			}
		case EControlAction::Scrolled:
			{
				if (O.ScrollX == Change.X && O.ScrollY == Change.Y)
				{
					return true;
				}
				O.ScrollX = Change.X;
				O.ScrollY = Change.Y;
				break;
			}
		case EControlAction::SelectionChanged:
			{
				if (O.Text == Change.Text)
				{
					return true;
				}
				O.Text = Change.Text;
				break;
			}
		case EControlAction::OpenChanged:
			{
				if (O.bOpen == Change.bChecked)
				{
					return true;
				}
				O.bOpen = Change.bChecked;
				if (!O.bOpen)
				{
					const auto State = FloatingStates.find(Widget->Identity);
					if (State != FloatingStates.end())
					{
						RetireFloatingFocus(Widget->Identity, State->second);
						State->second.bOpen = false;
					}
				}
				break;
			}
		default:
			{
				break;
			}
		}
		Widget->MarkDirty();
		// Copy before invoking: handlers may dispose, detach, or replace their own callback.
		try
		{
			auto Callback = Widget->ControlCallback;
			return !Callback || Callback(Change, Error);
		}
		catch (const std::exception &Exception)
		{
			Error = std::string("gui.callback_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.callback_exception";
		}
		return false;
	}

	bool FContext::RevealFocus(const std::shared_ptr<FWidget> &Widget, std::string &Error)
	{
		if (!Widget || !Widget->bHasLayout)
		{
			return true;
		}
		auto Target = Widget->Layout.Border;
		const auto Reveal = [](double Start, double Size, double ViewStart, double ViewSize)
		{
			// A target larger than the viewport must not oscillate between its
			// two edges.
			if (Start <= ViewStart && Start + Size >= ViewStart + ViewSize)
			{
				return 0.0;
			}
			return Start < ViewStart ? Start - ViewStart : std::max(0.0, Start + Size - ViewStart - ViewSize);
		};
		for (auto Node = Widget; Node; Node = Node->Parent.lock())
		{
			// Floating content escapes its logical ancestors' clipping and
			// scrolling.
			if (IsFloatingControl(Node->Control.Kind))
			{
				break;
			}
			const auto Scroll = Node->Parent.lock();
			if (!Scroll || Scroll->Control.Kind != EControlKind::ScrollView || !Scroll->bHasLayout)
			{
				continue;
			}
			const auto Box = Scroll->Layout;
			const auto MaxX = std::max(0.0, Box.ScrollWidth - Box.Content.Width);
			const auto MaxY = std::max(0.0, Box.ScrollHeight - Box.Content.Height);
			const auto X = std::clamp(Scroll->Control.ScrollX, 0.0, MaxX);
			const auto Y = std::clamp(Scroll->Control.ScrollY, 0.0, MaxY);
			// Account for focus changes queued since the last completed layout.
			Target.X -= X - Box.ScrollX;
			Target.Y -= Y - Box.ScrollY;
			FControlChange Change;
			Change.Action = EControlAction::Scrolled;
			Change.X = std::clamp(X + Reveal(Target.X, Target.Width, Box.Content.X, Box.Content.Width), 0.0, MaxX);
			Change.Y = std::clamp(Y + Reveal(Target.Y, Target.Height, Box.Content.Y, Box.Content.Height), 0.0, MaxY);
			if (Change.X != X || Change.Y != Y)
			{
				if (!NotifyControl(Scroll, Change, Error) || !Check(Error))
				{
					return false;
				}
				if (Focus.lock() != Widget || !Eligible(Widget, false, true))
				{
					return true;
				}
				Target.X -= std::clamp(Scroll->Control.ScrollX, 0.0, MaxX) - X;
				Target.Y -= std::clamp(Scroll->Control.ScrollY, 0.0, MaxY) - Y;
			}
		}
		return true;
	}

	bool FContext::FocusBackendTextControl(std::uint64_t Id, const FTextInputServices &Services, std::string &Error)
	{
		if (bRefreshingTextServices)
		{
			Error = "gui.reentrant_operation: text input service callback is in progress";
			return false;
		}
		struct FRefreshGuard
		{
			bool &Flag;
			~FRefreshGuard()
			{
				Flag = false;
			}
		} Guard{bRefreshingTextServices};
		bRefreshingTextServices = true;
		try
		{
			return Backend->FocusTextControl(Id, Services, Error);
		}
		catch (const std::exception &Exception)
		{
			Error = std::string("gui.callback_exception: ") + Exception.what();
		}
		catch (...)
		{
			Error = "gui.callback_exception";
		}
		return false;
	}

	bool FContext::RefreshTextFocus(std::string &Error)
	{
		const auto Widget = GetFocus();
		const auto Id = Widget && IsTextControl(Widget->Control.Kind) && Widget->bHasLayout ? Widget->Identity : 0;
		if (Id == TextFocus)
		{
			return true;
		}
		if (!FocusBackendTextControl(Id, TextInputServices, Error))
		{
			return false;
		}
		TextFocus = Id;
		return true;
	}

	void FContext::CancelControlPress() noexcept
	{
		ActiveControl.reset();
		ControlKey = Platform::EKey::Unknown;
		ControlPresses.clear();
		ScrollGestures.clear();
	}
} // namespace VeroScript::Gui
