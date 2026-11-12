// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiControlPaint.h"
#include <algorithm>
#include <cmath>

namespace VeroScript::Gui
{
	bool FContext::PerformControlAction(FEvent &Event, bool &bHandled, std::string &Error)
	{
		using Platform::EEventType;
		using Platform::EKey;
		const auto &I = Event.Input;
		if (!PerformAdditionalControlAction(Event, bHandled, Error))
		{
			return false;
		}
		if (bHandled)
		{
			return true;
		}
		const auto Pointer = FPointerIdentity::FromEvent(I);
		if ((I.Type == EEventType::PointerDown || I.Type == EEventType::PointerUp) &&
		    I.PointerKind != Platform::EPointerKind::Touch && I.Button != 1)
		{
			return true;
		}
		auto W = Event.Target;
		while (W && (W->Control.Kind == EControlKind::None || W->Control.Kind == EControlKind::ProgressBar || W->Control.Kind == EControlKind::Grid || W->Control.Kind == EControlKind::Wrap || W->Control.Kind == EControlKind::RadioGroup))
		{
			W = W->Parent.lock();
		}
		if (W && IsFloatingControl(W->Control.Kind))
		{
			W.reset();
		}
		if (I.PointerKind == Platform::EPointerKind::Touch)
		{
			if (I.Type == EEventType::PointerDown)
			{
				const bool bRangeGesture =
				    W && (W->Control.Kind == EControlKind::Slider || W->Control.Kind == EControlKind::ScrollBar || W->Control.Kind == EControlKind::Splitter);
				for (auto Scroll = bRangeGesture ? nullptr : Event.Target; Scroll; Scroll = Scroll->Parent.lock())
				{
					if (Scroll->Control.Kind == EControlKind::ScrollView && Eligible(Scroll, true, false) &&
					    (Scroll->Layout.ScrollWidth > Scroll->Layout.Content.Width ||
					        Scroll->Layout.ScrollHeight > Scroll->Layout.Content.Height))
					{
						if (!CapturePointer(Pointer, Scroll, Error))
						{
							return false;
						}
						ScrollGestures[Pointer] = {Scroll, I.X, I.Y, I.X, I.Y, false};
						break;
					}
				}
			}
			else if (I.Type == EEventType::PointerMove)
			{
				const auto At = ScrollGestures.find(Pointer);
				if (At != ScrollGestures.end())
				{
					auto &G = At->second;
					const auto Scroll = G.Widget.lock();
					if (!Eligible(Scroll, true, false))
					{
						ScrollGestures.erase(At);
						return true;
					}
					if (!G.bPanning && std::hypot(I.X - G.StartX, I.Y - G.StartY) >= 6)
					{
						const auto Press = ControlPresses.find(Pointer);
						const auto Previous = Press == ControlPresses.end() ? nullptr : Press->second.Widget.lock();
						if (Previous && IsTextControl(Previous->Control.Kind))
						{
							auto Cancel = I;
							Cancel.Type = EEventType::PointerCancelled;
							FTextEditResult Ignored;
							if (!Backend->EditText(Previous->Identity, Cancel, Ignored, Error))
							{
								return false;
							}
						}
						if (!CapturePointer(Pointer, Scroll, Error))
						{
							return false;
						}
						ControlPresses[Pointer] = {Scroll, 0};
						G.bPanning = true;
						for (auto P = Pressed.begin(); P != Pressed.end();)
						{
							if (P->first.first == Pointer)
							{
								P = Pressed.erase(P);
							}
							else
							{
								++P;
							}
						}
					}
					if (G.bPanning)
					{
						const auto &B = Scroll->Layout;
						const auto MaxX = std::max(0.0, B.ScrollWidth - B.Content.Width);
						const auto MaxY = std::max(0.0, B.ScrollHeight - B.Content.Height);
						FControlChange Change;
						Change.Action = EControlAction::Scrolled;
						Change.X =
						    std::clamp(std::clamp(Scroll->Control.ScrollX, 0.0, MaxX) + G.LastX - I.X, 0.0, MaxX);
						Change.Y =
						    std::clamp(std::clamp(Scroll->Control.ScrollY, 0.0, MaxY) + G.LastY - I.Y, 0.0, MaxY);
						G.LastX = I.X;
						G.LastY = I.Y;
						bHandled = true;
						return NotifyControl(Scroll, std::move(Change), Error);
					}
				}
			}
		}

		if (I.Type == EEventType::KeyDown && I.Key == EKey::Tab)
		{
			const auto Focused = GetFocus();
			if (Focused && Backend->IsTextComposing(Focused->Identity))
			{
				bHandled = true;
				return true;
			}
			std::vector<std::shared_ptr<FWidget>> Candidates;
			const auto Modal = GetModalRoot();
			for (const auto &Item : LayoutNodes)
			{
				if (auto Candidate = Item.lock(); Candidate != Modal && Eligible(Candidate, false, true))
				{
					Candidates.push_back(std::move(Candidate));
				}
			}
			if (Candidates.empty())
			{
				if (!Eligible(Modal, false, true)) return true;
				Candidates.push_back(Modal);
			}
			const auto At = std::find(Candidates.begin(), Candidates.end(), GetFocus());
			const auto Count = static_cast<std::ptrdiff_t>(Candidates.size());
			auto Index = At == Candidates.end() ? ((I.Modifiers & 1) ? 0 : -1) : At - Candidates.begin();
			Index = (Index + ((I.Modifiers & 1) ? Count - 1 : 1)) % Count;
			bHandled = true;
			return SetFocus(Candidates[static_cast<std::size_t>(Index)], Error);
		}
		if (I.Type == EEventType::PointerUp || I.Type == EEventType::PointerMove ||
		    I.Type == EEventType::PointerCancelled)
		{
			if (auto At = ControlPresses.find(Pointer); At != ControlPresses.end())
			{
				W = At->second.Widget.lock();
			}
		}
		if (I.Type == EEventType::KeyUp && ControlKey == I.Key && ControlKeyboardDevice == I.DeviceId)
		{
			W = ActiveControl.lock();
		}
		if (!W || !Eligible(W, false, false))
		{
			return RefreshTextFocus(Error);
		}
		const auto O = W->Control;
		const bool bActivate = O.Kind == EControlKind::Button || O.Kind == EControlKind::CheckBox ||
		    O.Kind == EControlKind::ToggleSwitch || O.Kind == EControlKind::RadioButton;
		const bool bRange =
		    O.Kind == EControlKind::Slider || O.Kind == EControlKind::ScrollBar || O.Kind == EControlKind::Splitter;
		if (O.Kind == EControlKind::Splitter && I.Type == EEventType::PointerDown)
		{
			const auto &B = W->Layout.Content;
			const auto Size = O.bVertical ? B.Height : B.Width;
			const auto At = (O.bVertical ? I.Y - B.Y : I.X - B.X);
			const auto Start = std::max(0.0, Size - O.DividerSize) * O.Value;
			if (At < Start || At > Start + O.DividerSize)
			{
				return true;
			}
		}
		if (I.Type == EEventType::PointerDown && (I.Button == 1 || I.PointerKind == Platform::EPointerKind::Touch))
		{
			// Keep a partially visible control stationary under the pressing pointer.
			if (Eligible(W, false, true) && !SetFocusTarget(W, false, Error))
			{
				return false;
			}
			if (bActivate || bRange || IsTextControl(O.Kind))
			{
				if (std::any_of(ControlPresses.begin(), ControlPresses.end(),
				        [&](const auto &P)
				{
					return P.second.Widget.lock() == W;
				}))
				{
					return true;
				}
				if (!CapturePointer(Pointer, W, Error))
				{
					return false;
				}
				ControlPresses[Pointer] = {W, 0};
				bHandled = true;
			}
		}
		if (I.Type == EEventType::PointerCancelled)
		{
			if (IsTextControl(O.Kind))
			{
				FTextEditResult Ignored;
				if (!Backend->EditText(W->Identity, I, Ignored, Error))
				{
					return false;
				}
			}

			ControlPresses.erase(Pointer);
			Captures.erase(Pointer);
			return true;
		}
		if (I.Type == EEventType::PointerUp && (I.Button == 1 || I.PointerKind == Platform::EPointerKind::Touch) &&
		    ControlPresses.count(Pointer))
		{
			ControlPresses.erase(Pointer);
			Captures.erase(Pointer);
			bHandled = true;
			if (bActivate && Backend->Contains(W->Identity, I.X, I.Y).value_or(W->Layout.Border.Contains(I.X, I.Y)))
			{
				return W->Activate(Error);
			}
		}
		if (bActivate && I.Type == EEventType::KeyDown && (I.Key == EKey::Space || I.Key == EKey::Enter) && !I.bRepeat)
		{
			ActiveControl = W;
			ControlKey = I.Key;
			ControlKeyboardDevice = I.DeviceId;
			bHandled = true;
		}
		if (bActivate && I.Type == EEventType::KeyUp && ActiveControl.lock() == W && ControlKey == I.Key &&
		    ControlKeyboardDevice == I.DeviceId)
		{
			ActiveControl.reset();
			ControlKey = EKey::Unknown;
			bHandled = true;
			return W->Activate(Error);
		}
		if (bRange && !O.bReadOnly)
		{
			double Next = O.Value;
			if (I.Type == EEventType::PointerDown ||
			    (I.Type == EEventType::PointerMove && ControlPresses.count(Pointer)))
			{
				if (I.Type == EEventType::PointerDown && I.Button != 1 &&
				    I.PointerKind != Platform::EPointerKind::Touch)
				{
					return true;
				}
				const auto &B = W->Layout.Content;
				const double Size = O.bVertical ? B.Height : B.Width;
				const double Position = O.bVertical ? I.Y - B.Y : I.X - B.X;
				const double Thumb =
				    O.Kind == EControlKind::Splitter ? std::min(O.DividerSize, Size) : ControlThumbSize(O, Size);
				const double Travel = std::max(0.0, Size - Thumb);
				const double Current = O.Maximum > O.Minimum ? (O.Value - O.Minimum) / (O.Maximum - O.Minimum) : 0;
				const double At = (O.Kind == EControlKind::Splitter
				                          ? O.Value
				                          : (O.bVertical && O.Kind == EControlKind::Slider ? 1 - Current : Current)) *
				    Travel;
				if (I.Type == EEventType::PointerDown)
				{
					ControlPresses[Pointer].GrabOffset =
					    Position >= At && Position <= At + Thumb ? Position - At : Thumb * .5;
				}
				auto Ratio =
				    Travel > 0 ? std::clamp((Position - ControlPresses[Pointer].GrabOffset) / Travel, 0.0, 1.0) : 0;
				if (O.bVertical && O.Kind == EControlKind::Slider)
				{
					Ratio = 1 - Ratio;
				}
				Next = O.Kind == EControlKind::Splitter ? Ratio : O.Minimum + Ratio * (O.Maximum - O.Minimum);
				bHandled = true;
			}
			else if (I.Type == EEventType::KeyDown)
			{
				const auto Step = O.Step > 0 ? O.Step : (O.Maximum - O.Minimum) / 100;
				switch (I.Key)
				{
				case EKey::Home:
					{
						Next = O.Minimum;
						break;
					}
				case EKey::End:
					{
						Next = O.Maximum;
						break;
					}
				case EKey::Down:
					{
						Next +=
						    O.Kind == EControlKind::ScrollBar || O.Kind == EControlKind::Splitter ? Step : -Step;
						break;
					}
				case EKey::Left:
					{
						Next -= Step;
						break;
					}
				case EKey::Up:
					{
						Next +=
						    O.Kind == EControlKind::ScrollBar || O.Kind == EControlKind::Splitter ? -Step : Step;
						break;
					}
				case EKey::Right:
					{
						Next += Step;
						break;
					}
				case EKey::PageDown:
					{
						Next += (O.Kind == EControlKind::ScrollBar || O.Kind == EControlKind::Splitter ? 1 : -1) *
						    std::max(Step, O.PageSize);
						break;
					}
				case EKey::PageUp:
					{
						Next += (O.Kind == EControlKind::ScrollBar || O.Kind == EControlKind::Splitter ? -1 : 1) *
						    std::max(Step, O.PageSize);
						break;
					}
				default:
					{
						return true;
					}
				}
				bHandled = true;
			}
			Next = std::clamp(Next, O.Minimum, O.Maximum);
			if (Next != O.Value)
			{
				FControlChange Change;
				Change.Action = EControlAction::ValueChanged;
				Change.Value = Next;
				return NotifyControl(W, std::move(Change), Error);
			}
		}
		if (O.Kind == EControlKind::ScrollView && I.Type == EEventType::KeyDown)
		{
			const auto &B = W->Layout;
			const auto MaxX = std::max(0.0, B.ScrollWidth - B.Content.Width);
			const auto MaxY = std::max(0.0, B.ScrollHeight - B.Content.Height);
			FControlChange Change;
			Change.Action = EControlAction::Scrolled;
			Change.X = std::clamp(W->Control.ScrollX, 0.0, MaxX);
			Change.Y = std::clamp(W->Control.ScrollY, 0.0, MaxY);
			switch (I.Key)
			{
			case EKey::Home:
				{
					Change.Y = 0;
					break;
				}
			case EKey::End:
				{
					Change.Y = B.ScrollHeight;
					break;
				}
			case EKey::PageUp:
				{
					Change.Y -= B.Content.Height;
					break;
				}
			case EKey::PageDown:
				{
					Change.Y += B.Content.Height;
					break;
				}
			case EKey::Up:
				{
					Change.Y -= 40;
					break;
				}
			case EKey::Down:
				{
					Change.Y += 40;
					break;
				}
			case EKey::Left:
				{
					Change.X -= 40;
					break;
				}
			case EKey::Right:
				{
					Change.X += 40;
					break;
				}
			default:
				{
					return true;
				}
			}
			Change.X = std::clamp(Change.X, 0.0, MaxX);
			Change.Y = std::clamp(Change.Y, 0.0, MaxY);
			bHandled = true;
			return NotifyControl(W, std::move(Change), Error);
		}
		if (I.Type == EEventType::Wheel)
		{
			if (IsTextControl(O.Kind))
			{
				FTextEditResult Result;
				if (!Backend->EditText(W->Identity, I, Result, Error))
				{
					return false;
				}
				if (Result.bHandled)
				{
					bHandled = true;
					return true;
				}
			}

			for (auto Scroll = Event.Target; Scroll; Scroll = Scroll->Parent.lock())
			{
				if (Scroll->Control.Kind != EControlKind::ScrollView)
				{
					continue;
				}
				const auto &B = Scroll->Layout;
				const auto MaxX = std::max(0.0, B.ScrollWidth - B.Content.Width);
				const auto MaxY = std::max(0.0, B.ScrollHeight - B.Content.Height);
				const auto StartX = std::clamp(Scroll->Control.ScrollX, 0.0, MaxX);
				const auto StartY = std::clamp(Scroll->Control.ScrollY, 0.0, MaxY);
				FControlChange Change;
				Change.Action = EControlAction::Scrolled;
				Change.X = std::clamp(StartX - I.DeltaX * 40, 0.0, MaxX);
				Change.Y = std::clamp(StartY - I.DeltaY * 40, 0.0, MaxY);
				if (Change.X != StartX || Change.Y != StartY)
				{
					bHandled = true;
					return NotifyControl(Scroll, std::move(Change), Error);
				}
			}
		}
		if (IsTextControl(O.Kind) && I.Type != EEventType::Wheel)
		{
			if (!RefreshTextFocus(Error))
			{
				return false;
			}
			FTextEditResult Result;
			if (!Backend->EditText(W->Identity, I, Result, Error))
			{
				return false;
			}
			bHandled = true;
			if (Result.bSubmitted)
			{
				if (O.Kind == EControlKind::NumberBox)
				{
					return CommitNumber(W, true, Error);
				}
				FControlChange Change;
				Change.Action = EControlAction::Submitted;
				Change.Text = std::move(Result.Text);
				return NotifyControl(W, std::move(Change), Error);
			}
			if (Result.bChanged)
			{
				if (O.Kind == EControlKind::NumberBox)
				{
					W->Control.Text = std::move(Result.Text);
					W->MarkDirty();
					return true;
				}
				FControlChange Change;
				Change.Action = EControlAction::TextChanged;
				Change.Text = std::move(Result.Text);
				return NotifyControl(W, std::move(Change), Error);
			}
		}
		return true;
	}
} // namespace VeroScript::Gui
