// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include <algorithm>
#include <set>

namespace VeroScript::Gui
{
	namespace
	{
		bool IsWithin(std::shared_ptr<FWidget> Node, const std::shared_ptr<FWidget> &Ancestor)
		{
			for (; Node; Node = Node->GetParent())
			{
				if (Node == Ancestor)
				{
					return true;
				}
			}
			return false;
		}
		std::shared_ptr<FWidget> RadioGroup(const std::shared_ptr<FWidget> &Widget)
		{
			for (auto Parent = Widget->GetParent(); Parent; Parent = Parent->GetParent())
			{
				if (Parent->GetControlOptions().Kind == EControlKind::RadioGroup)
				{
					return Parent;
				}
			}
			return {};
		}
	} // namespace

	bool FContext::PrepareAdditionalControls(const std::vector<std::shared_ptr<FWidget>> &Nodes, double Time,
	    bool bFloating, std::string &Error)
	{
		std::map<std::uint64_t, std::set<std::string>> Choices;
		std::set<std::uint64_t> Floating;
		if (bFloating)
		{
			bHasModalControls = false;
		}
		for (const auto &Node : Nodes)
		{
			auto &O = Node->Control;
			if (bFloating)
			{
				bHasModalControls = bHasModalControls || (O.bModal && O.bOpen);
			}
			if (O.Kind == EControlKind::Splitter &&
			    std::count_if(Node->Children.begin(), Node->Children.end(),
			        [](const auto &Child)
			{
				return !IsFloatingControl(Child->Control.Kind);
			}) > 2)
			{
				Error = "gui.splitter_child_limit";
				return false;
			}
			if (O.Kind == EControlKind::RadioButton)
			{
				const auto Group = RadioGroup(Node);
				if (!Group || O.ChoiceKey.empty() || !Choices[Group->Identity].insert(O.ChoiceKey).second)
				{
					Error = "gui.invalid_radio_group: choices require unique nonempty keys and a RadioGroup ancestor";
					return false;
				}
				const bool bChecked = Group->Control.Text == O.ChoiceKey;
				if (O.bChecked != bChecked)
				{
					O.bChecked = bChecked;
					Node->MarkDirty();
				}
			}
			if (!bFloating || !IsFloatingControl(O.Kind))
			{
				continue;
			}
			Floating.insert(Node->Identity);
			auto &State = FloatingStates[Node->Identity];
			State.Widget = Node;
			if (O.Kind == EControlKind::ToolTip && O.bAutoOpen)
			{
				const auto Anchor = Node->Parent.lock();
				const bool bHovered =
				    Anchor && std::any_of(Hovered.begin(), Hovered.end(), [&](const auto &H)
				{
					for (auto Hover = H.second.lock(); Hover; Hover = Hover->Parent.lock())
					{
						if (Hover == Anchor)
						{
							return true;
						}
						if (IsFloatingControl(Hover->Control.Kind))
						{
							break;
						}
					}
					return false;
				});
				if (bHovered && !State.bHovered)
				{
					State.HoverSince = Time;
				}
				State.bHovered = bHovered;
				O.bOpen = bHovered && Time - State.HoverSince >= O.Delay;
			}
			if (O.bOpen && !State.bOpen)
			{
				if (!O.bModal || !State.ModalOrder)
				{
					RememberFloatingFocus(State);
				}
				if (O.bModal && !State.ModalOrder)
				{
					if (NextModalOrder == UINT64_MAX)
					{
						Error = "gui.invalid_control_options";
						return false;
					}
					State.ModalOrder = ++NextModalOrder;
				}
			}
			else if (!O.bOpen && State.bOpen)
			{
				RetireFloatingFocus(Node->Identity, State);
			}
			State.bModal = O.bModal;
			State.bOpen = O.bOpen;
		}
		for (auto It = FloatingStates.begin(); bFloating && It != FloatingStates.end();)
		{
			if (!Floating.count(It->first))
			{
				if (It->second.bOpen)
				{
					RetireFloatingFocus(It->first, It->second);
				}
				It = FloatingStates.erase(It);
			}
			else
			{
				++It;
			}
		}
		return true;
	}

	bool FContext::PerformAdditionalControlAction(FEvent &Event, bool &bHandled, std::string &Error)
	{
		using Platform::EEventType;
		using Platform::EKey;
		const auto &I = Event.Input;
		const auto Focused = GetFocus();
		if (I.Type == EEventType::PointerDown || (I.Type == EEventType::KeyDown && I.Key == EKey::Escape && (!Focused || !Backend->IsTextComposing(Focused->Identity))))
		{
			for (auto It = LayoutNodes.rbegin(); It != LayoutNodes.rend(); ++It)
			{
				const auto Popup = It->lock();
				if (!Popup || Popup->Control.Kind != EControlKind::Popup || !Popup->Control.bOpen ||
				    !Eligible(Popup, false, false))
				{
					continue;
				}
				if (I.Type == EEventType::KeyDown ? Popup->Control.bDismissOnEscape : (Popup->Control.bDismissOnOutside && !IsWithin(Event.Target, Popup)))
				{
					FControlChange Change;
					Change.Action = EControlAction::OpenChanged;
					Change.bChecked = false;
					Change.Text = I.Type == EEventType::KeyDown ? "escape" : "outside";
					bHandled = true;
					return NotifyControl(Popup, Change, Error);
				}
				break;
			}
		}
		const auto W = Event.Target;
		if (W && W->Control.Kind == EControlKind::NumberBox && I.Type == EEventType::KeyDown &&
		    !Backend->IsTextComposing(W->Identity))
		{
			if (I.Key == EKey::Escape)
			{
				W->Control.Text = FormatControlNumber(W->Control.Value);
				W->MarkDirty();
				bHandled = true;
			}
			else if ((I.Key == EKey::Up || I.Key == EKey::Down) && !W->Control.bReadOnly)
			{
				FControlChange Change;
				Change.Action = EControlAction::ValueChanged;
				Change.Value = std::clamp(W->Control.Value + (I.Key == EKey::Up ? 1 : -1) * (W->Control.Step > 0 ? W->Control.Step : 1),
				    W->Control.Minimum, W->Control.Maximum);
				bHandled = true;
				if (!NotifyControl(W, Change, Error))
				{
					return false;
				}
				W->Control.Text = FormatControlNumber(W->Control.Value);
				W->MarkDirty();
			}
		}
		if (W && W->Control.Kind == EControlKind::RadioButton && I.Type == EEventType::KeyDown &&
		    (I.Key == EKey::Left || I.Key == EKey::Right || I.Key == EKey::Up || I.Key == EKey::Down))
		{
			std::vector<std::shared_ptr<FWidget>> Choices;
			const auto Group = RadioGroup(W);
			for (const auto &Item : LayoutNodes)
			{
				const auto Choice = Item.lock();
				if (Choice && Choice->Control.Kind == EControlKind::RadioButton && RadioGroup(Choice) == Group &&
				    Eligible(Choice, false, true))
				{
					Choices.push_back(Choice);
				}
			}
			const auto At = std::find(Choices.begin(), Choices.end(), W);
			if (At != Choices.end())
			{
				const auto N = static_cast<std::ptrdiff_t>(Choices.size());
				const auto Delta = I.Key == EKey::Left || I.Key == EKey::Up ? N - 1 : 1;
				const auto Next = Choices[static_cast<std::size_t>((At - Choices.begin() + Delta) % N)];
				bHandled = true;
				if (!SetFocus(Next, Error))
				{
					return false;
				}
				// Revealing focus can invoke scroll callbacks that remove or redirect the target.
				return GetFocus() != Next || SelectRadio(Next, Error);
			}
		}
		return true;
	}

	void FContext::RememberFloatingFocus(FFloatingState &State)
	{
		State.PreviousFocus = Focus;
		auto Container = Focus.lock();
		while (Container && !IsFloatingControl(Container->Control.Kind))
		{
			Container = Container->Parent.lock();
		}
		State.PreviousPortal = Container ? Container->Identity : 0;
	}

	void FContext::RetireFloatingFocus(std::uint64_t Id, const FFloatingState &State)
	{
		// Remove the closed portal from focus history before it can be reopened.
		// Portal identities also survive disposal of the formerly focused child.
		for (auto &Entry : FloatingStates)
		{
			if (Entry.first != Id && Entry.second.PreviousPortal == Id)
			{
				Entry.second.PreviousFocus = State.PreviousFocus;
				Entry.second.PreviousPortal = State.PreviousPortal;
			}
		}
		if (!bInputActive || (!IsWithin(Focus.lock(), State.Widget.lock()) && !(State.bModal && !Eligible(Focus.lock(), false, true))))
		{
			return;
		}
		auto Previous = State.PreviousFocus.lock();
		auto Portal = State.PreviousPortal;
		for (std::size_t Count = 0; !Eligible(Previous, false, true) && Portal && Count < FloatingStates.size(); ++Count)
		{
			const auto Found = FloatingStates.find(Portal);
			if (Found == FloatingStates.end() || Found->first == Id)
			{
				break;
			}
			Previous = Found->second.PreviousFocus.lock();
			Portal = Found->second.PreviousPortal;
		}
		Focus = Eligible(Previous, false, true) ? Previous : nullptr;
	}

	bool FContext::CommitNumber(const std::shared_ptr<FWidget> &Widget, bool bSubmit, std::string &Error)
	{
		if (!Widget || Widget->Control.Kind != EControlKind::NumberBox)
		{
			return true;
		}
		double Value = Widget->Control.Value;
		if (!Widget->Control.bReadOnly && ParseControlNumber(Widget->Control.Text, Value))
		{
			FControlChange Change;
			Change.Action = EControlAction::ValueChanged;
			Change.Value = Value;
			if (!NotifyControl(Widget, Change, Error))
			{
				return false;
			}
		}
		Widget->Control.Text = FormatControlNumber(Widget->Control.Value);
		Widget->MarkDirty();
		if (bSubmit)
		{
			FControlChange Change;
			Change.Action = EControlAction::Submitted;
			Change.Value = Widget->Control.Value;
			return NotifyControl(Widget, Change, Error);
		}
		return true;
	}

	bool FContext::SelectRadio(const std::shared_ptr<FWidget> &Widget, std::string &Error)
	{
		const auto Group = RadioGroup(Widget);
		if (!Group || Widget->Control.ChoiceKey.empty())
		{
			Error = "gui.invalid_radio_group";
			return false;
		}
		if (Group->Control.Text == Widget->Control.ChoiceKey)
		{
			return true;
		}
		FControlChange Change;
		Change.Action = EControlAction::SelectionChanged;
		Change.Text = Widget->Control.ChoiceKey;
		// Publish the complete group state before user callbacks can inspect or change it.
		std::vector<std::shared_ptr<FWidget>> Pending{Group};
		while (!Pending.empty())
		{
			const auto Choice = Pending.back();
			Pending.pop_back();
			if (Choice != Group && Choice->Control.Kind == EControlKind::RadioGroup)
			{
				continue;
			}
			if (Choice->Control.Kind == EControlKind::RadioButton)
			{
				const bool bChecked = Choice->Control.ChoiceKey == Change.Text;
				if (Choice->Control.bChecked != bChecked)
				{
					Choice->Control.bChecked = bChecked;
					Choice->MarkDirty();
				}
			}
			Pending.insert(Pending.end(), Choice->Children.begin(), Choice->Children.end());
		}
		if (!NotifyControl(Group, Change, Error))
		{
			return false;
		}
		// A group handler may select another choice, detach the widget, or replace its kind/key.
		if (Group->Control.Kind != EControlKind::RadioGroup || Widget->Control.Kind != EControlKind::RadioButton ||
		    Group->Control.Text != Change.Text || Widget->Control.ChoiceKey != Change.Text ||
		    !Widget->Control.bChecked || RadioGroup(Widget) != Group)
		{
			return true;
		}
		Change.Action = EControlAction::CheckedChanged;
		Change.bChecked = true;
		return NotifyControl(Widget, std::move(Change), Error);
	}
} // namespace VeroScript::Gui
